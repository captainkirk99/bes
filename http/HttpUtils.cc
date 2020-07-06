// HttpCatalogUtils.cc
// -*- mode: c++; c-basic-offset:4 -*-
//
// This file is part of httpd_catalog_HTTPD_CATALOG, A C++ HTTPD_CATALOG that can be loaded in to
// the OPeNDAP Back-End Server (BES) and is able to handle remote requests.
//
// Copyright (c) 2018 OPeNDAP, Inc.
// Author: Nathan Potter <ndp@opendap.org>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
//
// You can contact OPeNDAP, Inc. at PO Box 112, Saunderstown, RI. 02874-0112.

#include "config.h"

#ifdef HAVE_UNISTD_H

#include <unistd.h>

#endif

#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include <curl/curl.h>

#include <GNURegex.h>
#include <util.h>

#include <BESUtil.h>
#include <BESCatalogUtils.h>
#include <BESCatalogList.h>
#include <BESCatalog.h>
#include <BESRegex.h>
#include <TheBESKeys.h>
#include <BESInternalError.h>
#include <BESNotFoundError.h>
#include <BESSyntaxUserError.h>
#include <BESDebug.h>

#include "HttpNames.h"
#include "HttpUtils.h"

#define MODULE "http"

using namespace libdap;
using namespace http;

// These are static class members
map<string, string> HttpUtils::MimeList;
string HttpUtils::ProxyProtocol;
string HttpUtils::ProxyHost;
string HttpUtils::ProxyUser;
string HttpUtils::ProxyPassword;
string HttpUtils::ProxyUserPW;

int HttpUtils::ProxyPort = 0;
int HttpUtils::ProxyAuthType = 0;
bool HttpUtils::useInternalCache = false;

string HttpUtils::NoProxyRegex;

#define prolog string("HttpUtils::").append(__func__).append("() - ")

// Initialization routine for the httpd_catalog_HTTPD_CATALOG for certain parameters
// and keys, like the white list, the MimeTypes translation.
void HttpUtils::Initialize()
{
    // MimeTypes - translate from a mime type to a module name
    bool found = false;
    string key = HTTP_MIMELIST;
    vector<string> vals;
    TheBESKeys::TheKeys()->get_values(key, vals, found);
    if (found && vals.size()) {
        vector<string>::iterator i = vals.begin();
        vector<string>::iterator e = vals.end();
        for (; i != e; i++) {
            size_t colon = (*i).find(":");
            if (colon == string::npos) {
                string err = (string) "Malformed " + HTTP_MIMELIST + " " + (*i) +
                             " specified in the gateway configuration";
                throw BESSyntaxUserError(err, __FILE__, __LINE__);
            }
            string mod = (*i).substr(0, colon);
            string mime = (*i).substr(colon + 1);
            MimeList[mod] = mime;
        }
    }

    found = false;
    key = HTTP_PROXYHOST;
    TheBESKeys::TheKeys()->get_value(key, HttpUtils::ProxyHost, found);
    if (found && !HttpUtils::ProxyHost.empty()) {
        // if the proxy host is set, then check to see if the port is
        // set. Does not need to be.
        found = false;
        key = HTTP_PROXYPORT;
        string port;
        TheBESKeys::TheKeys()->get_value(key, port, found);
        if (found && !port.empty()) {
            HttpUtils::ProxyPort = atoi(port.c_str());
            if (!HttpUtils::ProxyPort) {
                string err = (string) "httpd catalog proxy host is specified, but specified port is absent";
                throw BESSyntaxUserError(err, __FILE__, __LINE__);
            }
        }

        // @TODO Either use this or remove it - right now this variable is never used downstream
        // find the protocol to use for the proxy server. If none set, default to http
        found = false;
        key = HTTP_PROXYPROTOCOL;
        TheBESKeys::TheKeys()->get_value(key, HttpUtils::ProxyProtocol, found);
        if (!found || HttpUtils::ProxyProtocol.empty()) {
            HttpUtils::ProxyProtocol = "http";
        }

        // find the user to use for authenticating with the proxy server. If none set,
        // default to ""
        found = false;
        key = HTTP_PROXYUSER;
        TheBESKeys::TheKeys()->get_value(key, HttpUtils::ProxyUser, found);
        if (!found) {
            HttpUtils::ProxyUser = "";
        }

        // find the password to use for authenticating with the proxy server. If none set,
        // default to ""
        found = false;
        key = HTTP_PROXYPASSWORD;
        TheBESKeys::TheKeys()->get_value(key, HttpUtils::ProxyPassword, found);
        if (!found) {
            HttpUtils::ProxyPassword = "";
        }

        // find the user:password string to use for authenticating with the proxy server. If none set,
        // default to ""
        found = false;
        key = HTTP_PROXYUSERPW;
        TheBESKeys::TheKeys()->get_value(key, HttpUtils::ProxyUserPW, found);
        if (!found) {
            HttpUtils::ProxyUserPW = "";
        }

        // find the authentication mechanism to use with the proxy server. If none set,
        // default to BASIC authentication.
        found = false;
        key = HTTP_PROXYAUTHTYPE;
        string authType;
        TheBESKeys::TheKeys()->get_value(key, authType, found);
        if (found) {
            authType = BESUtil::lowercase(authType);
            if (authType == "basic") {
                HttpUtils::ProxyAuthType = CURLAUTH_BASIC;
                BESDEBUG(MODULE, prolog << "ProxyAuthType BASIC set." << endl);
            } else if (authType == "digest") {
                HttpUtils::ProxyAuthType = CURLAUTH_DIGEST;
                BESDEBUG(MODULE, prolog << "ProxyAuthType DIGEST set." << endl);
            } else if (authType == "ntlm") {
                HttpUtils::ProxyAuthType = CURLAUTH_NTLM;
                BESDEBUG(MODULE, prolog << "ProxyAuthType NTLM set." << endl);
            } else {
                HttpUtils::ProxyAuthType = CURLAUTH_BASIC;
                BESDEBUG(MODULE,
                         prolog << "User supplied an invalid value '" << authType
                                << "'  for Gateway.ProxyAuthType. Falling back to BASIC authentication scheme."
                                << endl);
            }
        } else {
            HttpUtils::ProxyAuthType = CURLAUTH_BASIC;
        }
    }

    found = false;
    key = HTTP_USE_INTERNAL_CACHE;
    string use_cache;
    TheBESKeys::TheKeys()->get_value(key, use_cache, found);
    if (found) {
        if (use_cache == "true" || use_cache == "TRUE" || use_cache == "True" || use_cache == "yes" ||
            use_cache == "YES" || use_cache == "Yes")
            HttpUtils::useInternalCache = true;
        else
            HttpUtils::useInternalCache = false;
    } else {
        // If not set, default to false. Assume squid or ...
        HttpUtils::useInternalCache = false;
    }
    // Grab the value for the NoProxy regex; empty if there is none.
    found = false; // Not used
    TheBESKeys::TheKeys()->get_value("Http.NoProxy", HttpUtils::NoProxyRegex, found);
}

// Not used. There's a better version of this that returns a string in libdap.
// jhrg 3/24/11

/**
 * Look for the type of handler that can read the filename found in the \arg disp.
 * The string \arg disp (probably from a HTTP Content-Dispoition header) has the
 * format:
 *
 * ~~~xml
 * filename[#|=]<value>[ <attribute name>[#|=]<value>]
 * ~~~
 *
 * @param disp The disposition string
 * @param type The type of the handler that can read this file or the empty
 * string if the BES Catalog Utils cannot find a handler to read it.
 */
void HttpUtils::Get_type_from_disposition(const string &disp, string &type)
{
    // If this function extracts a filename from disp and it matches a handler's
    // regex using the Catalog Utils, this will be set to a non-empty value.
    type = "";

    size_t fnpos = disp.find("filename");
    if (fnpos != string::npos) {
        // Got the filename attribute, now get the
        // filename, which is after the pound sign (#)
        size_t pos = disp.find("#", fnpos);
        if (pos == string::npos) pos = disp.find("=", fnpos);
        if (pos != string::npos) {
            // Got the filename to the end of the
            // string, now get it to either the end of
            // the string or the start of the next
            // attribute
            string filename;
            size_t sp = disp.find(" ", pos);
            if (pos != string::npos) {
                // space before the next attribute
                filename = disp.substr(pos + 1, sp - pos - 1);
            } else {
                // to the end of the string
                filename = disp.substr(pos + 1);
            }

            // now see if it's wrapped in quotes
            if (filename[0] == '"') {
                filename = filename.substr(1);
            }
            if (filename[filename.length() - 1] == '"') {
                filename = filename.substr(0, filename.length() - 1);
            }

            // we have the filename now, run it through
            // the type match to get the file type.

            const BESCatalogUtils *utils = BESCatalogList::TheCatalogList()->default_catalog()->get_catalog_utils();
            type = utils->get_handler_name(filename);
        }
    }
}

void HttpUtils::Get_type_from_content_type(const string &ctype, string &type)
{
    BESDEBUG(MODULE, prolog << "BEGIN content-type: " << ctype << endl);
    map<string, string>::iterator i = MimeList.begin();
    map<string, string>::iterator e = MimeList.end();
    bool done = false;
    for (; i != e && !done; i++) {
        BESDEBUG(MODULE, prolog << "Comparing content type '" << ctype << "' against mime list element '" << (*i).second << "'" << endl);
        BESDEBUG(MODULE, prolog << "first: " << (*i).first << "  second: " << (*i).second << endl);
        if ((*i).second == ctype) {
            BESDEBUG(MODULE, prolog << "MATCH" << endl);
            type = (*i).first;
            done = true;
        }
    }
    BESDEBUG(MODULE, prolog << "END" << endl);
}

void HttpUtils::Get_type_from_url(const string &url, string &type) {
    const BESCatalogUtils *utils = BESCatalogList::TheCatalogList()->find_catalog("catalog")->get_catalog_utils();

    type = utils->get_handler_name(url);
}




