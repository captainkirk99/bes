// wrapperT.cc

// This file is part of bes, A C++ back-end server implementation framework
// for the OPeNDAP Data Access Protocol.

// Copyright (c) 2004,2005 University Corporation for Atmospheric Research
// Author: Patrick West <pwest@ucar.org>
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
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// You can contact University Corporation for Atmospheric Research at
// 3080 Center Green Drive, Boulder, CO 80301
 
// (c) COPYRIGHT University Corporation for Atmostpheric Research 2004-2005
// Please read the full copyright statement in the file COPYRIGHT_UCAR.
//
// Authors:
//      pwest       Patrick West <pwest@ucar.edu>

#include <iostream>

using std::cout ;
using std::cerr ;
using std::endl ;

#include "DODSApacheWrapper.h"
#include "DODSDataRequestInterface.h"
#include "DODSBasicException.h"

int
main( int argc, char **argv )
{
    /*
    if( argc != 2 )
    {
	cerr << "usage: " << argv[0] << " <requests>" << endl ;
	return 1 ;
    }
    */

    DODSDataRequestInterface rq;

    // BEGIN Initialize all data request elements correctly to a null pointer 
    rq.server_name=0;
    rq.server_address=0;
    rq.server_protocol=0;
    rq.server_port=0;
    rq.script_name=0;
    rq.user_address=0;
    rq.user_agent=0;
    rq.request=0;
    // END Initialize all the data request elements correctly to a null pointer

    rq.server_name="cedar-l" ;
    rq.server_address="jose" ;
    rq.server_protocol="TXT" ;
    rq.server_port="8081" ;
    rq.script_name="someting" ;
    rq.user_address="0.0.0.0" ;
    rq.user_agent = "Patrick" ;

    try
    {
	DODSApacheWrapper wrapper ;
	rq.cookie=wrapper.process_user( "username=pwest" ) ;
	wrapper.process_request( "request=define+d1+as+mfp920504a;get+dods+for+d1;" ) ;
	rq.request = wrapper.get_first_request() ;
	while( rq.request )
	{
	    wrapper.call_DODS(rq);
	    rq.request = wrapper.get_next_request() ;
	}

    }
    catch( DODSBasicException &e )
    {
	cerr << "problem: " << e.get_error_description() << endl ;
    }

    return 0 ;
}

