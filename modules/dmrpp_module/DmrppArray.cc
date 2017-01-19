// -*- mode: c++; c-basic-offset:4 -*-

// This file is part of the BES

// Copyright (c) 2016 OPeNDAP, Inc.
// Author: James Gallagher <jgallagher@opendap.org>
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

#include <string>
#include <sstream>
#include <iomanip>

#include <cstring>
#include <stack>

#include <BESError.h>
#include <BESDebug.h>

#include "DmrppArray.h"
#include "DmrppUtil.h"
#include "Odometer.h"

using namespace dmrpp;
using namespace libdap;
using namespace std;

namespace dmrpp {

/**
 * @brief Write an int vector to a string.
 * @note Only used by BESDEBUG calls
 * @param v
 * @return The string
 */
static string vec2str(vector<unsigned int> v)
{
    ostringstream oss;
    oss << "(";
    for (unsigned long long i = 0; i < v.size(); i++) {
        oss << (i ? "," : "") << v[i];
    }
    oss << ")";
    return oss.str();
}

void DmrppArray::_duplicate(const DmrppArray &)
{
}

DmrppArray::DmrppArray(const string &n, BaseType *v) :
        Array(n, v, true /*is dap4*/), DmrppCommon()
{
}

DmrppArray::DmrppArray(const string &n, const string &d, BaseType *v) :
        Array(n, d, v, true), DmrppCommon()
{
}

BaseType *
DmrppArray::ptr_duplicate()
{
    return new DmrppArray(*this);
}

DmrppArray::DmrppArray(const DmrppArray &rhs) :
        Array(rhs), DmrppCommon(rhs)
{
    _duplicate(rhs);
}

DmrppArray &
DmrppArray::operator=(const DmrppArray &rhs)
{
    if (this == &rhs) return *this;

    dynamic_cast<Array &>(*this) = rhs; // run Constructor=

    _duplicate(rhs);
    DmrppCommon::_duplicate(rhs);

    return *this;
}

/**
 * @brief Is this Array subset?
 * @return True if the array has a projection expression, false otherwise
 */
bool DmrppArray::is_projected()
{
    for (Dim_iter p = dim_begin(), e = dim_end(); p != e; ++p)
        if (dimension_size(p, true) != dimension_size(p, false)) return true;

    return false;
}

/**
 * @brief Compute the index of the address_in_target for an an array of target_shape.
 * Since we store multidimensional arrays as a single one dimensional array
 * internally we need to be able to locate a particular address in the one dimensional
 * storage utilizing an n-tuple (where n is the dimension of the array). The get_index
 * function does this by computing the location based on the n-tuple address_in_target
 * and the shape of the array, passed in as target_shape.
 */
unsigned long long get_index(vector<unsigned int> address_in_target, const vector<unsigned int> target_shape)
{
    if (address_in_target.size() != target_shape.size()) {
        ostringstream oss;
        oss << "The target_shape  (size: " << target_shape.size() << ")" << " and the address_in_target (size: "
                << address_in_target.size() << ")" << " have different dimensionality.";
        throw BESError(oss.str(), BES_INTERNAL_ERROR, __FILE__, __LINE__);
    }
    unsigned long long digit_multiplier = 1;
    unsigned long long subject_index = 0;
    for (int i = target_shape.size() - 1; i >= 0; i--) {
        if (address_in_target[i] > target_shape[i]) {
            ostringstream oss;
            oss << "The address_in_target[" << i << "]: " << address_in_target[i] << " is larger than target_shape["
                    << i << "]: " << target_shape[i] << " This will make the bad things happen.";
            throw BESError(oss.str(), BES_INTERNAL_ERROR, __FILE__, __LINE__);
        }
        subject_index += address_in_target[i] * digit_multiplier;
        digit_multiplier *= target_shape[i];
    }
    return subject_index;
}

vector<unsigned int> DmrppArray::get_shape(bool constrained)
{
    vector<unsigned int> array_shape;
    for (Dim_iter dim = dim_begin(); dim != dim_end(); dim++) {
        array_shape.push_back(dimension_size(dim, constrained));
    }
    return array_shape;
}

DmrppArray::dimension DmrppArray::get_dimension(unsigned int dim_num)
{
    Dim_iter dimIter = dim_begin();
    unsigned int dim_index = 0;

    while (dimIter != dim_end()) {
        if (dim_num == dim_index) return *dimIter;
        dimIter++;
        dim_index++;
    }
    ostringstream oss;
    oss << "DmrppArray::get_dimension() -" << " The array " << name() << " does not have " << dim_num << " dimensions!";
    throw BESError(oss.str(), BES_INTERNAL_ERROR, __FILE__, __LINE__);
}

/**
 * @brief This recursive private method collects values from the rbuf and copies
 * them into buf. It supports stop, stride, and start and while correct is not
 * efficient.
 */
void DmrppArray::insert_constrained_no_chunk(Dim_iter dimIter, unsigned long *target_index,
        vector<unsigned int> &subsetAddress, const vector<unsigned int> &array_shape, H4ByteStream *h4bytestream)
{
    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ << "() - subsetAddress.size(): " << subsetAddress.size() << endl);

    unsigned int bytesPerElt = prototype()->width();
    char *sourceBuf = h4bytestream->get_rbuf();
    char *targetBuf = get_buf();

    unsigned int start = this->dimension_start(dimIter);
    unsigned int stop = this->dimension_stop(dimIter, true);
    unsigned int stride = this->dimension_stride(dimIter, true);
    BESDEBUG("dmrpp",
            "DmrppArray::"<< __func__ << "() - start: " << start << " stride: " << stride << " stop: " << stop << endl);

    dimIter++;

    // This is the end case for the recursion.
    // TODO stride == 1 belongs inside this or else rewrite this as if else if else
    // see below.
    if (dimIter == dim_end() && stride == 1) {
        BESDEBUG("dmrpp",
                "DmrppArray::"<< __func__ << "() - stride is 1, copying from all values from start to stop." << endl);

        subsetAddress.push_back(start);
        unsigned int start_index = get_index(subsetAddress, array_shape);
        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ << "() - start_index: " << start_index << endl);
        subsetAddress.pop_back();

        subsetAddress.push_back(stop);
        unsigned int stop_index = get_index(subsetAddress, array_shape);
        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ << "() - stop_index: " << start_index << endl);
        subsetAddress.pop_back();

        // Copy data block from start_index to stop_index
        // FIXME Replace this loop with a call to std::memcpy()
        for (unsigned int sourceIndex = start_index; sourceIndex <= stop_index; sourceIndex++, target_index++) {
            unsigned long target_byte = *target_index * bytesPerElt;
            unsigned long source_byte = sourceIndex * bytesPerElt;
            // Copy a single value.
            for (unsigned int i = 0; i < bytesPerElt; i++) {
                targetBuf[target_byte++] = sourceBuf[source_byte++];
            }
            (*target_index)++;
        }
    }
    else {
        for (unsigned int myDimIndex = start; myDimIndex <= stop; myDimIndex += stride) {
            // Is it the last dimension?
            if (dimIter != dim_end()) {
                // Nope!
                // then we recurse to the last dimension to read stuff
                subsetAddress.push_back(myDimIndex);
                insert_constrained_no_chunk(dimIter, target_index, subsetAddress, array_shape, h4bytestream);
                subsetAddress.pop_back();
            }
            else {
                // We are at the last (inner most) dimension.
                // So it's time to copy values.
                subsetAddress.push_back(myDimIndex);
                unsigned int sourceIndex = get_index(subsetAddress, array_shape);
                BESDEBUG("dmrpp",
                        "DmrppArray::"<< __func__ << "() - " "Copying source value at sourceIndex: " << sourceIndex << endl);
                subsetAddress.pop_back();
                // Copy a single value.
                unsigned long target_byte = *target_index * bytesPerElt;
                unsigned long source_byte = sourceIndex * bytesPerElt;

                // FIXME Replace this loop with a call to std::memcpy()
                for (unsigned int i = 0; i < bytesPerElt; i++) {
                    targetBuf[target_byte++] = sourceBuf[source_byte++];
                }
                (*target_index)++;
            }
        }
    }
}

unsigned long long DmrppArray::get_size(bool constrained)
{
    // number of array elements in the constrained array
    unsigned long long constrained_size = 1;
    for (Dim_iter dim = dim_begin(); dim != dim_end(); dim++) {
        constrained_size *= dimension_size(dim, constrained);
    }
    return constrained_size;
}

bool DmrppArray::read_no_chunks()
{
    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ << "() for " << name() << " BEGIN" << endl);

    vector<H4ByteStream> *chunk_refs = get_chunk_vec();
    if (chunk_refs->size() == 0) {
        ostringstream oss;
        oss << "DmrppArray::" << __func__ << "() - Unable to obtain a ByteStream object for array " << name()
                << " Without a ByteStream we cannot read! " << endl;
        throw BESError(oss.str(), BES_INTERNAL_ERROR, __FILE__, __LINE__);
    }

    // For now we only handle the one chunk case.
    H4ByteStream h4_byte_stream = (*chunk_refs)[0];
    h4_byte_stream.read(); // Use the default vlaues for deflate (false) and chunk size (0)

    if (!is_projected()) {      // if there is no projection constraint
        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - No projection, copying all values into array. " << endl);
        val2buf(h4_byte_stream.get_rbuf());    // yes, it's not type-safe
    }
    else {
        vector<unsigned int> array_shape = get_shape(false);
        unsigned long long constrained_size = get_size(true);

        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - constrained_size:  " << constrained_size << endl);

        reserve_value_capacity(constrained_size);
        unsigned long target_index = 0;
        vector<unsigned int> subset;
        insert_constrained_no_chunk(dim_begin(), &target_index, subset, array_shape, &h4_byte_stream);
    }

    set_read_p(true);

    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ << "() for " << name() << " END"<< endl);

    return true;
}

bool DmrppArray::read_chunks()
{
    BESDEBUG("dmrpp", __FUNCTION__ << " for variable '" << name() << "' - BEGIN" << endl);

    // FIXME Remove if (read_p()) return true;

    vector<H4ByteStream> *chunk_refs = get_chunk_vec();
    if (chunk_refs->size() == 0) {
        ostringstream oss;
        oss << "DmrppArray::" << __func__ << "() - Unable to obtain a byteStream object for array " << name()
                << " Without a byteStream we cannot read! " << endl;
        throw BESError(oss.str(), BES_INTERNAL_ERROR, __FILE__, __LINE__);
    }
    // Allocate target memory.
    // Fix me - I think this needs to be the constrained size!
    reserve_value_capacity(length());
    vector<unsigned int> array_shape = get_shape(false);

    BESDEBUG("dmrpp",
            "DmrppArray::"<< __func__ <<"() - dimensions(): " << dimensions(false) << " array_shape.size(): " << array_shape.size() << endl);

    if (this->dimensions(false) != array_shape.size()) {
        ostringstream oss;
        oss << "DmrppArray::" << __func__ << "() - array_shape does not match the number of array dimensions! " << endl;
        throw BESError(oss.str(), BES_INTERNAL_ERROR, __FILE__, __LINE__);
    }

    BESDEBUG("dmrpp",
            "DmrppArray::"<< __func__ << "() - "<< dimensions() << "D Array. Processing " << chunk_refs->size() << " chunks" << endl);

    switch (dimensions()) {
#if 0
    //########################### OneD Arrays ###############################
    case 1: {
        for(unsigned long i=0; i<chunk_refs->size(); i++) {
            H4ByteStream h4bs = (*chunk_refs)[i];
            BESDEBUG("dmrpp", "----------------------------------------------------------------------------" << endl);
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Processing chunk[" << i << "]: BEGIN" << endl);
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - " << h4bs.to_string() << endl);

            if (!is_projected()) {  // is there a projection constraint?
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - ################## There is no constraint. Getting entire chunk." << endl);
                // Apparently not, so we send it all!
                // read the data
                h4bs.read();
                // Get the source (read) and write (target) buffers.
                char * source_buffer = h4bs.get_rbuf();
                char * target_buffer = get_buf();
                // Compute insertion point for this chunk's inner (only) row data.
                vector<unsigned int> start_position = h4bs.get_position_in_array();
                unsigned long long start_char_index = start_position[0] * prototype()->width();
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - start_char_index: " << start_char_index << " start_position: " << start_position[0] << endl);
                // if there is no projection constraint then just copy those bytes.
                memcpy(target_buffer+start_char_index, source_buffer, h4bs.get_rbuf_size());
            }
            else {

                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Found constraint expression." << endl);

                // There is a projection constraint.
                // Recover constraint information for this (the first and only) dim.
                Dim_iter this_dim = dim_begin();
                unsigned int ce_start = dimension_start(this_dim,true);
                unsigned int ce_stride = dimension_stride(this_dim,true);
                unsigned int ce_stop = dimension_stop(this_dim,true);
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - ce_start:  " << ce_start << endl);
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - ce_stride: " << ce_stride << endl);
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - ce_stop:   " << ce_stop << endl);

                // Gather chunk information
                unsigned int chunk_inner_row_start = h4bs.get_position_in_array()[0];
                unsigned int chunk_inner_row_size = get_chunk_dimension_sizes()[0];
                unsigned int chunk_inner_row_end = chunk_inner_row_start + chunk_inner_row_size;
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_inner_row_start: " << chunk_inner_row_start << endl);
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_inner_row_size:  " << chunk_inner_row_size << endl);
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_inner_row_end:   " << chunk_inner_row_end << endl);

                // Do we even want this chunk?
                if( ce_start > chunk_inner_row_end || ce_stop < chunk_inner_row_start) {
                    // No, no we don't. Skip this.
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Chunk not accessed by CE. SKIPPING." << endl);
                }
                else {
                    // We want this chunk.
                    // Read the chunk.
                    h4bs.read();

                    unsigned long long chunk_start_element, chunk_end_element;

                    // In this next bit we determine the first element in this
                    // chunk that matches the subset expression.
                    int first_element_offset = 0;
                    if(ce_start < chunk_inner_row_start) {
                        // This case means that we must find the first element matching
                        // a stride that started at ce_start, somewhere up the line from
                        // this chunk. So, we subtract the ce_start from the chunk start to align
                        // the values for modulo arithmetic on the stride value.
                        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_inner_row_start: " << chunk_inner_row_start << endl);

                        if(ce_stride!=1)
                        first_element_offset = ce_stride - (chunk_inner_row_start - ce_start) % ce_stride;
                        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - first_element_offset: " << first_element_offset << endl);
                    }
                    else {
                        first_element_offset = ce_start - chunk_inner_row_start;
                        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - ce_start is in this chunk. first_element_offset: " << first_element_offset << endl);
                    }

                    chunk_start_element = (chunk_inner_row_start + first_element_offset);
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_start_element: " << chunk_start_element << endl);

                    // Now we figure out the correct last element, based on the subset expression
                    chunk_end_element = chunk_inner_row_end;
                    if(ce_stop<chunk_inner_row_end) {
                        chunk_end_element = ce_stop;
                        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - ce_stop is in this chunk. " << endl);
                    }
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_end_element: " << chunk_end_element << endl);

                    // Compute the read() address and length for this chunk's inner (only) row data.
                    unsigned long long element_count = chunk_end_element - chunk_start_element + 1;
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - element_count: " << element_count << endl);
                    unsigned long long length = element_count * prototype()->width();
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - length: " << length << endl);

                    if(ce_stride==1) {
                        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - ################## Copy contiguous block mode." << endl);
                        // Since the stride is 1 we are getting everything between start
                        // and stop, so memcopy!
                        // Get the source (read) and write (target) buffers.
                        char * source_buffer = h4bs.get_rbuf();
                        char * target_buffer = get_buf();
                        unsigned int target_char_start_index = ((chunk_start_element - ce_start) / ce_stride ) * prototype()->width();
                        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - target_char_start_index: " << target_char_start_index << endl);
                        unsigned long long source_char_start_index = first_element_offset * prototype()->width();
                        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - source_char_start_index: " << source_char_start_index << endl);
                        // if there is no projection constraint then just copy those bytes.
                        memcpy(target_buffer+target_char_start_index, source_buffer + source_char_start_index,length);
                    }
                    else {

                        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - ##################  Copy individual elements mode." << endl);

                        // Get the source (read) and write (target) buffers.
                        char * source_buffer = h4bs.get_rbuf();
                        char * target_buffer = get_buf();
                        // Since stride is not equal to 1 we have to copy individual values
                        for(unsigned int element_index=chunk_start_element; element_index<=chunk_end_element;element_index+=ce_stride) {
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - element_index: " << element_index << endl);
                            unsigned int target_char_start_index = ((element_index - ce_start) / ce_stride ) * prototype()->width();
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - target_char_start_index: " << target_char_start_index << endl);
                            unsigned int chunk_char_start_index = (element_index - chunk_inner_row_start) * prototype()->width();
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_char_start_index: " << chunk_char_start_index << endl);
                            memcpy(target_buffer+target_char_start_index, source_buffer + chunk_char_start_index,prototype()->width());
                        }
                    }
                }
            }
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Processing chunk[" << i << "]:  END" << endl);
        }
        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - END ##################  END" << endl);

    }break;
    //########################### TwoD Arrays ###############################
    case 2: {
        if(!is_projected()) {
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ << "() - 2D Array. No CE Found. Reading " << chunk_refs->size() << " chunks" << endl);
            char * target_buffer = get_buf();
            vector<unsigned int> chunk_shape = get_chunk_dimension_sizes();
            for(unsigned long i=0; i<chunk_refs->size(); i++) {
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - READING chunk[" << i << "]: " << (*chunk_refs)[i].to_string() << endl);
                H4ByteStream h4bs = (*chunk_refs)[i];
                h4bs.read();
                char * source_buffer = h4bs.get_rbuf();
                vector<unsigned int> chunk_origin = h4bs.get_position_in_array();
                vector<unsigned int> chunk_row_address = chunk_origin;
                unsigned long long target_element_index = get_index(chunk_origin,array_shape);
                unsigned long long target_char_index = target_element_index * prototype()->width();
                unsigned long long source_element_index = 0;
                unsigned long long source_char_index = source_element_index * prototype()->width();
                unsigned long long chunk_inner_dim_bytes = chunk_shape[1] * prototype()->width();
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Packing Array From Chunks: "
                        << " chunk_inner_dim_bytes: " << chunk_inner_dim_bytes << endl);

                for(unsigned int i=0; i<chunk_shape[0];i++) {
                    BESDEBUG("dmrpp", "DmrppArray::" << __func__ << "() - "
                            "target_char_index: " << target_char_index <<
                            " source_char_index: " << source_char_index << endl);
                    memcpy(target_buffer+target_char_index, source_buffer+source_char_index, chunk_inner_dim_bytes);
                    chunk_row_address[0] += 1;
                    target_element_index = get_index(chunk_row_address,array_shape);
                    target_char_index = target_element_index * prototype()->width();
                    source_element_index += chunk_shape[1];
                    source_char_index = source_element_index * prototype()->width();
                }
            }
        }
        else {
            char * target_buffer = get_buf();

            vector<unsigned int> array_shape = get_shape(false);
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - array_shape: " << vec2str(array_shape) << endl);

            vector<unsigned int> constrained_array_shape = get_shape(true);
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - constrained_array_shape: " << vec2str(constrained_array_shape) << endl);

            vector<unsigned int> chunk_shape = get_chunk_dimension_sizes();
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_shape: " << vec2str(chunk_shape) << endl);

            // There is a projection constraint.
            // Recover constraint information for the first/outer dim.
            Dim_iter outer_dim_itr = dim_begin();
            Dim_iter inner_dim_itr = dim_begin();
            inner_dim_itr++;
            unsigned int outer_dim = 0;
            unsigned int inner_dim = 1;
            unsigned int outer_start = dimension_start(outer_dim_itr,true);
            unsigned int outer_stride = dimension_stride(outer_dim_itr,true);
            unsigned int outer_stop = dimension_stop(outer_dim_itr,true);
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - outer_start:  " << outer_start << endl);
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - outer_stride: " << outer_stride << endl);
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - outer_stop:   " << outer_stop << endl);

            // There is a projection constraint.
            // Recover constraint information for the last/inner dim.
            unsigned int inner_start = dimension_start(inner_dim_itr,true);
            unsigned int inner_stride = dimension_stride(inner_dim_itr,true);
            unsigned int inner_stop = dimension_stop(inner_dim_itr,true);
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_start:  " << inner_start << endl);
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_stride: " << inner_stride << endl);
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_stop:   " << inner_stop << endl);

            for(unsigned long i=0; i<chunk_refs->size(); i++) {
                BESDEBUG("dmrpp", "- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - " << endl);
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Processing chunk[" << i << "]: " << endl);
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ << (*chunk_refs)[i].to_string() << endl);
                H4ByteStream h4bs = (*chunk_refs)[i];
                vector<unsigned int> chunk_origin = h4bs.get_position_in_array();
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_origin:   " << vec2str(chunk_origin) << endl);

                // What's the first row/element that we are going to access for the outer dimension of the chunk?
                int outer_first_element_offset = 0;
                if(outer_start < chunk_origin[outer_dim]) {
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - outer_start: " << outer_start << endl);
                    if(outer_stride!=1) {
                        outer_first_element_offset = (chunk_origin[outer_dim] - outer_start) % outer_stride;
                        if(outer_first_element_offset!=0)
                        outer_first_element_offset = outer_stride - outer_first_element_offset;
                    }
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - outer_first_element_offset: " << outer_first_element_offset << endl);
                }
                else {
                    outer_first_element_offset = outer_start - chunk_origin[outer_dim];
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - outer_start is in this chunk. outer_first_element_offset: " << outer_first_element_offset << endl);
                }
                unsigned long long outer_start_element = chunk_origin[outer_dim] + outer_first_element_offset;
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - outer_start_element: " << outer_start_element << endl);

                // Now we figure out the correct last element, based on the subset expression
                unsigned long long outer_end_element = chunk_origin[outer_dim] + chunk_shape[outer_dim] - 1;
                if(outer_stop<outer_end_element) {
                    outer_end_element = outer_stop;
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - outer_stop is in this chunk. " << endl);
                }
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - outer_end_element: " << outer_end_element << endl);

                // What's the first row/element that we are going to access for the inner dimension of the chunk?
                int inner_first_element_offset = 0;
                if(inner_start < chunk_origin[inner_dim]) {
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_start: " << inner_start << endl);
                    if(inner_stride!=1) {
                        inner_first_element_offset = (chunk_origin[inner_dim] - inner_start) % inner_stride;
                        if(inner_first_element_offset!=0)
                        inner_first_element_offset = inner_stride - inner_first_element_offset;
                    }
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_first_element_offset: " << inner_first_element_offset << endl);
                }
                else {
                    inner_first_element_offset = inner_start - chunk_origin[inner_dim];
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_start is in this chunk. inner_first_element_offset: " << inner_first_element_offset << endl);
                }
                unsigned long long inner_start_element = chunk_origin[inner_dim] + inner_first_element_offset;
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_start_element: " << inner_start_element << endl);

                // Now we figure out the correct last element, based on the subset expression
                unsigned long long inner_end_element = chunk_origin[inner_dim] + chunk_shape[inner_dim] - 1;
                if(inner_stop<inner_end_element) {
                    inner_end_element = inner_stop;
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_stop is in this chunk. " << endl);
                }
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_end_element: " << inner_end_element << endl);

                // Do we even want this chunk?
                if( outer_start > (chunk_origin[outer_dim]+chunk_shape[outer_dim]) || outer_stop < chunk_origin[outer_dim]) {
                    // No. No, we do not. Skip this.
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Chunk not accessed by CE (outer_dim). SKIPPING." << endl);
                }
                else if( inner_start > (chunk_origin[inner_dim]+chunk_shape[inner_dim])
                        || inner_stop < chunk_origin[inner_dim]) {
                    // No. No, we do not. Skip this.
                    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Chunk not accessed by CE (inner_dim). SKIPPING." << endl);
                }
                else {
                    // Read and Process chunk

                    // Now. Now we are going to read this thing.
                    h4bs.read();
                    char * source_buffer = h4bs.get_rbuf();

                    vector<unsigned int> chunk_row_address = chunk_origin;
                    unsigned long long outer_chunk_end = outer_end_element - chunk_origin[outer_dim];
                    unsigned long long outer_chunk_start = outer_start_element - chunk_origin[outer_dim];
                    // unsigned int outer_result_position = 0;
                    // unsigned int inner_result_position = 0;
                    vector<unsigned int> target_address;
                    target_address.push_back(0);
                    target_address.push_back(0);
                    // unsigned long long chunk_inner_dim_bytes =  constrained_array_shape[inner_dim] * prototype()->width();
                    for(unsigned int odim_index=outer_chunk_start; odim_index<=outer_chunk_end;odim_index+=outer_stride) {
                        BESDEBUG("dmrpp", "DmrppArray::" << __func__ << "() ----------------------------------" << endl);
                        BESDEBUG("dmrpp", "DmrppArray::" << __func__ << "() --- "
                                "odim_index: " << odim_index << endl);
                        chunk_row_address[outer_dim] = chunk_origin[outer_dim] + odim_index;
                        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_row_address: " << vec2str(chunk_row_address) << endl);

                        target_address[outer_dim] = (chunk_row_address[outer_dim] - outer_start)/outer_stride;

                        if(inner_stride==1) {
                            //#############################################################################
                            // 2D - inner_stride == 1

                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - The InnerMostStride is 1." << endl);

                            // Compute how much we are going to copy
                            unsigned long long chunk_constrained_inner_dim_elements = inner_end_element - inner_start_element + 1;
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_constrained_inner_dim_elements: " << chunk_constrained_inner_dim_elements << endl);

                            unsigned long long chunk_constrained_inner_dim_bytes = chunk_constrained_inner_dim_elements * prototype()->width();
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_constrained_inner_dim_bytes: " << chunk_constrained_inner_dim_bytes << endl);

                            // Compute where we need to put it.
                            target_address[inner_dim] = (inner_start_element - inner_start ) / inner_stride;
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - target_address: " << vec2str(target_address) << endl);

                            unsigned int target_start_element_index = get_index(target_address,constrained_array_shape);
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - target_start_element_index: " << target_start_element_index << endl);

                            unsigned int target_char_start_index = target_start_element_index* prototype()->width();
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - target_char_start_index: " << target_char_start_index << endl);

                            // Compute where we are going to read it from
                            vector<unsigned int> chunk_source_address;
                            chunk_source_address.push_back(odim_index);
                            chunk_source_address.push_back(inner_first_element_offset);
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_source_address: " << vec2str(chunk_source_address) << endl);

                            unsigned int chunk_start_element_index = get_index(chunk_source_address,chunk_shape);
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_start_element_index: " << chunk_start_element_index << endl);

                            unsigned int chunk_char_start_index = chunk_start_element_index * prototype()->width();
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_char_start_index: " << chunk_char_start_index << endl);

                            // Copy the bytes
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Using memcpy to transfer " << chunk_constrained_inner_dim_bytes << " bytes." << endl);
                            memcpy(target_buffer+target_char_start_index, source_buffer+chunk_char_start_index, chunk_constrained_inner_dim_bytes);
                        }
                        else {
                            //#############################################################################
                            // 2D -  inner_stride != 1
                            unsigned long long vals_in_chunk = 1 + (inner_end_element-inner_start_element)/inner_stride;
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - InnerMostStride is equal to " << inner_stride
                                    << ". Copying " << vals_in_chunk << " individual values." << endl);

                            unsigned long long inner_chunk_start = inner_start_element - chunk_origin[inner_dim];
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_chunk_start: " << inner_chunk_start << endl);

                            unsigned long long inner_chunk_end = inner_end_element - chunk_origin[inner_dim];
                            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - inner_chunk_end: " << inner_chunk_end << endl);

                            vector<unsigned int> chunk_source_address = chunk_origin;
                            chunk_source_address[outer_dim] = odim_index;

                            for(unsigned int idim_index=inner_chunk_start; idim_index<=inner_chunk_end; idim_index+=inner_stride) {
                                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() --------- idim_index: " << idim_index << endl);

                                // Compute where we need to put it.
                                target_address[inner_dim] = ( idim_index + chunk_origin[inner_dim] - inner_start ) / inner_stride;
                                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - target_address: " << vec2str(target_address) << endl);

                                unsigned int target_start_element_index = get_index(target_address,constrained_array_shape);
                                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - target_start_element_index: " << target_start_element_index << endl);

                                unsigned int target_char_start_index = target_start_element_index* prototype()->width();
                                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - target_char_start_index: " << target_char_start_index << endl);

                                // Compute where we are going to read it from

                                chunk_row_address[inner_dim] = chunk_origin[inner_dim] + idim_index;
                                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_row_address: " << vec2str(chunk_row_address) << endl);

                                chunk_source_address[inner_dim] = idim_index;
                                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_source_address: " << vec2str(chunk_source_address) << endl);

                                unsigned int chunk_start_element_index = get_index(chunk_source_address,chunk_shape);
                                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_start_element_index: " << chunk_start_element_index << endl);

                                unsigned int chunk_char_start_index = chunk_start_element_index * prototype()->width();
                                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_char_start_index: " << chunk_char_start_index << endl);

                                // Copy the bytes
                                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Using memcpy to transfer " << prototype()->width() << " bytes." << endl);
                                memcpy(target_buffer+target_char_start_index, source_buffer+chunk_char_start_index, prototype()->width());
                            }
                        }
                    }
                }
            }
        }
    }break;
#endif
#if 0
    //########################### ThreeD Arrays ###############################
    case 3: {
        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ << "() - 3D Array. Reading " << chunk_refs->size() << " chunks" << endl);

        char * target_buffer = get_buf();
        vector<unsigned int> chunk_shape = get_chunk_dimension_sizes();
        unsigned long long chunk_inner_dim_bytes = chunk_shape[2] * prototype()->width();

        for(unsigned long i=0; i<chunk_refs->size(); i++) {
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - READING chunk[" << i << "]: " << (*chunk_refs)[i].to_string() << endl);
            H4ByteStream h4bs = (*chunk_refs)[i];
            h4bs.read();

            vector<unsigned int> chunk_origin = h4bs.get_position_in_array();

            char * source_buffer = h4bs.get_rbuf();
            unsigned long long source_element_index = 0;
            unsigned long long source_char_index = 0;

            unsigned long long target_element_index = get_index(chunk_origin,array_shape);
            unsigned long long target_char_index = target_element_index * prototype()->width();

            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Packing Array From Chunks: "
                    << " chunk_inner_dim_bytes: " << chunk_inner_dim_bytes << endl);

            unsigned int K_DIMENSION = 0; // Outermost dim
            unsigned int J_DIMENSION = 1;
            unsigned int I_DIMENSION = 2;// inner most dim (fastest varying)

            vector<unsigned int> chunk_row_insertion_point_address = chunk_origin;
            for(unsigned int k=0; k<chunk_shape[K_DIMENSION]; k++) {
                chunk_row_insertion_point_address[K_DIMENSION] = chunk_origin[K_DIMENSION] + k;
                BESDEBUG("dmrpp", "DmrppArray::" << __func__ << "() - "
                        << "k: " << k << "  chunk_row_insertion_point_address: "
                        << vec2str(chunk_row_insertion_point_address) << endl);
                for(unsigned int j=0; j<chunk_shape[J_DIMENSION]; j++) {
                    chunk_row_insertion_point_address[J_DIMENSION] = chunk_origin[J_DIMENSION] + j;
                    target_element_index = get_index(chunk_row_insertion_point_address,array_shape);
                    target_char_index = target_element_index * prototype()->width();

                    BESDEBUG("dmrpp", "DmrppArray::" << __func__ << "() - "
                            "k: " << k << " j: " << j <<
                            " target_char_index: " << target_char_index <<
                            " source_char_index: " << source_char_index <<
                            " chunk_row_insertion_point_address: " << vec2str(chunk_row_insertion_point_address) << endl);

                    memcpy(target_buffer+target_char_index, source_buffer+source_char_index, chunk_inner_dim_bytes);
                    source_element_index += chunk_shape[I_DIMENSION];
                    source_char_index = source_element_index * prototype()->width();
                }
            }
        }

    }break;
    //########################### FourD Arrays ###############################
    case 4: {
        char * target_buffer = get_buf();
        vector<unsigned int> chunk_shape = get_chunk_dimension_sizes();
        unsigned long long chunk_inner_dim_bytes = chunk_shape[2] * prototype()->width();

        for(unsigned long i=0; i<chunk_refs->size(); i++) {
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - READING chunk[" << i << "]: " << (*chunk_refs)[i].to_string() << endl);
            H4ByteStream h4bs = (*chunk_refs)[i];
            h4bs.read();

            vector<unsigned int> chunk_origin = h4bs.get_position_in_array();

            char * source_buffer = h4bs.get_rbuf();
            unsigned long long source_element_index = 0;
            unsigned long long source_char_index = 0;

            unsigned long long target_element_index = get_index(chunk_origin,array_shape);
            unsigned long long target_char_index = target_element_index * prototype()->width();

            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Packing Array From Chunk[" << i << "]"
                    << " chunk_origin: " << vec2str(chunk_origin) << endl);

            unsigned int L_DIMENSION = 0; // Outermost dim
            unsigned int K_DIMENSION = 1;
            unsigned int J_DIMENSION = 2;
            unsigned int I_DIMENSION = 3;// inner most dim (fastest varying)

            vector<unsigned int> chunk_row_insertion_point_address = chunk_origin;
            for(unsigned int l=0; l<chunk_shape[L_DIMENSION]; l++) {
                chunk_row_insertion_point_address[L_DIMENSION] = chunk_origin[L_DIMENSION] + l;
                BESDEBUG("dmrpp", "DmrppArray::" << __func__ << "() - "
                        << "l: " << l << "  chunk_row_insertion_point_address: "
                        << vec2str(chunk_row_insertion_point_address) << endl);
                for(unsigned int k=0; k<chunk_shape[K_DIMENSION]; k++) {
                    chunk_row_insertion_point_address[K_DIMENSION] = chunk_origin[K_DIMENSION] + k;
                    BESDEBUG("dmrpp", "DmrppArray::" << __func__ << "() - "
                            << "l: " << l
                            << " k: " << k
                            << " chunk_row_insertion_point_address: "
                            << vec2str(chunk_row_insertion_point_address) << endl);
                    for(unsigned int j=0; j<chunk_shape[J_DIMENSION]; j++) {
                        chunk_row_insertion_point_address[J_DIMENSION] = chunk_origin[J_DIMENSION] + j;
                        target_element_index = get_index(chunk_row_insertion_point_address,array_shape);
                        target_char_index = target_element_index * prototype()->width();

                        BESDEBUG("dmrpp", "DmrppArray::" << __func__ << "() - "
                                << "l: " << l << " k: " << k << " j: " << j <<
                                " target_char_index: " << target_char_index <<
                                " source_char_index: " << source_char_index <<
                                " chunk_row_insertion_point_address: " << vec2str(chunk_row_insertion_point_address) << endl);

                        memcpy(target_buffer+target_char_index, source_buffer+source_char_index, chunk_inner_dim_bytes);
                        source_element_index += chunk_shape[I_DIMENSION];
                        source_char_index = source_element_index * prototype()->width();
                    }
                }
            }
        }
    }break;
#endif
    //########################### N-Dimensional Arrays ###############################
    default:
        for (unsigned long i = 0; i < chunk_refs->size(); i++) {

            BESDEBUG("dmrpp", __FUNCTION__ <<": chunk[" << i << "]: " << (*chunk_refs)[i].to_string() << endl);

            H4ByteStream h4bs = (*chunk_refs)[i];

            vector<unsigned int> target_element_address = h4bs.get_position_in_array();
            vector<unsigned int> chunk_source_address(dimensions(), 0);

            // Recursive insertion operation.
            insert_constrained_chunk(0, &target_element_address, &chunk_source_address, &h4bs);

            BESDEBUG("dmrpp",
                    __FUNCTION__ <<": chunk[" << i << "] was " << (h4bs.is_read()?"READ ":"SKIPPED ") << endl);
        }
        break;
    }

    return true;
}

/**
 * @brief This recursive call inserts a (previously read) chunk's data into the
 * appropriate parts of the Array object's internal memory.
 *
 * Successive calls climb into the array to the insertion point for the current
 * chunk's innermost row. Once located, this row is copied into the array at the
 * insertion point. The next row for insertion is located by returning from the
 * insertion call to the next dimension iteration in the call recursive call
 * stack.
 *
 * This starts with dimension 0 and the chunk_row_insertion_point_address set
 * to the chunks origin point
 *
 * @param dim is the dimension on which we are working. We recurse from
 * dimension 0 to the last dimension
 * @param target_element_address - This vector is used to hold the element
 * address in the result array to where this chunk's data will be written.
 * @param chunk_source_address - This vector is used to hold the chunk
 * element address from where data will be read. The values of this are relative to
 * the chunk's origin (position in array).
 * @param chunk The H4ByteStream containing the read data values to insert.
 */
void DmrppArray::insert_constrained_chunk(unsigned int dim, vector<unsigned int> *target_element_address,
        vector<unsigned int> *chunk_source_address, H4ByteStream *chunk)
{

    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - dim: "<< dim << " BEGIN "<< endl);

    // The size, in elements, of each of the chunks dimensions.
    vector<unsigned int> chunk_shape = get_chunk_dimension_sizes();

    // The array index of the last dimension
    unsigned int last_dim = chunk_shape.size() - 1;

    // The chunk's origin point a.k.a. its "position in array".
    vector<unsigned int> chunk_origin = chunk->get_position_in_array();

    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - Retrieving dimension "<< dim << endl);

    dimension thisDim = this->get_dimension(dim);

    BESDEBUG("dmrpp",
            "DmrppArray::"<< __func__ <<"() - thisDim: "<< thisDim.name << " start " << thisDim.start << " stride " << thisDim.stride << " stop " << thisDim.stop << endl);

    // What's the first element that we are going to access for this dimension of the chunk?
    unsigned int first_element_offset = 0; // start with 0
    if ((unsigned) thisDim.start < chunk_origin[dim]) {
        // If the start is behind this chunk, then it's special.
        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - dim: "<<dim << " thisDim.start: " << thisDim.start << endl);
        if (thisDim.stride != 1) { // And if the stride isn't 1,
            // we have to figure our where to begin in this chunk.
            first_element_offset = (chunk_origin[dim] - thisDim.start) % thisDim.stride;
            // If it's zero great!
            if (first_element_offset != 0) {
                // otherwise we adjustment to get correct first element.
                first_element_offset = thisDim.stride - first_element_offset;
            }
        }
        BESDEBUG("dmrpp",
                "DmrppArray::"<< __func__ <<"() - dim: "<< dim << " first_element_offset: " << first_element_offset << endl);
    }
    else {
        first_element_offset = thisDim.start - chunk_origin[dim];
        BESDEBUG("dmrpp",
                "DmrppArray::"<< __func__ <<"() - dim: "<< dim << " thisDim.start is beyond the chunk origin at this dim. first_element_offset: " << first_element_offset << endl);
    }
    // Is the next point to be sent in this chunk at all?
    if (first_element_offset > chunk_shape[dim]) {
        // Nope! Time to bail
        return;
    }

    unsigned long long start_element = chunk_origin[dim] + first_element_offset;
    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - dim: "<< dim << " start_element: " << start_element << endl);

    // Now we figure out the correct last element, based on the subset expression
    unsigned long long end_element = chunk_origin[dim] + chunk_shape[dim] - 1;
    if ((unsigned) thisDim.stop < end_element) {
        end_element = thisDim.stop;
        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - dim: "<< dim << " thisDim.stop is in this chunk. " << endl);
    }
    BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - dim: "<< dim << " end_element: " << end_element << endl);

    // Do we even want this chunk?
    if ((unsigned) thisDim.start > (chunk_origin[dim] + chunk_shape[dim])
            || (unsigned) thisDim.stop < chunk_origin[dim]) {
        // No. No, we do not. Skip this.
        BESDEBUG("dmrpp",
                "DmrppArray::"<< __func__ <<"() - dim: " << dim << " Chunk not accessed by CE. SKIPPING." << endl);
        return;
    }

    unsigned long long chunk_start = start_element - chunk_origin[dim];
    unsigned long long chunk_end = end_element - chunk_origin[dim];

    if (dim == last_dim) {

        BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - dim: "<< dim << " THIS IS LAST DIM. WRITING. "<< endl);

        // Now. Now we are going to read this thing.
        // Read and Process chunk
        chunk->read(is_deflate_compression(), get_chunk_size_in_elements() * var()->width());
        char * source_buffer = chunk->get_rbuf();

        if (thisDim.stride == 1) {
            //#############################################################################
            // ND - inner_stride == 1

            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - dim: " << dim << " The stride is 1." << endl);

            // Compute how much we are going to copy
            unsigned long long chunk_constrained_inner_dim_elements = end_element - start_element + 1;
            BESDEBUG("dmrpp",
                    "DmrppArray::"<< __func__ <<"() - dim: " << dim << " chunk_constrained_inner_dim_elements: " << chunk_constrained_inner_dim_elements << endl);

            unsigned long long chunk_constrained_inner_dim_bytes = chunk_constrained_inner_dim_elements
                    * prototype()->width();
            BESDEBUG("dmrpp",
                    "DmrppArray::"<< __func__ <<"() - dim: " << dim << " chunk_constrained_inner_dim_bytes: " << chunk_constrained_inner_dim_bytes << endl);

            // Compute where we need to put it.
            (*target_element_address)[dim] = (start_element - thisDim.start) / thisDim.stride;
            BESDEBUG("dmrpp",
                    "DmrppArray::"<< __func__ <<"() - dim: " << dim << " target_element_address: " << vec2str(*target_element_address) << endl);

            unsigned int target_start_element_index = get_index(*target_element_address, get_shape(true));
            BESDEBUG("dmrpp",
                    "DmrppArray::"<< __func__ <<"() - dim: " << dim << " target_start_element_index: " << target_start_element_index << endl);

            unsigned int target_char_start_index = target_start_element_index * prototype()->width();
            BESDEBUG("dmrpp",
                    "DmrppArray::"<< __func__ <<"() - dim: " << dim << " target_char_start_index: " << target_char_start_index << endl);

            // Compute where we are going to read it from
            (*chunk_source_address)[dim] = first_element_offset;
            BESDEBUG("dmrpp",
                    "DmrppArray::"<< __func__ <<"() - dim: " << dim << " chunk_source_address: " << vec2str(*chunk_source_address) << endl);

            unsigned int chunk_start_element_index = get_index(*chunk_source_address, chunk_shape);
            BESDEBUG("dmrpp",
                    "DmrppArray::"<< __func__ <<"() - dim: " << dim << " chunk_start_element_index: " << chunk_start_element_index << endl);

            unsigned int chunk_char_start_index = chunk_start_element_index * prototype()->width();
            BESDEBUG("dmrpp",
                    "DmrppArray::"<< __func__ <<"() - dim: " << dim << " chunk_char_start_index: " << chunk_char_start_index << endl);

            char *target_buffer = get_buf();

            // Copy the bytes
            BESDEBUG("dmrpp",
                    "DmrppArray::"<< __func__ <<"() - dim: " << dim << " Using memcpy to transfer " << chunk_constrained_inner_dim_bytes << " bytes." << endl);
            memcpy(target_buffer + target_char_start_index, source_buffer + chunk_char_start_index,
                    chunk_constrained_inner_dim_bytes);
        }
        else {
            //#############################################################################
            // inner_stride != 1
            unsigned long long vals_in_chunk = 1 + (end_element - start_element) / thisDim.stride;
            BESDEBUG("dmrpp",
                    "DmrppArray::"<< __func__ <<"() - dim: "<<dim<<" InnerMostStride is equal to " << thisDim.stride << ". Copying " << vals_in_chunk << " individual values." << endl);

            unsigned long long chunk_start = start_element - chunk_origin[dim];
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - ichunk_start: " << chunk_start << endl);

            unsigned long long chunk_end = end_element - chunk_origin[dim];
            BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() - chunk_end: " << chunk_end << endl);

            for (unsigned int chunk_index = chunk_start; chunk_index <= chunk_end; chunk_index += thisDim.stride) {
                BESDEBUG("dmrpp", "DmrppArray::"<< __func__ <<"() --------- idim_index: " << chunk_index << endl);

                // Compute where we need to put it.
                (*target_element_address)[dim] = (chunk_index + chunk_origin[dim] - thisDim.start) / thisDim.stride;
                BESDEBUG("dmrpp",
                        "DmrppArray::"<< __func__ <<"() - target_element_address: " << vec2str(*target_element_address) << endl);

                unsigned int target_start_element_index = get_index(*target_element_address, get_shape(true));
                BESDEBUG("dmrpp",
                        "DmrppArray::"<< __func__ <<"() - target_start_element_index: " << target_start_element_index << endl);

                unsigned int target_char_start_index = target_start_element_index * prototype()->width();
                BESDEBUG("dmrpp",
                        "DmrppArray::"<< __func__ <<"() - target_char_start_index: " << target_char_start_index << endl);

                // Compute where we are going to read it from
                (*chunk_source_address)[dim] = chunk_index;
                BESDEBUG("dmrpp",
                        "DmrppArray::"<< __func__ <<"() - chunk_source_address: " << vec2str(*chunk_source_address) << endl);

                unsigned int chunk_start_element_index = get_index(*chunk_source_address, chunk_shape);
                BESDEBUG("dmrpp",
                        "DmrppArray::"<< __func__ <<"() - chunk_start_element_index: " << chunk_start_element_index << endl);

                unsigned int chunk_char_start_index = chunk_start_element_index * prototype()->width();
                BESDEBUG("dmrpp",
                        "DmrppArray::"<< __func__ <<"() - chunk_char_start_index: " << chunk_char_start_index << endl);

                char *target_buffer = get_buf();

                // Copy the bytes
                BESDEBUG("dmrpp",
                        "DmrppArray::"<< __func__ <<"() - Using memcpy to transfer " << prototype()->width() << " bytes." << endl);
                memcpy(target_buffer + target_char_start_index, source_buffer + chunk_char_start_index,
                        prototype()->width());
            }
        }
    }
    else {
        // Not the last dimension, so we continue to proceed down the Recursion Branch.
        for (unsigned int dim_index = chunk_start; dim_index <= chunk_end; dim_index += thisDim.stride) {
            (*target_element_address)[dim] = (chunk_origin[dim] + dim_index - thisDim.start) / thisDim.stride;
            (*chunk_source_address)[dim] = dim_index;

            BESDEBUG("dmrpp",
                    "DmrppArray::" << __func__ << "() - RECURSION STEP - " << "Departing dim: " << dim << " dim_index: " << dim_index << " target_element_address: " << vec2str((*target_element_address)) << " chunk_source_address: " << vec2str((*chunk_source_address)) << endl);

            // Re-entry here:
            insert_constrained_chunk(dim + 1, target_element_address, chunk_source_address, chunk);
        }
    }
}

bool DmrppArray::read()
{
    if (read_p()) return true;

    if (get_chunk_dimension_sizes().empty()) {
        if (get_immutable_chunks().size() == 1) {
            // This handles the case for arrays that have exactly one h4:byteStream
            return read_no_chunks();
        }
        else {
            ostringstream oss;
            oss << "DmrppArray: Unchunked arrays must have exactly one H4ByteStream object. "
                    "This one has " << get_immutable_chunks().size() << endl;
            throw BESError(oss.str(), BES_INTERNAL_ERROR, __FILE__, __LINE__);
        }
    }
    else {
        // so now we know we are handling the chunks
        return read_chunks();
    }
}

void DmrppArray::dump(ostream & strm) const
{
    strm << DapIndent::LMarg << "DmrppArray::" << __func__ << "(" << (void *) this << ")" << endl;
    DapIndent::Indent();
    DmrppCommon::dump(strm);
    Array::dump(strm);
    strm << DapIndent::LMarg << "value: " << "----" << /*d_buf <<*/endl;
    DapIndent::UnIndent();
}

} // namespace dmrpp
