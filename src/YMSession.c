//
//  YMSession.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSession.h"
#include "YMPrivate.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogSession
#if ( ymlog_type >= ymlog_target )
#undef ymlog
#define ymlog(x,...)
#endif

YMSessionRef YMSessionCreate()
{
    _YMSession *session = (_YMSession *)calloc(1,sizeof(_YMSession));
    session->_typeID = _YMSessionTypeID;
    
    return (YMSessionRef)session;
}

void _YMSessionFree(YMTypeRef object)
{
    YMSessionRef session = (YMSessionRef)object;
    free(session);
}
