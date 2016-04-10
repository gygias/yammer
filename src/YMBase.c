//
//  YMBase.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMBase.h"

#include "YMLog.h"

// isequal
#include "YMAddress.h"
// ...

YM_EXTERN_C_PUSH

YMFILE gYMWatchFile = NULL_FILE;

YMTypeID _YMAddressTypeID = 'a';
YMTypeID _YMArrayTypeID = 'A';
YMTypeID _YMmDNSBrowserTypeID = 'b';
YMTypeID _YMConnectionTypeID = 'c';
YMTypeID _YMCompressionTypeID = 'C';
YMTypeID _YMTaskTypeID = 'f';
YMTypeID _YMPipeTypeID = 'i';
YMTypeID _YMLockTypeID = 'k';
YMTypeID _YMRSAKeyPairTypeID = 'K';
YMTypeID _YMmDNSServiceTypeID = 'm';
YMTypeID _YMNumberTypeID = 'n';
YMTypeID _YMLocalSocketPairTypeID = 'o';
YMTypeID _YMSemaphoreTypeID = 'p';
YMTypeID _YMPeerTypeID = 'P';
YMTypeID _YMSecurityProviderTypeID = 'v';
YMTypeID _YMSessionTypeID = 's';
YMTypeID _YMSocketTypeID = 'O';
YMTypeID _YMStreamTypeID = 'r';
YMTypeID _YMStringTypeID = 'S';
YMTypeID _YMThreadTypeID = 't';
YMTypeID _YMTLSProviderTypeID = 'T';
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
extern void _YMCompressionFree(YMTypeRef);
extern void _YMSocketFree(YMTypeRef);
extern void _YMNumberFree(YMTypeRef);

typedef struct __ym_type
{
    YMTypeID __type;
    int __retainCount; // todo find a better way to preallocate this
    MUTEX_PTR_TYPE __mutex;
} ___ym_type;
typedef struct __ym_type __ym_type_t;

void __YMFree(__ym_type_t *object);

#include "YMUtilities.h"

YMTypeRef _YMAlloc(YMTypeID type, size_t size)
{
    ymassert(size >= sizeof(_YMType),"base: fatal: bad alloc '%c'",type);
    
    __ym_type_t *object = YMALLOC(size);
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

YMTypeRef YMRetain(YMTypeRef o_)
{
    __ym_type_t *o = (__ym_type_t *)o_;
    YMLockMutex(o->__mutex);
    ymassert(o->__retainCount >= 1, "base: fatal: retain count inconsistent for %p",o);

    o->__retainCount++;
    YMUnlockMutex(o->__mutex);
    
    return o;
}

YMTypeRef YMAutorelease(YMTypeRef object)
{
    return object; // todo, lol
}

YM_RELEASE_RETURN_TYPE YMRelease(YMTypeRef o_)
{
    __ym_type_t *o = (__ym_type_t *)o_;
    
    ymsoftassert(o, "released null");
    
    bool dealloc = false;
    YMLockMutex(o->__mutex);
    {
        ymassert(o->__retainCount >= 1, "base: fatal: something has overreleased %p (%c)",o,o->__type);
        
        if ( o->__retainCount-- == 1 )
            dealloc = true;
    }
    YMUnlockMutex(o->__mutex);
    
    if ( dealloc ) {
        __YMFree(o);
        YMDestroyMutex(o->__mutex);
        free(o);
    }
    
#ifdef YMDEBUG
    return dealloc;
#endif
}

void __YMFree(__ym_type_t *o)
{
    YMTypeID type = o->__type;
    if ( type == _YMPipeTypeID )
        _YMPipeFree(o);
    else if ( type == _YMStreamTypeID )
        _YMStreamFree(o);
    else if ( type == _YMConnectionTypeID )
        _YMConnectionFree(o);
    else if ( type == _YMSecurityProviderTypeID )
        _YMSecurityProviderFree(o);
    else if ( type == _YMPlexerTypeID )
        _YMPlexerFree(o);
    else if ( type == _YMSessionTypeID )
        _YMSessionFree(o);
    else if ( type == _YMmDNSServiceTypeID )
        _YMmDNSServiceFree(o);
    else if ( type == _YMmDNSBrowserTypeID )
        _YMmDNSBrowserFree(o);
    else if ( type == _YMThreadTypeID )
        _YMThreadFree(o);
    else if ( type == _YMLockTypeID )
        _YMLockFree(o);
    else if ( type == _YMSemaphoreTypeID )
        _YMSemaphoreFree(o);
    else if ( type == _YMDictionaryTypeID )
        _YMDictionaryFree(o);
    else if ( type == _YMRSAKeyPairTypeID )
        _YMRSAKeyPairFree(o);
    else if ( type == _YMX509CertificateTypeID )
        _YMX509CertificateFree(o);
    else if ( type == _YMTLSProviderTypeID )
        _YMTLSProviderFree(o);
    else if ( type == _YMLocalSocketPairTypeID )
        _YMLocalSocketPairFree(o);
    else if ( type == _YMAddressTypeID )
        _YMAddressFree(o);
    else if ( type == _YMPeerTypeID )
        _YMPeerFree(o);
    else if ( type == _YMStringTypeID )
        _YMStringFree(o);
    else if ( type == _YMTaskTypeID )
        _YMTaskFree(o);
    else if ( type == _YMArrayTypeID )
        _YMArrayFree(o);
    else if ( type == _YMCompressionTypeID )
        _YMCompressionFree(o);
    else if ( type == _YMSocketTypeID )
        _YMSocketFree(o);
    else if ( type == _YMNumberTypeID )
        _YMNumberFree(o);
    else
        ymabort("base: fatal: free type unknown %c",type);
}

bool YMAPI YMIsEqual(YMTypeRef a_, YMTypeRef b_)
{
    __ym_type_t *a = (__ym_type_t *)a_,
                *b = (__ym_type_t *)b_;
    if ( ! a || ! b )
        return false;
    if ( a->__type != b->__type )
        return false;
    
    if ( a->__type == _YMStringTypeID )
        return YMStringEquals((YMStringRef)a, (YMStringRef)b);
    else if ( a->__type == _YMAddressTypeID )
        return YMAddressIsEqual((YMAddressRef)a, (YMAddressRef)b);
    else
        ymabort("equivalency for ymtype %d not implemented",a->__type);
    return false;
}

void YMSelfLock(YMTypeRef o_)
{
    YMLockMutex(((__ym_type_t *)o_)->__mutex);
}

void YMSelfUnlock(YMTypeRef o_)
{
    YMUnlockMutex(((__ym_type_t *)o_)->__mutex);
}

#include "YMTLSProvider.h"

void YMFreeGlobalResources()
{
    YMTLSProviderFreeGlobals();
	YMUtilitiesFreeGlobals();
    YMLogFreeGlobals();
}

YM_EXTERN_C_POP
