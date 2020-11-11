// -*- mode: c++; c-basic-offset:4 -*-

// This file is part of the BES

// Copyright (c) 2016 OPeNDAP, Inc.
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

#include <fcntl.h>

#include <unistd.h>
#include <time.h>

#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cerrno>
#include <sstream>
#include <iostream>
#include <fstream>
#include <GetOpt.h>

#include <curl/curl.h>


#include "D4Dimensions.h"

#include "BESInternalError.h"
#include "BESUtil.h"
#include "CurlUtils.h"
#include "TheBESKeys.h"
#include "BESLog.h"
#include "BESDebug.h"
#include "BESStopWatch.h"

#include "awsv4.h"
#include "HttpNames.h"
#include "EffectiveUrl.h"

#include "Chunk.h"
#include "CredentialsManager.h"
#include "AccessCredentials.h"
#include "CredentialsManager.h"
#include "CurlHandlePool.h"
#include "DmrppCommon.h"
#include "DmrppRequestHandler.h"
#include "DmrppByte.h"
#include "DmrppArray.h"
#include "DMRpp.h"
#include "DmrppTypeFactory.h"
#include "DmrppD4Group.h"

//#include <memory>
//#include <iterator>
//#include <algorithm>


bool Debug = false;
bool debug = false;
bool bes_debug = false;

using std::cerr;
using std::endl;
using std::string;

#define prolog std::string("retriever::").append(__func__).append("() - ")



/**
 *
 * @return
 */
string get_errno()
{
    char *s_err = strerror(errno);
    if (s_err)
        return s_err;
    else
        return "Unknown error.";
}



/**
 *
 * @param bes_config_file
 * @param bes_log_file
 * @param bes_debug_log_file
 * @param bes_debug_keys
 * @param http_netrc_file
 * @return
 */
dmrpp::DmrppRequestHandler *bes_setup(
        const string &bes_config_file,
        const string &bes_log_file,
        const string &bes_debug_log_file,
        const string &bes_debug_keys,
        const string &http_netrc_file
){
    TheBESKeys::ConfigFile = bes_config_file; // Set the config file for TheBESKeys
    TheBESKeys::TheKeys()->set_key("BES.LogName",bes_log_file); // Set the log file so it goes where we say.
    TheBESKeys::TheKeys()->set_key("AllowedHosts","^https?:\\/\\/.*$", false); // Set AllowedHosts to allow any URL
    TheBESKeys::TheKeys()->set_key("AllowedHosts","^file:\\/\\/\\/.*$", true); // Set AllowedHosts to allow any file

    if(bes_debug) BESDebug::SetUp(bes_debug_log_file+","+bes_debug_keys); // Enable BESDebug settings


    if(!http_netrc_file.empty()){
        TheBESKeys::TheKeys()->set_key(HTTP_NETRC_FILE_KEY,http_netrc_file, false); // Set the netrc file
    }

    // Initialize the dmr++ goodness.
    return new dmrpp::DmrppRequestHandler("Chaos");
}


#if 0
void curl_stuff(const string target_url, vector<string> request_headers){
    AccessCredentials *credentials = CredentialsManager::theCM()->get(target_url);
    if (credentials && credentials->is_s3_cred()) {
        if(debug) cerr << prolog << "Got AccessCredentials instance: " << endl << credentials->to_json() << endl);
        // If there are available credentials, and they are S3 credentials then we need to sign
        // the request
        const std::time_t request_time = std::time(0);

        const std::string auth_header =
                AWSV4::compute_awsv4_signature(
                        target_url,
                        request_time,
                        credentials->get(AccessCredentials::ID_KEY),
                        credentials->get(AccessCredentials::KEY_KEY),
                        credentials->get(AccessCredentials::REGION_KEY),
                        "s3");

        // passing nullptr for the first call allocates the curl_slist
        // The following code builds the slist that holds the headers. This slist is freed
        // once the URL is dereferenced in dmrpp_easy_handle::read_data(). jhrg 11/26/19
        request_headers = append_http_header(0, "Authorization:", auth_header);
        if (!handle->d_request_headers)
            throw BESInternalError(
                    string("CURL Error setting Authorization header: ").append(
                            curl::error_message(res, handle->d_errbuf)), __FILE__, __LINE__);

        // We pre-compute the sha256 hash of a null message body
        curl_slist *temp = append_http_header(handle->d_request_headers, "x-amz-content-sha256:",
                                              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        if (!temp)
            throw BESInternalError(
                    string("CURL Error setting x-amz-content-sha256: ").append(
                            curl::error_message(res, handle->d_errbuf)),
                    __FILE__, __LINE__);
        handle->d_request_headers = temp;

        temp = append_http_header(handle->d_request_headers, "x-amz-date:", AWSV4::ISO8601_date(request_time));
        if (!temp)
            throw BESInternalError(
                    string("CURL Error setting x-amz-date header: ").append(
                            curl::error_message(res, handle->d_errbuf)),
                    __FILE__, __LINE__);
        handle->d_request_headers = temp;

        // handle->d_request_headers = curl::add_auth_headers(handle->d_request_headers);

        res = curl_easy_setopt(handle->d_handle, CURLOPT_HTTPHEADER, handle->d_request_headers);
        curl::eval_curl_easy_setopt_result(res, prolog, "CURLOPT_HTTPHEADER", handle->d_errbuf, __FILE__, __LINE__);
}
#endif

/**
 *
 * @param url
 * @return
 */
size_t get_remote_size(string url){
    // TODO Use cURL to perform a HEAD on the URL and figure out how big the thing is.
    char error_buffer[CURL_ERROR_SIZE];
    std::vector<std::string> resp_hdrs;
    curl_slist *request_headers = NULL;

    request_headers = curl::add_auth_headers(request_headers);
    CURL *ceh = curl::init(url, request_headers, &resp_hdrs);
    curl::set_error_buffer(ceh, error_buffer);

    // In cURLville, CURLOPT_NOBODY means a HEAD request i.e. "Don't send the response body a.k.a. NoBody"
    CURLcode curl_status = curl_easy_setopt(ceh, CURLOPT_NOBODY, 1L);
    curl::eval_curl_easy_setopt_result(curl_status, prolog, "CURLOPT_NOBODY", error_buffer, __FILE__, __LINE__);

    if(Debug) cerr << prolog << "HEAD request is configured" << endl;


    curl::super_easy_perform(ceh);
    if (request_headers)
        curl_slist_free_all(request_headers);
    if (ceh)
        curl_easy_cleanup(ceh);

    bool done = false;
    size_t ret_val = 0;
    string content_length_hdr_key("content-length: ");
    for(size_t i=0; !done && i<resp_hdrs.size() ;i++){
        if(Debug) cerr << prolog << "HEADER["<<i<<"]: " << resp_hdrs[i] << endl;
        string lc_header = BESUtil::lowercase(resp_hdrs[i]);
        size_t index = lc_header.find(content_length_hdr_key);
        if(index==0){
            string value = lc_header.substr(content_length_hdr_key.size());
            ret_val = stol(value);
            done = true;
        }
    }


    if(!done)
        throw BESInternalError(prolog + "Failed to determine size of target resource: " + url, __FILE__, __LINE__);

    return ret_val;
}

/**
 *
 * @param target_url
 * @param output_file
 */
void simple_get(const string target_url, const string output_file_base) {

    string output_file = output_file_base + "_simple_get.out";
    vector<string> resp_hdrs;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int fd;
    if ((fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC , mode)) < 0) {
        throw BESInternalError(get_errno(), __FILE__, __LINE__);
    }
    {
        BESStopWatch sw;
        sw.start(prolog + "url: " + target_url);
        curl::http_get_and_write_resource(target_url, fd, &resp_hdrs); // Throws BESInternalError if there is a curl error.
    }
    close(fd);

    if(Debug){
        for(size_t i=0; i<resp_hdrs.size() ;i++){
            cerr << prolog << "ResponseHeader["<<i<<"]: " << resp_hdrs[i] << endl;
        }
    }
}


/**
 *
 * @param target_url
 * @param target_size
 * @param chunk_count
 * @param chunks
 */
void make_chunks(const string &target_url, const size_t &target_size, const size_t &chunk_count, vector<dmrpp::Chunk *> &chunks){
    if(debug) cerr << prolog << "BEGIN" << endl;
    size_t chunk_size = target_size/chunk_count;
    size_t chunk_start = 0;
    size_t chunk_index;
    for(chunk_index=0; chunk_index<chunk_count; chunk_index++){
        vector<unsigned int> position_in_array;
        position_in_array.push_back(chunk_index);
        if(debug) cerr << prolog << "chunks[" << chunk_index << "]  chunk_start: " << chunk_start << " chunk_size: " << chunk_size << endl;
        auto chunk = new dmrpp::Chunk(target_url, "LE", chunk_size, chunk_start,position_in_array);
        chunk_start += chunk_size;
        chunks.push_back(chunk);
    }
    if(target_size%chunk_size){
        // So there's a remainder and we should make a final chunk for it too.
        size_t last_chunk_size = target_size - chunk_start;
        if(debug) cerr << prolog << "Remainder chunk. chunk[" << chunks.size() << "] last_chunk_size: " << last_chunk_size << endl;
        if(debug) cerr << prolog << "Remainder chunk! target_size: "<<target_size<<"  index: "<< chunk_index << " last_chunk_start: " << chunk_start << " last_chunk_size: " << last_chunk_size << endl;
        if(last_chunk_size>0){
            vector<unsigned int> position_in_array;
            position_in_array.push_back(chunk_index);
            if(debug) cerr << prolog << "chunks["<<chunk_index<<"]  chunk_start: " << chunk_start << " chunk_size: " << last_chunk_size << endl;
            auto last_chunk = new dmrpp::Chunk(target_url, "LE", last_chunk_size, chunk_start,position_in_array);
            chunks.push_back(last_chunk);
        }
    }
    if(debug) cerr << prolog << "END chunks: " << chunks.size()  << endl;
}


/**
 *
 * @param target_url
 * @param target_size
 * @param chunk_count
 */
void serial_chunky_get(const string target_url, const size_t target_size, const unsigned long chunk_count, const string output_file_base){

    string output_file = output_file_base + "_serial_chunky_get.out";
    vector<dmrpp::Chunk *> chunks;
    make_chunks(target_url, target_size, chunk_count, chunks);

    std::ofstream fs;
    fs.open (output_file, std::fstream::in | std::fstream::out | std::ofstream::trunc | std::ofstream::binary);
    if(fs.fail())
        throw BESInternalError(prolog + "Failed to open file: "+output_file, __FILE__, __LINE__);

    for(size_t i=0; i<chunks.size(); i++) {
        stringstream ss;
        ss << prolog << "chunk={index: " << i << ", offset: " << chunks[i]->get_offset() << ", size: "
           << chunks[i]->get_size() << "}";

        {
            BESStopWatch sw;
            sw.start(ss.str());
            chunks[i]->read_chunk();
        }

        if(debug) cerr << ss.str() << " retrieval from: " << target_url << " completed, timing finished." <<  endl;
        fs.write(chunks[i]->get_rbuf(),chunks[i]->get_rbuf_size());
        if(debug) cerr << ss.str() << " has been written to: " << output_file << endl;
    }
    auto itr = chunks.begin();
    while(itr != chunks.end()){
        delete *itr;
        *itr=0;
        itr++;
    }

}


/**
 *
 * @param target_url
 * @param target_size
 * @param chunk_count
 */
void add_chunks(const string &target_url, const size_t &target_size, const size_t &chunk_count, dmrpp::DmrppArray *target_array){

    if(debug) cerr << prolog << "BEGIN" << endl;

    size_t chunk_size = target_size/chunk_count;
    if(chunk_size==0)
        throw BESInternalError(prolog + "Chunk size was zero.", __FILE__, __LINE__);
    stringstream chunk_dim_size;
    chunk_dim_size << chunk_size;
    target_array->parse_chunk_dimension_sizes(chunk_dim_size.str());

    size_t chunk_start = 0;
    size_t chunk_index;
    for(chunk_index=0; chunk_index<chunk_count; chunk_index++){
        vector<unsigned int> position_in_array;
        position_in_array.push_back(chunk_start);
        if(debug) cerr << prolog << "chunks[" << chunk_index << "]  chunk_start: " << chunk_start << " chunk_size: " << chunk_size  << " chunk_poa: " << position_in_array[0] << endl;
        target_array->add_chunk(target_url,"LE",chunk_size,chunk_start,position_in_array);
        chunk_start += chunk_size;
    }
    if(target_size%chunk_size){
        // So there's a remainder and we should make a final chunk for it too.
        size_t last_chunk_size = target_size - chunk_start;
        if(debug) cerr << prolog << "Remainder chunk! target_size: " << target_size << "  index: " << chunk_index << " last_chunk_start: " << chunk_start << " last_chunk_size: " << last_chunk_size << endl;
        if(last_chunk_size>0){
            vector<unsigned int> position_in_array;
            position_in_array.push_back(chunk_start);
            if(debug) cerr << prolog << "chunks[" << chunk_index << "]  chunk_start: " << chunk_start << " chunk_size: " << last_chunk_size <<  " chunk_poa: " << position_in_array[0] << endl;
            target_array->add_chunk(target_url,"LE",last_chunk_size,chunk_start,position_in_array);
        }
    }
    if(debug) cerr << prolog << "END" << endl;
}


/**
 *
 * @param target_url
 * @param target_size
 * @param chunk_count
 * @param output_file_base
 */
void array_get(const string &target_url, const size_t &target_size, const size_t &chunk_count, const string &output_file_base){

    if(debug) cerr << prolog << "BEGIN" << endl;
    string output_file = output_file_base + "_array_get.out";


    auto *tmplt = new dmrpp::DmrppByte("data");
    auto *target_array = new dmrpp::DmrppArray("data",tmplt);
    //auto *dim = new libdap::D4Dimension("data",target_size);
    target_array->append_dim(target_size);
    add_chunks(target_url, target_size, chunk_count, target_array);
    target_array->set_send_p(true); // Mark it to be sent so that it will be read.

    dmrpp::DmrppTypeFactory factory;
    dmrpp::DMRpp dmr(&factory);
    dmr.set_href(target_url);
    dmrpp::DmrppD4Group *root = dynamic_cast<dmrpp::DmrppD4Group *>(dmr.root());
    root->add_var(target_array);

    if(debug){
        cerr << prolog << "Built dataset: " << endl ;
        dmrpp::DmrppCommon::d_print_chunks=true;
        libdap::XMLWriter xmlWriter;
        dmr.print_dap4(xmlWriter);
        cerr << xmlWriter.get_doc() << endl;
    }

    {
        stringstream timer_msg;
        timer_msg << prolog << "DmrppD4Group.intern_data() for " << target_size << " bytes in " << chunk_count <<
        " chunks, parallel transfers are " << (dmrpp::DmrppRequestHandler::d_use_parallel_transfers?"enabled":"disabled");
        BESStopWatch sw;
        sw.start(timer_msg.str());
        // target_array->intern_data();
        root->set_in_selection(true);
        root->intern_data();
    }
    delete target_array;
    if(debug) cerr << prolog << "END" << endl;
}







/**
 *
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char *argv[])
{

    int result = 0;
    string bes_log_file;
    string bes_debug_log_file="cerr";
    string bes_debug_keys="bes,http,curl,dmrpp";
    string target_url = "https://www.opendap.org/pub/binary/hyrax-1.16/centos-7.x/bes-debuginfo-3.20.7-1.static.el7.x86_64.rpm";
    string output_file_base("retriever");
    string prefix;
    size_t number_o_chunks = 100;
    size_t max_target_size = 0;
    string http_netrc_file;
    bool parallel_reads = false;

    char *prefixCstr = getenv("prefix");
    if(prefixCstr){
        prefix = prefixCstr;
    }
    else {
        prefix = "/";
    }
    auto bes_config_file = BESUtil::assemblePath(prefix, "/etc/bes/bes.conf", true);


    GetOpt getopt(argc, argv, "n:C:c:o:u:l:S:dbDP");
    int option_char;
    while ((option_char = getopt()) != -1) {
        switch (option_char) {
            case 'D':
                Debug = true;
                debug = true;
                break;
            case 'd':
                debug = true;
                break;
            case 'b':
                bes_debug = true;
                break;
            case 'P':
                parallel_reads = true;
                break;
            case 'c':
                bes_config_file = getopt.optarg;
                break;
            case 'u':
                target_url = getopt.optarg;
                break;
            case 'l':
                bes_log_file = getopt.optarg;
                break;
            case 'n':
                http_netrc_file = getopt.optarg;
                break;
            case 'o':
                output_file_base = getopt.optarg;
                break;
            case 'C':
                number_o_chunks = atol(getopt.optarg);
                break;
            case 'S':
                max_target_size = atol(getopt.optarg);
                break;

            default:
                break;
        }
    }

    if(bes_log_file.empty()){
        bes_log_file = output_file_base + "_bes.log";
    }

    cerr  << prolog << "# -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --" << endl;
    cerr  << prolog << "debug: " << (debug?"true":"false") << endl;
    cerr  << prolog << "Debug: " << (Debug?"true":"false") << endl;
    cerr  << prolog << "bes_debug: " << (bes_debug?"true":"false") << endl;
    cerr  << prolog << "output_file_base: '" << output_file_base << "'" << endl;
    cerr  << prolog << "bes_config_file: '" << bes_config_file << "'"  << endl;
    cerr  << prolog << "bes_log_file: '" << bes_log_file << "'"  << endl;
    cerr  << prolog << "bes_debug_log_file: '" << bes_debug_log_file << "'"  << endl;
    cerr  << prolog << "bes_debug_keys: '" << bes_debug_keys << "'" << endl;
    cerr  << prolog << "http_netrc_file: '" << http_netrc_file << "'" << endl;
    cerr  << prolog << "target_url: '" << target_url << "'" << endl;
    cerr  << prolog << "number_o_chunks: '" << number_o_chunks << "'" << endl;
    cerr  << prolog << "parallel_reads: '" << (parallel_reads?"true":"false") << "'" << endl;
    cerr  << prolog << " -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --" << endl;


    try {
        dmrpp::DmrppRequestHandler *dmrppRH = bes_setup(bes_config_file, bes_log_file, bes_debug_log_file,
                                                        bes_debug_keys, http_netrc_file);
        dmrpp::DmrppRequestHandler::d_use_parallel_transfers=parallel_reads;

        string effectiveUrl = curl::retrieve_effective_url(target_url)->str();
        if(debug) cerr << prolog << "curl::retrieve_effective_url() returned:  " << effectiveUrl << endl;

        size_t target_size = get_remote_size(effectiveUrl);
        if(target_size < max_target_size || max_target_size==0){
            max_target_size = target_size;
        }
        if(debug) cerr << prolog << "Remote resource is " << target_size << " bytes.  max_target_size: " << max_target_size << endl;

#if 0 // these work but are parked a.t.m.
        simple_get(target_url, output_file_base);
        serial_chunky_get( target_url,  max_target_size, number_o_chunks, output_file_base);
#endif
        array_get(effectiveUrl, max_target_size, number_o_chunks, output_file_base);

        delete dmrppRH;
    }
    catch(BESError e){
        cerr  << prolog << "Caught BESError. Message: " << e.get_message() << "  " << e.get_file() << ":"<< e.get_line() << endl;
        result = 1;
    }
    catch(...){
        cerr  << prolog << "Caught Unknown Exception." << endl;
        result =  2;
    }

    return result;
}
