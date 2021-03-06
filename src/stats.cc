/*
	Flexisip, a flexible SIP proxy server with media capabilities.
	Copyright (C) 2010-2016  Belledonne Communications SARL, All rights reserved.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <poll.h>

#include "stats.hh"
#include "log/logmanager.hh"


Stats::Stats(const std::string &name) {
	mName = name;
	mRunning = false;
	if (pipe(mControlFds) == -1){
		LOGF("Cannot create control pipe of Stats thread: %s", strerror(errno));
	}
}

void Stats::start() {
	mRunning = true;
	pthread_create(&mThread, NULL, &Stats::threadfunc, this);
}

void Stats::stop() {
	if (mRunning) {
		mRunning = false;
		if (write(mControlFds[1], "please stop", 1) == -1){
			LOGF("Cannot write to control pipe of Stats thread: %s", strerror(errno));
		}
		pthread_join(mThread, NULL);
	}
}

static void split(const std::string &s, char delim, std::vector<std::string> &elems) {
	std::stringstream ss;
	ss.str(s);
	std::string item;
	while (std::getline(ss, item, delim)) {
		elems.push_back(item);
	}
}

static std::vector<std::string> split(const std::string &s, char delim) {
	std::vector<std::string> elems;
	split(s, delim, elems);
	return elems;
}

GenericEntry* Stats::find(GenericStruct *root, std::vector<std::string> &path) {
	std::string elem = path.front();
	path.erase(path.begin());
	for (auto it = root->getChildren().begin(); it != root->getChildren().end(); ++it) {
		GenericEntry *entry = *it;
		if (entry && entry->getName().compare(elem) == 0) {
			if (path.empty()) {
				return entry;
			} else {
				GenericStruct *gstruct = dynamic_cast<GenericStruct *>(entry);
				if (gstruct) {
					return find(gstruct, path);
				} else {
					return NULL;
				}
			}
		}
	}
	return NULL;
}

static std::string printEntry(GenericEntry *entry, bool printHelpInsteadOfValue) {
	GenericStruct *gstruct = dynamic_cast<GenericStruct *>(entry);
	bool isNode = gstruct != NULL;
	std::string answer = "";
	
	if (printHelpInsteadOfValue) {
		if (isNode) answer += "[";
		answer += entry->getName();
		if (isNode) answer += "]";
		answer += " : " + entry->getHelp();
	} else {
		if (isNode) {
			return "[" + gstruct->getName() + "]";
		} else {
			StatCounter64 *counter = dynamic_cast<StatCounter64 *>(entry);
			if (counter) {
				return counter->getName() + " : " + std::to_string(counter->read());
			} else {
				ConfigValue *value = dynamic_cast<ConfigValue *>(entry);
				if (value) {
					return value->getName() + " : " + value->get();
				}
			}
		}
	}
	return answer;
}

static std::string printSection(GenericStruct *gstruct, bool printHelpInsteadOfValue) {
	std::string answer = "";
	for (auto it = gstruct->getChildren().begin(); it != gstruct->getChildren().end(); ++it) {
		GenericEntry *child = *it;
		if (child) {
			answer += printEntry(child, printHelpInsteadOfValue) + "\r\n";
		}
	}
	return answer;
}

static void updateLogsVerbosity(GenericManager *manager) {
	std::string loglevel = manager->getGlobal()->get<ConfigString>("log-level")->read();
	std::string sysloglevel = manager->getGlobal()->get<ConfigString>("syslog-level")->read();
	bool user_errors = manager->getGlobal()->get<ConfigBoolean>("user-errors-logs")->read();
	flexisip::log::initLogs(flexisip_sUseSyslog, loglevel, sysloglevel, user_errors, false);
}

void Stats::parseAndAnswer(unsigned int socket, const std::string& query) {
	std::vector<std::string> query_split = split(query, ' ');
	std::string answer = "Error: unknown error";
	int size = query_split.size();
	
	if (size < 2) {
		answer = "Error: at least 2 arguments were expected, got " + std::to_string(size);
	} else {
		std::string command = query_split.front();
		std::string arg = query_split.at(1);
		std::vector<std::string> arg_split = split(arg, '/');
		GenericManager *manager = GenericManager::get();
		GenericStruct *root = manager->getRoot();
		GenericEntry *entry = NULL;
		
		if (arg.compare("all") == 0) {
			entry = root;
		} else {
			entry = find(root, arg_split);
		}
		
		if (entry) {
			if (strcmp("GET", command.c_str()) == 0) {
				GenericStruct *gstruct = dynamic_cast<GenericStruct *>(entry);
				if (gstruct) {
					answer = printSection(gstruct, false);
				} else {
					answer = printEntry(entry, false);
				}
			} else if (strcmp("LIST", command.c_str()) == 0) {
				GenericStruct *gstruct = dynamic_cast<GenericStruct *>(entry);
				if (gstruct) {
					answer = printSection(gstruct, true);
				} else {
					answer = printEntry(entry, true);
				}
			} else if (strcmp("SET", command.c_str()) == 0) {
				if (size < 3) {
					answer = "Error: at least 3 arguments were expected, got " + std::to_string(size);
				} else {
					std::string value = query_split.at(2);
					ConfigValue *config_value = dynamic_cast<ConfigValue *>(entry);
					if (config_value && strcmp("global/debug", arg.c_str()) == 0) {
						config_value->set(value);
						updateLogsVerbosity(manager);
						answer = "debug : " + value;
					} else if (config_value && strcmp("global/log-level", arg.c_str()) == 0) {
						config_value->set(value);
						updateLogsVerbosity(manager);
						answer = "log-level : " + value;
					} else if (config_value && strcmp("global/syslog-level", arg.c_str()) == 0) {
						config_value->set(value);
						updateLogsVerbosity(manager);
						answer = "syslog-level : " + value;
					} else {
						answer = "Only debug, log-level and syslog-level from global can be updated while flexisip is running";
					}
				}
			} else {
				answer = "Error: unknown command " + command;
			}
		} else {
			answer = "Error: " + arg + " not found";
		}
	}
	
	send(socket, answer.c_str(), answer.length(), 0);
}

void Stats::run() {
	int server_socket, child_socket;
	unsigned int remote_length;
	struct sockaddr_un local, remote;
	int local_length;
	struct pollfd pfd[2];
	
	if ((server_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		LOGE("Socket error %i : %s", errno, std::strerror(errno));
		stop();
	}
	local.sun_family = AF_UNIX;
	int pid = getpid();
	std::string path = "/tmp/flexisip-" + mName + "-" + std::to_string(pid);
	SLOGD << "Statistics socket is at " << path;
	strcpy(local.sun_path, path.c_str());
	unlink(local.sun_path);
	local_length = strlen(local.sun_path) + sizeof(local.sun_family);
	if (::bind(server_socket, (struct sockaddr *)&local, local_length) == -1) {
		LOGE("Bind error%i : %s", errno, std::strerror(errno));
		stop();
	}

	if (listen(server_socket, 1) == -1) {
		LOGE("Listen error %i : %s", errno, std::strerror(errno));
		stop();
	}
	
	while (mRunning) {
		memset(pfd, 0, sizeof(pfd));
		pfd[0].fd = server_socket;
		pfd[0].events = POLLIN;
		pfd[1].fd = mControlFds[0];
		pfd[1].events = POLLIN;
		
		int ret = poll(pfd,2,-1);
		
		if (ret == -1){
			if (errno != EINTR){
				LOGE("Stats thread getting poll() error: %s", strerror(errno));
			}
			continue;
		}else if (ret == 0){
			continue; /*timeout not possible*/
		}else{
			if (pfd[0].revents != POLLIN){
				continue;
			}
		}
		/*otherwise we have something to accept on our server_socket*/
		
		remote_length = sizeof(remote);
		if ((child_socket = accept(server_socket, (struct sockaddr *)&remote, &remote_length)) == -1) {
			LOGE("Accept error %i : %s", errno, std::strerror(errno));
			continue;
		}
		
		bool finished = false;
		do {
		    char buffer[512]={0};
		    int n = recv(child_socket, buffer, sizeof(buffer)-1, 0);
		    if (n < 0) {
				LOGE("Recv error %i : %s", errno, std::strerror(errno));
		    }
		    if (n > 0) {
				LOGD("[Stats] Received: %s", buffer);
				parseAndAnswer(child_socket, buffer);
		    }
		    finished = true;
		} while (!finished && mRunning);
		shutdown(child_socket, SHUT_RDWR);
		close(child_socket);
	}
	shutdown(server_socket, SHUT_RDWR);
	close(server_socket);
	unlink(path.c_str());
}

void *Stats::threadfunc(void *arg) {
	Stats *thiz = (Stats *)arg;
	thiz->run();
	return NULL;
}

Stats::~Stats() {
	if (mRunning) {
		stop();
	}
	close(mControlFds[0]);
	close(mControlFds[1]);
}
