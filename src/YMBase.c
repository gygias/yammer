//
//  YMBase.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMBase.h"
#include "YMPrivate.h"

YMTypeID _YMPipeTypeID = 'i';
YMTypeID _YMStreamTypeID = 'r';
YMTypeID _YMConnectionTypeID = 'c';
YMTypeID _YMSecurityProviderTypeID = 'p';
YMTypeID _YMPlexerTypeID = 'x';
YMTypeID _YMSessionTypeID = 's';
YMTypeID _YMmDNSServiceTypeID = 'm';
YMTypeID _YMThreadTypeID = 't';

extern void _YMPipeFree(YMTypeRef);
extern void _YMStreamFree(YMTypeRef);
extern void _YMConnectionFree(YMTypeRef);
extern void _YMSecurityProviderFree(YMTypeRef);
extern void _YMPlexerFree(YMTypeRef);
extern void _YMSessionFree(YMTypeRef);
extern void _YMmDNSServiceFree(YMTypeRef);
extern void _YMThreadFree(YMTypeRef);

void YMFree(YMTypeRef object)
{
    YMTypeID type = ((_YMTypeRef*)object)->_typeID;
    if ( type == _YMPipeTypeID )
        _YMPipeFree(object);
    else if ( type == _YMStreamTypeID )
        _YMStreamFree(object);
    else if ( type == _YMConnectionTypeID )
        _YMConnectionFree(object);
    else if ( type == _YMSecurityProviderTypeID )
        _YMSecurityProviderFree(object);
    else if ( type == _YMPlexerTypeID )
        _YMPlexerFree(object);
    else if ( type == _YMSessionTypeID )
        _YMSessionFree(object);
    else if ( type == _YMmDNSServiceTypeID )
        _YMmDNSServiceFree(object);
    else if ( type == _YMThreadTypeID )
        _YMThreadFree(object);
}