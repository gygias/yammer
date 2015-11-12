//
//  YMBase.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMBase.h"

#include "YMLog.h"

typedef struct __YMType
{
    YMTypeID _type;
} _YMType;

YMTypeID _YMAddressTypeID = 'a';
YMTypeID _YMmDNSBrowserTypeID = 'b';
YMTypeID _YMConnectionTypeID = 'c';
YMTypeID _YMPipeTypeID = 'i';
YMTypeID _YMLockTypeID = 'k';
YMTypeID _YMRSAKeyPairTypeID = 'K';
YMTypeID _YMLinkedListTypeID = 'l';
YMTypeID _YMmDNSServiceTypeID = 'm';
YMTypeID _YMLocalSocketPairTypeID = 'o';
YMTypeID _YMSemaphoreTypeID = 'p';
YMTypeID _YMStreamTypeID = 'r';
YMTypeID _YMSessionTypeID = 's';
YMTypeID _YMThreadTypeID = 't';
YMTypeID _YMTLSProviderTypeID = 'T';
YMTypeID _YMSecurityProviderTypeID = 'v';
YMTypeID _YMPlexerTypeID = 'x';
YMTypeID _YMX509CertificateTypeID = 'X';
YMTypeID _YMDictionaryTypeID = 'y';

extern void _YMPipeFree(YMTypeRef);
extern void _YMStreamFree(YMTypeRef);
extern void _YMConnectionFree(YMTypeRef);
extern void _YMSecurityProviderFree(YMTypeRef);
extern void _YMPlexerFree(YMTypeRef);
extern void _YMSessionFree(YMTypeRef);
extern void _YMmDNSServiceFree(YMTypeRef);
extern void _YMmDNSBrowserFree(YMTypeRef);
extern void _YMThreadFree(YMTypeRef);
extern void _YMLockFree(YMTypeRef);
extern void _YMSemaphoreFree(YMTypeRef);
extern void _YMLinkedListFree(YMTypeRef);
extern void _YMDictionaryFree(YMTypeRef);
extern void _YMRSAKeyPairFree(YMTypeRef);
extern void _YMX509CertificateFree(YMTypeRef);
extern void _YMTLSProviderFree(YMTypeRef);
extern void _YMLocalSocketPairFree(YMTypeRef);
extern void _YMAddressFree(YMTypeRef);

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
    else if ( type == _YMmDNSBrowserTypeID )
        _YMmDNSBrowserFree(object);
    else if ( type == _YMThreadTypeID )
        _YMThreadFree(object);
    else if ( type == _YMLockTypeID )
        _YMLockFree(object);
    else if ( type == _YMSemaphoreTypeID )
        _YMSemaphoreFree(object);
    else if ( type == _YMLinkedListTypeID )
        _YMLinkedListFree(object);
    else if ( type == _YMDictionaryTypeID )
        _YMDictionaryFree(object);
    else if ( type == _YMRSAKeyPairTypeID )
        _YMRSAKeyPairFree(object);
    else if ( type == _YMX509CertificateTypeID )
        _YMX509CertificateFree(object);
    else if ( type == _YMTLSProviderTypeID )
        _YMTLSProviderFree(object);
    else if ( type == _YMLocalSocketPairTypeID )
        _YMLocalSocketPairFree(object);
    else if ( type == _YMAddressTypeID )
        _YMAddressFree(object);
    else
    {
        ymlog("YMFree unknown type %c",((_YMType *)object)->_type);
        abort();
    }
}
