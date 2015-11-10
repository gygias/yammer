//
//  YMConnection.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMConnection.h"
#include "YMPrivate.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogConnection
#if ( ymlog_type >= ymlog_target )
#undef ymlog
#define ymlog(x,...)
#endif

YMConnectionRef YMConnectionCreate()
{
    _YMConnection *connection = (_YMConnection *)calloc(1,sizeof(_YMConnection));
    connection->_typeID = _YMConnectionTypeID;
    
    return (YMConnectionRef)connection;
}

void _YMConnectionFree(YMTypeRef object)
{
    YMConnectionRef connection = (YMConnectionRef)object;
    free(connection);
}
