// BESInterface.cc

// This file is part of bes, A C++ back-end server implementation framework
// for the OPeNDAP Data Access Protocol.

// Copyright (c) 2004,2005 University Corporation for Atmospheric Research
// Author: Patrick West <pwest@ucar.edu> and Jose Garcia <jgarcia@ucar.edu>
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
//      jgarcia     Jose Garcia <jgarcia@ucar.edu>

#include <unistd.h>
#include <string>
#include <sstream>

using std::string ;
using std::ostringstream ;

#include "BESInterface.h"

#include "BESStatusReturn.h"
#include "TheBESKeys.h"
#include "BESResponseHandler.h"
#include "BESAggFactory.h"
#include "BESAggregationServer.h"
#include "BESReporterList.h"

#include "BESExceptionManager.h"
#include "BESHandlerException.h"
#include "BESMemoryException.h"

#include "BESDataNames.h"

list< p_opendap_init > BESInterface::_init_list ;
list< p_opendap_end > BESInterface::_end_list ;

BESInterface::BESInterface()
    : _transmitter( 0 )
{
}

BESInterface::~BESInterface()
{
}

/** @brief Executes the given request to generate a specified response object

    Execute the request by:
    1. initializing BES
    2. validating the request, make sure all elements are present
    3. build the request plan (ie filling in the BESDataHandlerInterface)
    4. execute the request plan using the BESDataHandlerInterface
    5. transmit the resulting response object
    6. log the status of the execution
    7. notify the reporters of the request
    8. end the request, which allows developers to add callbacks to notify
    them of the end of the request

    If an exception is thrown in any of these steps the exception is handed
    over to the exception manager in order to generate the proper response.

    @return status of the execution of the request, 0 if okay, !0 otherwise
    @see initialize
    @see validate_data_request
    @see build_data_request_plan
    @see execute_data_request_plan
    @see transmit_data
    @see log_status
    @see report_request
    @see end_request
    @see exception_manager
 */
int
BESInterface::execute_request()
{
    int status = 0 ;

    try
    {
	initialize() ;
	validate_data_request() ;
	build_data_request_plan() ;
	execute_data_request_plan() ;
	invoke_aggregation() ;
    }
    catch( BESException &ex )
    {
	status = exception_manager( ex ) ;
    }
    catch( Error &e )
    {
	ostringstream s ;
	s << "libdap exception: error_code = " << e.get_error_code()
	  << ": " << e.get_error_message() ;
	BESHandlerException ex( s.str(), __FILE__, __LINE__ ) ;
	status = exception_manager( ex ) ;
    }
    catch( bad_alloc &b )
    {
	string serr = "BES out of memory" ;
	BESMemoryException ex( serr, __FILE__, __LINE__ ) ;
	status = exception_manager( ex ) ;
    }
    catch(...)
    {
	string serr = "An undefined exception has been thrown" ;
	BESException ex( serr, __FILE__, __LINE__ ) ;
	status = exception_manager( ex ) ;
    }

    try
    {
	transmit_data() ;
	log_status() ;
	report_request() ;
	end_request() ;
	if( _dhi.error_info ) delete _dhi.error_info ;
	_dhi.error_info = 0 ;
    }
    catch( BESException &ex )
    {
	status = exception_manager( ex ) ;
    }
    catch( Error &e )
    {
	ostringstream s ;
	s << "libdap exception: error_code = " << e.get_error_code()
	  << ": " << e.get_error_message() ;
	BESException ex( s.str(), __FILE__, __LINE__ ) ;
	status = exception_manager( ex ) ;
    }
    catch( bad_alloc &b )
    {
	string serr = "BES out of memory" ;
	BESMemoryException ex( serr, __FILE__, __LINE__ ) ;
	status = exception_manager( ex ) ;
    }
    catch(...)
    {
	string serr = "An undefined exception has been thrown" ;
	BESException ex( serr, __FILE__, __LINE__ ) ;
	status = exception_manager( ex ) ;
    }

    // If there is error information then the transmit, log, report or end
    // failed, so just print the error information
    if( _dhi.error_info )
    {
	_dhi.error_info->print( stdout ) ;
    }

    return status;
}

void
BESInterface::add_init_callback( p_opendap_init init )
{
    _init_list.push_back( init ) ;
}

/** @brief Initialize the BES object
 *
 *  This method must be called by all derived classes as it will initialize
 *  the environment
 */
void
BESInterface::initialize()
{
    bool do_continue = true ;
    init_iter i = _init_list.begin() ;
    for( ; i != _init_list.end() && do_continue == true; i++ )
    {
	p_opendap_init p = *i ;
	do_continue = p( _dhi ) ;
    }
}

/** @brief Validate the incoming request information
 */
void
BESInterface::validate_data_request()
{
}

/** @brief Build the data request plan.

    It is the responsibility of the derived class to build the request plan.
    In other words, the container list must be filled in and the action set
    in the BESDataHandlerInterface structure.

    @see _BESDataHandlerInterface
 */
void
BESInterface::build_data_request_plan()
{
}

/** @brief Execute the data request plan

    Given the information in the BESDataHandlerInterface, execute the
    request. To do this we simply find the response handler given the action
    in the BESDataHandlerInterface and tell it to execute.

    If no BESResponseHandler can be found given the action then an
    exception is thrown.

    @see _BESDataHandlerInterface
    @see BESResponseHandler
    @see DODSResponseObject
 */
void
BESInterface::execute_data_request_plan()
{
    BESResponseHandler *rh = _dhi.response_handler ;
    if( rh )
    {
	rh->execute( _dhi ) ;
    }
    else
    {
	string se = "The response handler \"" + _dhi.action
		    + "\" does not exist" ;
	throw BESHandlerException( se, __FILE__, __LINE__ ) ;
    }
}

/** @brief Aggregate the resulting response object
 */
void
BESInterface::invoke_aggregation()
{
    if( _dhi.data[AGG_CMD] != "" )
    {
	BESAggregationServer *agg = BESAggFactory::TheFactory()->find_handler( _dhi.data[AGG_HANDLER] ) ;
	if( agg )
	{
	    agg->aggregate( _dhi ) ;
	}
    }
}

/** @brief Transmit the resulting response object

    The derived classes are responsible for specifying a transmitter object
    for use in transmitting the response object. Again, the
    BESResponseHandler knows how to transmit itself.

    If no response handler or no response object or no transmitter is
    specified then do nothing here.

    @see BESResponseHandler
    @see DODSResponseObject
    @see BESTransmitter
 */
void
BESInterface::transmit_data()
{
    if( _transmitter )
    {
	if( _dhi.error_info )
	{
	    _dhi.error_info->transmit( _transmitter, _dhi ) ;
	}
	else if( _dhi.response_handler )
	{
	    _dhi.response_handler->transmit( _transmitter, _dhi ) ;
	}
    }
    else
    {
	if( _dhi.error_info )
	{
	    _dhi.error_info->print( stdout ) ;
	}
    }
}

/** @brief Log the status of the request
 */
void
BESInterface::log_status()
{
}

/** @brief Report the request and status of the request to
 * BESReporterList::TheList()

    If interested in reporting the request and status of the request then
    one must register a BESReporter with BESReporterList::TheList().

    If no BESReporter objects are registered then nothing happens.

    @see BESReporterList
    @see BESReporter
 */
void
BESInterface::report_request()
{
    BESReporterList::TheList()->report( _dhi ) ;
}

void
BESInterface::add_end_callback( p_opendap_end end )
{
    _end_list.push_back( end ) ;
}

/** @brief End the OPeNDAP request
 *
 *  This method allows developers to add callbacks at the end of a request,
 *  to do any cleanup or do any extra work at the end of a request
 */
void
BESInterface::end_request()
{
    end_iter i = _end_list.begin() ;
    for( ; i != _end_list.end(); i++ )
    {
	p_opendap_end p = *i ;
	p( _dhi ) ;
    }
}

/** @brief Clean up after the request
 */
void
BESInterface::clean()
{
    if( _dhi.response_handler ) delete _dhi.response_handler ;
    _dhi.response_handler = 0 ;
}

/** @brief Manage any exceptions thrown during the whole process

    Specific responses are generated given a specific Exception caught. If
    additional exceptions are thrown within derived systems then implement
    those in the derived exception_manager methods. This is a catch-all
    manager and should be called once derived methods have caught their
    exceptions.

    @param e BESException to be managed
    @return status after exception is handled
    @see BESException
 */
int
BESInterface::exception_manager( BESException &e )
{
    return BESExceptionManager::TheEHM()->handle_exception( e, _dhi ) ;
}

