// NgapContainer.cc

// -*- mode: c++; c-basic-offset:4 -*-

// This file is part of ngap_module, A C++ module that can be loaded in to
// the OPeNDAP Back-End Server (BES) and is able to handle remote requests.

// Copyright (c) 2002,2003 OPeNDAP, Inc.
// Author: Patrick West <pwest@ucar.edu>
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

// Authors:
//      pcw       Patrick West <pwest@ucar.edu>

#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <fstream>
#include <streambuf>

#include <BESSyntaxUserError.h>
#include "BESNotFoundError.h"
#include <BESInternalError.h>
#include <BESDebug.h>
#include <BESUtil.h>
#include <TheBESKeys.h>
#include <WhiteList.h>

#include "NgapContainer.h"
#include "NgapApi.h"
#include "NgapUtils.h"
#include "NgapNames.h"
#include "RemoteHttpResource.h"
#include "curl_utils.h"

#define prolog std::string("NgapContainer::").append(__func__).append("() - ")

using namespace std;
using namespace bes;

namespace ngap {

    /** @brief Creates an instances of NgapContainer with symbolic name and real
     * name, which is the remote request.
     *
     * The real_name is the remote request URL.
     *
     * @param sym_name symbolic name representing this remote container
     * @param real_name the remote request URL
     * @throws BESSyntaxUserError if the url does not validate
     * @see NgapUtils
     */
    NgapContainer::NgapContainer(const string &sym_name,
                                 const string &real_name, const string &type) :
            BESContainer(sym_name, real_name, type), d_dmrpp_resource(0) {

        NgapApi ngap_api;
        if (type.empty())
            set_container_type("ngap");

        string data_access_url = ngap_api.convert_ngap_resty_path_to_data_access_url(real_name);

        set_real_name(data_access_url);
        // Because we know the name is really a URL, then we know the "relative_name" is meaningless
        // So we set it to be the same as "name"
        set_relative_name(data_access_url);
    }

/**
 * TODO: I think this implementation of the copy constructor is incomplete/inadequate. Review and fix as needed.
 */
    NgapContainer::NgapContainer(const NgapContainer &copy_from) :
            BESContainer(copy_from), d_dmrpp_resource(copy_from.d_dmrpp_resource) {
        // we can not make a copy of this container once the request has
        // been made
        if (d_dmrpp_resource) {
            string err = (string) "The Container has already been accessed, "
                         + "can not create a copy of this container.";
            throw BESInternalError(err, __FILE__, __LINE__);
        }
    }

    void NgapContainer::_duplicate(NgapContainer &copy_to) {
        if (copy_to.d_dmrpp_resource) {
            string err = (string) "The Container has already been accessed, "
                         + "can not duplicate this resource.";
            throw BESInternalError(err, __FILE__, __LINE__);
        }
        copy_to.d_dmrpp_resource = d_dmrpp_resource;
        BESContainer::_duplicate(copy_to);
    }

    BESContainer *
    NgapContainer::ptr_duplicate() {
        NgapContainer *container = new NgapContainer;
        _duplicate(*container);
        return container;
    }

    NgapContainer::~NgapContainer() {
        if (d_dmrpp_resource) {
            release();
        }
    }

/** @brief access the remote target response by making the remote request
 *
 * @return full path to the remote request response data file
 * @throws BESError if there is a problem making the remote request
 */
    string NgapContainer::access() {

        BESDEBUG( MODULE, prolog << "BEGIN" << endl);

        // Since this the ngap we know that the real_name is a URL.
        string data_access_url  = get_real_name();
        string dmrpp_url  = data_access_url + ".dmrpp";

        BESDEBUG( MODULE, prolog << "data_access_url: " << data_access_url << endl);
        BESDEBUG( MODULE, prolog << "dmrpp_url: " << dmrpp_url << endl);

        string type = get_container_type();
        if (type == "ngap")
            type = "";

        if(!d_dmrpp_resource) {
            BESDEBUG( MODULE, prolog << "Building new RemoteResource." << endl );
            d_dmrpp_resource = new ngap::RemoteHttpResource(dmrpp_url);
            d_dmrpp_resource->retrieveResource();
        }
        BESDEBUG( MODULE, prolog << "Located remote resource." << endl );

        string cachedResource = d_dmrpp_resource->getCacheFileName();
        BESDEBUG( MODULE, prolog << "Using local cache file: " << cachedResource << endl );

        type = d_dmrpp_resource->getType();
        set_container_type(type);
        BESDEBUG( MODULE, prolog << "Type: " << type << endl );


        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#if 1
        // James: I have the questions.
        //
        // 1) In the try catch blocks that read and write I need to call close in the try, but also in the catch(...)?
        //    Or is JUST in the catch(...) good enuff.
        //
        // 2) In reality this is accessing a file under the control of a file locking cache do it should be rewritten
        //    to utilize file locking? It's hard to know because in this modulke (and others) the cache file name is.\
        //    passed directly into the bes dispatch machinery at the end of this method:
        //
        //    return cachedResource;
        //
        //    So maybe it's good, or maybe there's a bigger issue around access and locking?
        //

        string dmrpp;

        //  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
        // Read the dmr++ file into a string object
        std::ifstream cr_istrm(cachedResource);
        if(!cr_istrm.is_open()){
            string msg = "Could not open '" + cachedResource + "' to read cached response.";
            BESDEBUG(MODULE, prolog << msg << endl);
            throw BESInternalError(msg, __FILE__, __LINE__);
        }
        try {
            std::stringstream buffer;
            buffer << cr_istrm.rdbuf();
            dmrpp = buffer.str();
            cr_istrm.close();
        }
        catch(...){
            cr_istrm.close();
        }

        //  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
        // Replace all occurrences of the dmr++ href attr key.
        int startIndex=0;
        string dmrpp_href_key("DATA_ACCESS_URL");
        while ((startIndex = dmrpp.find(dmrpp_href_key)) != -1){
            dmrpp.erase(startIndex, dmrpp_href_key.length());
            dmrpp.insert(startIndex, data_access_url);
        }

        //  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
        // Replace the contents of the cached dmr++ file with the modified string.
        std::ofstream cr_ostrm(cachedResource);
        if(!cr_ostrm.is_open()){
            string msg = "Could not open '" + cachedResource + "' to write modified cached response.";
            BESDEBUG(MODULE, prolog << msg << endl);
            throw BESInternalError(msg, __FILE__, __LINE__);
        }
        try {
            cr_ostrm << dmrpp;
            cr_ostrm.close();
        }
        catch(...){
            cr_ostrm.close();
        }
#else


        FILE *crFile;
        stringstream df;

        unsigned buf_size=10000;
        char buf[buf_size];

        crFile = fopen(cachedResource.c_str() , "r");
        if (crFile == NULL){
            string msg = "Cannot open cached resource: " + cachedResource  ;
            BESDEBUG(MODULE, prolog << msg << endl);
            throw BESInternalError(msg, __FILE__, __LINE__);
        }
        while (fgets (buf , buf_size , crFile) != NULL ) { df << buf; }
        fclose (crFile);
        dmrpp = df.str();

        int startIndex=0;
        string dmrpp_href_key("DATA_ACCESS_URL");
        while ((startIndex = dmrpp.find(dmrpp_href_key)) != -1){
            dmrpp.erase(startIndex, dmrpp_href_key.length());
            dmrpp.insert(startIndex, data_access_url);
        }

        crFile = fopen(cachedResource.c_str(), "w");
        fputs(dmrpp.c_str(), crFile);
        fclose(crFile);

#endif


        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

        BESDEBUG( MODULE, prolog << "Done accessing " << get_real_name() << " returning cached file " << cachedResource << endl);
        BESDEBUG( MODULE, prolog << "Done accessing " << *this << endl);
        BESDEBUG( MODULE, prolog << "END" << endl);

        return cachedResource;    // this should return the file name from the NgapCache
    }



/** @brief release the resources
 *
 * Release the resource
 *
 * @return true if the resource is released successfully and false otherwise
 */
    bool NgapContainer::release() {
        if (d_dmrpp_resource) {
            BESDEBUG( MODULE, prolog << "Releasing RemoteResource" << endl);
            delete d_dmrpp_resource;
            d_dmrpp_resource = 0;
        }

        BESDEBUG( MODULE, prolog << "Done releasing Ngap response" << endl);
        return true;
    }

/** @brief dumps information about this object
 *
 * Displays the pointer value of this instance along with information about
 * this container.
 *
 * @param strm C++ i/o stream to dump the information to
 */
    void NgapContainer::dump(ostream &strm) const {
        strm << BESIndent::LMarg << "NgapContainer::dump - (" << (void *) this
             << ")" << endl;
        BESIndent::Indent();
        BESContainer::dump(strm);
        if (d_dmrpp_resource) {
            strm << BESIndent::LMarg << "RemoteResource.getCacheFileName(): " << d_dmrpp_resource->getCacheFileName()
                 << endl;
            strm << BESIndent::LMarg << "response headers: ";
            vector<string> *hdrs = d_dmrpp_resource->getResponseHeaders();
            if (hdrs) {
                strm << endl;
                BESIndent::Indent();
                vector<string>::const_iterator i = hdrs->begin();
                vector<string>::const_iterator e = hdrs->end();
                for (; i != e; i++) {
                    string hdr_line = (*i);
                    strm << BESIndent::LMarg << hdr_line << endl;
                }
                BESIndent::UnIndent();
            } else {
                strm << "none" << endl;
            }
        } else {
            strm << BESIndent::LMarg << "response not yet obtained" << endl;
        }
        BESIndent::UnIndent();
    }

}
