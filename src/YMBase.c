//
//  YMBase.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMBase.h"

#include "YMLog.h"

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
YMTypeID _YMPeerTypeID = 'P';
YMTypeID _YMStreamTypeID = 'r';
YMTypeID _YMSessionTypeID = 's';
YMTypeID _YMStringTypeID = 'S';
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
extern void _YMPeerFree(YMTypeRef);
extern void _YMStringFree(YMTypeRef);

typedef struct __ym_type
{
    YMTypeID type;
    int __retainCount; // todo find a better way to preallocate this
    pthread_mutex_t __retainMutex;
} ___ym_type;
typedef struct __ym_type __YMType;
typedef __YMType *__YMTypeRef;

void __YMFree(__YMTypeRef object);

#include "YMUtilities.h"

YMTypeRef _YMAlloc(YMTypeID type, size_t size)
{
    if ( size < sizeof(__YMType) )
    {
        ymerr("base: fatal: bad alloc");
        abort();
    }
    
    __YMTypeRef object = YMALLOC(size);
    object->type = type;
    object->__retainCount = 1;
    
    pthread_mutex_t mutex;
    if ( ! YMCreateMutexWithOptions(YMLockDefault, &mutex) )
    {
        ymerr("base: fatal: create mutex failed");
        abort();
    }
    object->__retainMutex = mutex;
    
    // can't divine a way to escape infinite recursion here
    //YMStringRef name = _YMStringCreateForYMAlloc("ymtype-%p", object, NULL);
    //object->__retainLock = _YMLockCreateForYMAlloc(YMLockDefault, name);
    //YMRelease(name);
    
    return object;
}

YMTypeRef YMRetain(YMTypeRef object_)
{
    __YMTypeRef object = (__YMTypeRef)object_;
    YMLockMutex(object->__retainMutex);
    if ( object->__retainCount < 1 )
    {
        ymerr("base: fatal: retain count inconsistent");
        abort();
    }
    object->__retainCount++;
    YMUnlockMutex(object->__retainMutex);
    
    return object;
}

void YMRelease(YMTypeRef object_)
{
    __YMTypeRef object = (__YMTypeRef)object_;
    YMLockMutex(object->__retainMutex);
    if ( object->__retainCount < 1 )
    {
        ymerr("base: fatal: retain count inconsistent");
        abort();
    }
    else if ( object->__retainCount == 1 )
        __YMFree(object);
    YMUnlockMutex(object->__retainMutex);
    
    YMDestroyMutex(object->__retainMutex);
    free(object);
}

void __YMFree(__YMTypeRef object)
{
    YMTypeID type = object->type;
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
    else if ( type == _YMPeerTypeID )
        _YMPeerFree(object);
    else if ( type == _YMStringTypeID )
        _YMStringFree(object);
    else
    {
        ymlog("YMFree unknown type %c",object->type);
        abort();
    }
}
