
// -*- mode: c++; c-basic-offset:4 -*-

// This file is part of bes, A C++ implementation of the OPeNDAP
// Hyrax data server

// Copyright (c) 2015 OPeNDAP, Inc.
// Authors: James Gallagher <jgallagher@opendap.org>
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

#include <cassert>
#include <sstream>
#include <memory>

#include <BaseType.h>
#include <Int32.h>
#include <Str.h>
#include <Array.h>
#include <Structure.h>

#include <D4RValue.h>
#include <Error.h>
#include <debug.h>
#include <util.h>
#include <ServerFunctionsList.h>

#include "BBoxFunction.h"
#include "roi_utils.h"

using namespace std;

namespace libdap {

/**
 * @brief Return the bounding box for an array
 *
 * Given an N-dimensional Array of simple types and two
 * minimum and maximum values, return the indices of a N-dimensional
 * bounding box. The indices are returned using an Array of
 * Structure, where each element of the array holds the name,
 * start index and stop index in fields with those names.
 *
 * It is up to the caller to make use of the returned values; the
 * array is not modified in any way other than to read in it's
 * values (and set the variable's read_p property).
 *
 * @note There are both DAP2 and DAP4 versions of this function.
 *
 * @param argc Argument count
 * @param argv Argument vector - variable in the current DDS
 * @param dds The current DDS
 * @param btpp Value-result parameter for the resulting Array of Structure
 */
void
function_dap2_bbox(int argc, BaseType *argv[], DDS &, BaseType **btpp)
{
    switch (argc) {
    case 0:
        throw Error("No help yet");
    case 3:
        // correct number of args
        break;
    default:
        throw Error(malformed_expr, "Wrong number of args to bbox()");
    }

    if (argv[0] && argv[0]->type() != dods_array_c)
        throw Error("In function bbox(): Expected argument 1 to be an Array.");
    if (!argv[0]->var()->is_simple_type() || argv[0]->var()->type() == dods_str_c || argv[0]->var()->type() == dods_url_c)
        throw Error("In function bbox(): Expected argument 1 to be an Array of numeric types.");

    // cast is safe given the above
    Array *the_array = static_cast<Array*>(argv[0]);

    // Read the variable into memory
    the_array->read();
    the_array->set_read_p(true);

    // Get the values as doubles
    vector<double> the_values;
    extract_double_array(the_array, the_values); // This function sets the size of the_values

    double min_value = extract_double_value(argv[1]);
    double max_value = extract_double_value(argv[2]);

    // Build the response
    unsigned int rank = the_array->dimensions();
    auto_ptr<Array> response = roi_bbox_build_empty_bbox(rank, "indices");

    switch (rank) {
    case 1: {
        unsigned int X = the_array->dimension_size(the_array->dim_begin());

        bool found_start = false;
        unsigned int start = 0;
        for (unsigned int i = 0; i < X && !found_start; ++i) {
            if (the_values.at(i) >= min_value && the_values.at(i) <= max_value) {
                start = i;
                found_start = true;
            }
        }

        // ! found_start == error?
        if (!found_start) {
            ostringstream oss("In function bbox(): No values between ", std::ios::ate);
            oss << min_value << " and " << max_value << " were found in the array '" << the_array->name() << "'";
            throw Error(oss.str());
        }

        bool found_stop = false;
        unsigned int stop = X-1;
        for (int i = X - 1; i >= 0 && !found_stop; --i) {
            if (the_values.at(i) >= min_value && the_values.at(i) <= max_value) {
                stop = (unsigned int)i;
                found_stop = true;
            }
        }

        // ! found_stop == error?
        if (!found_stop)
            throw InternalErr(__FILE__, __LINE__, "In BBoxFunction: Found start but not stop.");

        Structure *slice = roi_bbox_build_slice(start, stop, the_array->dimension_name(the_array->dim_begin()));
        response->set_vec_nocopy(0, slice);
        break;
    }
    case 2: {
#if 1
        // quick reminder: rows == y == j; cols == x == i
        Array::Dim_iter rows = the_array->dim_begin(), cols = the_array->dim_begin()+1;
        unsigned int Y = the_array->dimension_size(rows);
        unsigned int X = the_array->dimension_size(cols);

        unsigned int x_start = 0;
        unsigned int y_start = 0;
        bool found_y_start = false;
        // Must look at all rows to find the 'left-most' col with value
        for (unsigned int j = 0; j < Y; ++j) {
            bool found_x_start = false;

            for (unsigned int i = 0; i < X && !found_x_start; ++i) {
                unsigned int ind = j * X + i;
                if (the_values.at(ind) >= min_value && the_values.at(ind) <= max_value) {
                    x_start = min(i, x_start);
                    found_x_start = true;
                    if (!found_y_start) {
                        y_start = j;
                        found_y_start = true;
                    }
                }
            }
        }

        // ! found_y_start == error?
        if (!found_y_start) {
            ostringstream oss("In function bbox(): No values between ", std::ios::ate);
            oss << min_value << " and " << max_value << " were found in the array '" << the_array->name() << "'";
            throw Error(oss.str());
        }

        unsigned int x_stop = 0;
        unsigned int y_stop = 0;
        bool found_y_stop = false;
        // Must look at all rows to find the 'left-most' col with value
        for (int j = Y - 1; j >= (int)y_start; --j) {
            bool found_x_stop = false;

            for (int i = X - 1; i >= 0 && !found_x_stop; --i) {
                unsigned int ind = j * X + i;
                if (the_values.at(ind) >= min_value && the_values.at(ind) <= max_value) {
                    x_stop = max((unsigned int)i, x_stop);
                    found_x_stop = true;
                    if (!found_y_stop) {
                        y_stop = j;
                        found_y_stop = true;
                    }
                }
            }
        }

        // ! found_stop == error?
        if (!found_y_stop)
            throw InternalErr(__FILE__, __LINE__, "In BBoxFunction: Found start but not stop.");

        response->set_vec_nocopy(0, roi_bbox_build_slice(y_start, y_stop, the_array->dimension_name(rows)));
        response->set_vec_nocopy(1, roi_bbox_build_slice(x_start, x_stop, the_array->dimension_name(cols)));
        break;
#else
        int i = 0;
        for (Array::Dim_iter di = the_array->dim_begin(), de = the_array->dim_end(); di != de; ++di) {
            // FIXME hack code for now to see end-to-end operation. jhrg 2/25/15
            //Structure *slice = roi_bbox_build_slice(10, 20, the_array->dimension_name(di));
            //response->set_vec_nocopy(i++, slice);
            response->set_vec_nocopy(i++, roi_bbox_build_slice(10, 20, the_array->dimension_name(di)));
        }
        break;
#endif
    }
    case 3:
        //break;
    default:
        throw Error("In function bbox(): Arrays with rank " + long_to_string(rank) + " are not yet supported.");
        break;
    }

    response->set_read_p(true);
    response->set_send_p(true);

    *btpp = response.release();
    return;
}

/**
 * @brief Return the bounding box for an array
 *
 * @note The main difference between this function and the DAP2
 * version is to use args->size() in place of argc and
 * args->get_rvalue(n)->value(dmr) in place of argv[n].
 *
 * @note Not yet implemented.
 *
 * @see function_dap2_bbox
 */
BaseType *function_dap4_bbox(D4RValueList * /* args */, DMR & /* dmr */)
{
    auto_ptr<Array> response(new Array("bbox", new Structure("bbox")));

    throw Error(malformed_expr, "Not yet implemented for DAP4 functions.");

    return response.release();
}

} // namesspace libdap
