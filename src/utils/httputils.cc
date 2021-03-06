/*
	Flexisip, a flexible SIP proxy server with media capabilities.
	Copyright (C) 2016  Belledonne Communications SARL.

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

#include <sofia-sip/url.h>

#include "httputils.hh"

using namespace std;

const char *HttpUtils::mReservedChars = "!*'();:@&=+$,/?#[]";

std::string HttpUtils::escapeReservedCharacters(const char *str) {
	string escapedStr;
	if (url_reserved_p(str)) {
		escapedStr.resize(url_esclen(str, mReservedChars));
		url_escape(&escapedStr.at(0), str, mReservedChars);
	} else {
		escapedStr = str;
	}
	return escapedStr;
}
