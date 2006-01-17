// OPeNDAPSetCommand.cc

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

#include "OPeNDAPSetCommand.h"
#include "OPeNDAPTokenizer.h"
#include "DODSResponseHandlerList.h"
#include "DODSContainerPersistenceList.h"
#include "OPeNDAPParserException.h"
#include "OPeNDAPDataNames.h"

/** @brief parses the request to create a new container or replace an already
 * existing container given a symbolic name, a real name, and a data type.
 *
 * The syntax for a request handled by this response handler is:
 *
 * set container values * &lt;sym_name&gt;,&lt;real_name&gt;,&lt;data_type&gt;;
 *
 * The request must end with a semicolon and must contain the symbolic name,
 * the real name (in most cases a file name), and the type of data represented
 * by this container (e.g. cedar, netcdf, cdf, hdf, etc...).
 *
 * @param tokenizer holds on to the list of tokens to be parsed
 * @param dhi structure that holds request and response information
 * @throws OPeNDAPParserException if there is a problem parsing the request
 * @see OPeNDAPTokenizer
 * @see _DODSDataHandlerInterface
 */
DODSResponseHandler *
OPeNDAPSetCommand::parse_request( OPeNDAPTokenizer &tokenizer,
                                  DODSDataHandlerInterface &dhi )
{
    string my_token = tokenizer.get_next_token() ;

    /* First we will make sure that the developer has not over-written this
     * command to work with a sub command. In other words, they have a new
     * command called "set something". Look up set.something
     */
    string newcmd = _cmd + "." + my_token ;
    OPeNDAPCommand *cmdobj = OPeNDAPCommand::find_command( newcmd ) ;
    if( cmdobj && cmdobj != OPeNDAPCommand::TermCommand )
    {
	return cmdobj->parse_request( tokenizer, dhi ) ;
    }

    /* No sub command, so proceed with the default set command
     */
    dhi.action = _cmd ;
    DODSResponseHandler *retResponse =
	DODSResponseHandlerList::TheList()->find_handler( _cmd ) ;
    if( !retResponse )
    {
	throw OPeNDAPParserException( (string)"Improper command " + _cmd );
    }

    if( my_token == "container" )
    {
	dhi.data[STORE_NAME] = PERSISTENCE_VOLATILE ;
	my_token = tokenizer.get_next_token() ;
	if( my_token != "values" )
	{
	    if( my_token == "in" )
	    {
		dhi.data[STORE_NAME] = tokenizer.get_next_token() ;
	    }
	    else
	    {
		tokenizer.parse_error( my_token + " not expected\n" ) ;
	    }
	    my_token = tokenizer.get_next_token() ;
	}

	if( my_token == "values" )
	{
	    dhi.data[SYMBOLIC_NAME] = tokenizer.get_next_token() ;
	    my_token = tokenizer.get_next_token() ;
	    if( my_token == "," )
	    {
		dhi.data[REAL_NAME] = tokenizer.get_next_token() ; 
		my_token = tokenizer.get_next_token() ;
		if( my_token == "," )
		{
		    dhi.data[CONTAINER_TYPE] = tokenizer.get_next_token() ;
		    my_token = tokenizer.get_next_token() ;
		    if( my_token != ";" )
		    {
			tokenizer.parse_error( my_token + " not expected\n" ) ;
		    }
		}
		else
		{
		    tokenizer.parse_error( my_token + " not expected\n" ) ;
		}
	    }
	    else
	    {
		tokenizer.parse_error( my_token + " not expected\n" ) ;
	    }
	}
	else
	{
	    tokenizer.parse_error( my_token + " not expected\n" ) ;
	}
    }
    else
    {
	tokenizer.parse_error( my_token + " not expected\n" ) ;
    }

    return retResponse ;
}

// $Log: OPeNDAPSetCommand.cc,v $
