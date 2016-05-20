
// -*- mode: c++; c-basic-offset:4 -*-

// This file is part of libdap, A C++ implementation of the OPeNDAP Data
// Access Protocol.

// Copyright (c) 2015 OPeNDAP, Inc.
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

#include <cppunit/TextTestRunner.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/extensions/HelperMacros.h>

#include <sstream>
#include <iterator>

#include <BaseType.h>
#include <Byte.h>
#include <Int32.h>
#include <Float32.h>
#include <Float64.h>
#include <Str.h>
#include <Array.h>
#include <Structure.h>
#include <DDS.h>
#include <DMR.h>
#include <D4RValue.h>
#include <util.h>
#include <GetOpt.h>
#include <debug.h>

#include <test/TestTypeFactory.h>
#include <test/TestCommon.h>
#include <test/D4TestTypeFactory.h>

#include "test_config.h"
#include "test_utils.h"

#include "MakeNormalizedMaskFunction.h"
#include "functions_util.h"

using namespace CppUnit;
using namespace libdap;
using namespace std;

int test_variable_sleep_interval = 0;

static bool debug = false;
static bool debug2 = false;

#undef DBG
#define DBG(x) do { if (debug) (x); } while(false);
#undef DBG2
#define DBG2(x) do { if (debug2) (x); } while(false);

namespace functions
{

class MakeNormalizedMaskFunctionTest:public TestFixture
{
private:
    TestTypeFactory btf;
    D4TestTypeFactory d4_ttf;

    Array *dim0, *dim1;

public:
    MakeNormalizedMaskFunctionTest() : dim0(0), dim1(0)
    { }

    ~MakeNormalizedMaskFunctionTest()
    {}

    void setUp() {
        try {
            // Set up the arrays
            const int dim_10 = 10;
            dim0 = new Array("dim0", new Float32("dim0"));
            dim0->append_dim(dim_10, "one");

            vector<dods_float32> values;
            for (int i = 0; i < dim_10; ++i) {
                values.push_back(dim_10 * sin(i * 180/dim_10));
            }
            DBG2(cerr << "Initial one D Array data values: ");
            DBG2(copy(values.begin(), values.end(), ostream_iterator<dods_float32>(cerr, " ")));
            DBG2(cerr << endl);
            dim0->set_value(values, values.size());

            // Set up smaller array
            const int dim_4 = 4;
            dim1 = new Array("dim1", new Float32("dim1"));
            dim1->append_dim(dim_4, "one");

            values.clear();
            for (int i = 0; i < dim_4; ++i) {
                values.push_back(dim_4 * sin(i * 180/dim_4));
            }
            DBG2(cerr << "Initial one D Array data values: ");
            DBG2(copy(values.begin(), values.end(), ostream_iterator<dods_float32>(cerr, " ")));
            DBG2(cerr << endl);
            dim1->set_value(values, values.size());

        }
        catch (Error & e) {
            cerr << "SetUp: " << e.get_error_message() << endl;
            throw;
        }
    }

    void tearDown() { }

    void no_arg_test() {
        DBG(cerr << "In no_arg_test..." << endl);

        BaseType *result = 0;
        try {
            BaseType *argv[] = { };
            DDS *dds = new DDS(&btf, "empty");
            function_dap2_make_normalized_mask(0, argv, *dds /* DDS & */, &result);
            CPPUNIT_ASSERT(result->type() == dods_str_c);
        }
        catch (Error &e) {
            CPPUNIT_FAIL("make_normalized_mask() Should throw an exception when called with no arguments");
        }
        catch (...) {
            CPPUNIT_FAIL("unknown exception.");
        }

        DBG(cerr << "Out no_arg_test" << endl);
    }

    void find_value_index_test() {
        // I googled for this way to init std::vector<>.
        // See http://www.cplusplus.com/reference/vector/vector/vector/
        double init_values[] = {1,2,3,4,5,6,7,8,9,10};
        vector<double> data (init_values, init_values + sizeof(init_values) / sizeof(double) );
        DBG2(cerr << "data values: ");
        DBG2(copy(data.begin(), data.end(), ostream_iterator<double>(cerr, " ")));
        DBG2(cerr << endl);

        CPPUNIT_ASSERT(find_value_index(4.0, data) == 3);

        CPPUNIT_ASSERT(find_value_index(11.0, data) == -1);
    }

    void find_value_indices_test() {
        double init_values[] = {1,2,3,4,5,6,7,8,9,10};
        vector<double> row (init_values, init_values + sizeof(init_values) / sizeof(double) );
        DBG2(cerr << "row values: ");
        DBG2(copy(row.begin(), row.end(), ostream_iterator<double>(cerr, " ")));
        DBG2(cerr << endl);

        double init_values2[] = {10,20,30,40,50,60,70,80,90,100};
        vector<double> col (init_values2, init_values2 + sizeof(init_values2) / sizeof(double) );
        DBG2(cerr << "col values: ");
        DBG2(copy(col.begin(), col.end(), ostream_iterator<double>(cerr, " ")));
        DBG2(cerr << endl);

        vector< vector<double> > maps;
        maps.push_back(row);
        maps.push_back(col);

        vector<double> tuple;
        tuple.push_back(4.0);
        tuple.push_back(40.0);

        vector<int> ind = find_value_indices(tuple, maps);
        CPPUNIT_ASSERT(ind.at(0) == 3);
        CPPUNIT_ASSERT(ind.at(1) == 3);

        vector<double> t2;
        t2.push_back(4.0);
        t2.push_back(41.5);

        ind = find_value_indices(t2, maps);
        CPPUNIT_ASSERT(ind.at(0) == 3);
        CPPUNIT_ASSERT(ind.at(1) == -1);
    }

    void all_indices_valid_test() {
        vector<int> i1;
        i1.push_back(3);
        i1.push_back(7);

        CPPUNIT_ASSERT(all_indices_valid(i1));

        vector<int> i2;
        i2.push_back(3);
        i2.push_back(-1);

        CPPUNIT_ASSERT(!all_indices_valid(i2));
    }

    void make_normalized_mask_helper_test_1() {

        vector<Array*> dims;
        dims.push_back(dim0);   // dim0 has 18 values that are 10 * sin(i * 18)
        dims.push_back(dim0);   // dim0 has 18 values that are 10 * sin(i * 18)

        Array *tuples = new Array("mask", new Float32("mask"));
        vector<dods_float32> tuples_values;
        tuples_values.push_back(10*sin(2*18));
        tuples_values.push_back(10*sin(2*18));
        tuples_values.push_back(10*sin(3*18));
        tuples_values.push_back(10*sin(3*18));
        tuples_values.push_back(10*sin(4*18));
        tuples_values.push_back(10*sin(4*18));
        tuples_values.push_back(10*sin(5*18));
        tuples_values.push_back(10*sin(5*18));
        tuples->set_value(tuples_values, tuples_values.size());

        DBG(cerr << "Tuples: ");
        DBG(copy(tuples_values.begin(), tuples_values.end(), std::ostream_iterator<dods_float32>(std::cerr, " ")));
        DBG(cerr << endl);

        // NB: mask is a value-result parameter passed by reference
	vector< vector<int> > mask;
        mask = make_normalized_mask_helper<dods_float32>(dims, tuples);
        DBG(cerr << "Two D mask: " << dec);
        //DBG(copy(mask.begin(), mask.end(), std::ostream_iterator<int>(std::cerr, " ")));
        DBG(cerr << endl);
	/*
        CPPUNIT_ASSERT(mask.at(0) == 0);
        CPPUNIT_ASSERT(mask.at(21) == 0);
        CPPUNIT_ASSERT(mask.at(22) == 1);
        CPPUNIT_ASSERT(mask.at(33) == 1);
        CPPUNIT_ASSERT(mask.at(44) == 1);
        CPPUNIT_ASSERT(mask.at(55) == 1);
        CPPUNIT_ASSERT(mask.at(56) == 0);
        CPPUNIT_ASSERT(mask.at(70) == 0);
        CPPUNIT_ASSERT(mask.at(80) == 0);
        CPPUNIT_ASSERT(mask.at(99) == 0);
	*/
    }

    CPPUNIT_TEST_SUITE( MakeNormalizedMaskFunctionTest );

    CPPUNIT_TEST(no_arg_test);
    CPPUNIT_TEST(find_value_index_test);
    CPPUNIT_TEST(find_value_indices_test);
    CPPUNIT_TEST(all_indices_valid_test);
    CPPUNIT_TEST(make_normalized_mask_helper_test_1);

    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(MakeNormalizedMaskFunctionTest);

} // namespace functions

int main(int argc, char*argv[]) {
    CppUnit::TextTestRunner runner;
    runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());

    GetOpt getopt(argc, argv, "dD");
    char option_char;
    while ((option_char = getopt()) != EOF)
        switch (option_char) {
        case 'd':
            debug = 1;  // debug is a static global
            break;
        case 'D':
            debug2 = 1;
            break;
        default:
            break;
        }

    bool wasSuccessful = true;
    string test = "";
    int i = getopt.optind;
     if (i == argc) {
        // run them all
        wasSuccessful = runner.run("");
    }
    else {
        while (i < argc) {
            test = string("functions::MakeNormalizedMaskFunctionTest::") + argv[i++];

            wasSuccessful = wasSuccessful && runner.run(test);
        }
    }

    return wasSuccessful ? 0 : 1;
}
