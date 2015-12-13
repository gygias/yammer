//
//  YMBase.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMBase.h"

#include "YMLog.h"

YM_EXTERN_C_PUSH

YMFILE gYMWatchFile = NULL_FILE;

YMTypeID _YMAddressTypeID = 'a';
YMTypeID _YMArrayTypeID = 'A';
YMTypeID _YMmDNSBrowserTypeID = 'b';
YMTypeID _YMConnectionTypeID = 'c';
YMTypeID _YMTaskTypeID = 'f';
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
extern void _YMTaskFree(YMTypeRef);
extern void _YMArrayFree(YMTypeRef);

typedef struct __ym_type
{
    YMTypeID __type;
    int __retainCount; // todo find a better way to preallocate this
    MUTEX_PTR_TYPE __mutex;
} ___ym_type;
typedef struct __ym_type __YMType;
typedef __YMType *__YMTypeRef;

void __YMFree(__YMTypeRef object);

#include "YMUtilities.h"

YMTypeRef _YMAlloc(YMTypeID type, size_t size)
{
    ymassert(size >= sizeof(_YMType),"base: fatal: bad alloc '%c'",type);
    
    __YMTypeRef object = YMALLOC(size);
    object->__type = type;
    object->__retainCount = 1;
    
    object->__mutex = YMCreateMutexWithOptions(YMLockRecursive);
    ymassert(object->__mutex,"base: fatal: create mutex failed");
    
    // can't divine a way to escape infinite recursion here
    //YMStringRef name = _YMStringCreateForYMAlloc("ymtype-%p", object, NULL);
    //object->__retainLock = _YMLockCreateForYMAlloc(YMInternalLockType, name);
    //YMRelease(name);
    
    return object;
}

YMTypeRef YMRetain(YMTypeRef object_)
{
    __YMTypeRef object = (__YMTypeRef)object_;
    YMLockMutex(object->__mutex);
    ymassert(object->__retainCount >= 1, "base: fatal: retain count inconsistent for %p",object);

    object->__retainCount++;
    YMUnlockMutex(object->__mutex);
    
    return object;
}

YMTypeRef YMAutorelease(YMTypeRef object)
{
    return object; // todo, lol
}

YM_RELEASE_RETURN_TYPE YMRelease(YMTypeRef object_)
{
    __YMTypeRef object = (__YMTypeRef)object_;
    
    ymsoftassert(object, "released null");
    
    bool dealloc = false;
    YMLockMutex(object->__mutex);
    {
        ymassert(object->__retainCount >= 1, "base: fatal: something has overreleased %p (%c)",object,object->__type);
        
        if ( object->__retainCount-- == 1 )
            dealloc = true;
    }
    YMUnlockMutex(object->__mutex);
    
    if ( dealloc )
    {
        __YMFree(object);
        YMDestroyMutex(object->__mutex); // todo CRASH 2
        free(object);
    }
    
#ifdef YMDEBUG
    return dealloc;
#endif
}

void __YMFree(__YMTypeRef object)
{
    YMTypeID type = object->__type;
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
    else if ( type == _YMTaskTypeID )
        _YMTaskFree(object);
    else if ( type == _YMArrayTypeID )
        _YMArrayFree(object);
    else
        ymabort("base: fatal: free type unknown %c",type);
}

void YMSelfLock(YMTypeRef object)
{
    YMLockMutex(((__YMTypeRef)object)->__mutex);
}

void YMSelfUnlock(YMTypeRef object)
{
    YMUnlockMutex(((__YMTypeRef)object)->__mutex);
}

#include "YMTLSProvider.h"

void YMFreeGlobalResources()
{
    YMTLSProviderFreeGlobals();
	YMUtilitiesFreeGlobals();
}

YM_EXTERN_C_POP
