//
//  YMInternal.h
//  yammer
//
//  Created by david on 11/13/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMInternal_h
#define YMInternal_h

#include "YMBase.h"
#include "YMString.h"
#include "YMLock.h"

#define YM_USE_CALLOC
#ifdef YM_USE_CALLOC
#define YMALLOC(x) calloc(1,(x))
#else
#define YMALLOC(x) malloc(x)
#endif

#ifndef WIN32
#define YM_ONCE_OBJ pthread_once_t
#define YM_ONCE_INIT PTHREAD_ONCE_INIT
#define YM_ONCE_DEF(x) void x()
#define YM_ONCE_FUNC(x,y) void x() { y; }
#define YM_ONCE_DO(o,f) pthread_once(&o,f);
#define YM_ONCE_DO_LOCAL(f) { static YM_ONCE_OBJ gLocalInitOnce = YM_ONCE_INIT; YM_ONCE_DO(gLocalInitOnce,f); }
#else
#define YM_ONCE_OBJ INIT_ONCE
#define YM_ONCE_INIT INIT_ONCE_STATIC_INIT
#define YM_ONCE_DEF(x) BOOL CALLBACK x(YM_ONCE_OBJ *InitOnce, PVOID Parameter, PVOID *Context)
#define YM_ONCE_FUNC(x,y) BOOL CALLBACK x(YM_ONCE_OBJ *InitOnce, PVOID Parameter, PVOID *Context) { { y } return true; }
#define YM_ONCE_DO(o,f) InitOnceExecuteOnce(&o, f, NULL, NULL);
#define YM_ONCE_DO2(o,f,p,c) InitOnceExecuteOnce(&o, f, p, c);
#define YM_ONCE_DO_LOCAL(f) { static YM_ONCE_OBJ gLocalInitOnce = YM_ONCE_INIT; YM_ONCE_DO(gLocalInitOnce,f,NULL,NULL); }
#define YM_ONCE_DO_LOCAL2(f,p,c) { static YM_ONCE_OBJ gLocalInitOnce = YM_ONCE_INIT; YM_ONCE_DO(gLocalInitOnce,f,p,c); }
#endif

#ifndef WIN32
#define YMFILE int
#else
#define YMFILE HANDLE
#endif

#define YM_DEBUG_INFO // consolidate extra-curricular stuff under here so it doesn't get forgotten

#define YM_TYPE_RESERVED (128 - sizeof(YMTypeID))

typedef struct _ym_type
{
    YMTypeID __type;
    uint8_t __internal[YM_TYPE_RESERVED];
} __ym_type;
typedef struct _ym_type _YMType;
typedef _YMType *_YMTypeRef;

YMTypeRef _YMAlloc(YMTypeID type, size_t size);

extern YMTypeID _YMPipeTypeID;
extern YMTypeID _YMStreamTypeID;
extern YMTypeID _YMConnectionTypeID;
extern YMTypeID _YMSecurityProviderTypeID;
extern YMTypeID _YMPlexerTypeID;
extern YMTypeID _YMSessionTypeID;
extern YMTypeID _YMThreadTypeID;
extern YMTypeID _YMmDNSServiceTypeID;
extern YMTypeID _YMmDNSBrowserTypeID;
extern YMTypeID _YMLockTypeID;
extern YMTypeID _YMSemaphoreTypeID;
extern YMTypeID _YMLinkedListTypeID;
extern YMTypeID _YMDictionaryTypeID;
extern YMTypeID _YMRSAKeyPairTypeID;
extern YMTypeID _YMX509CertificateTypeID;
extern YMTypeID _YMTLSProviderTypeID;
extern YMTypeID _YMLocalSocketPairTypeID;
extern YMTypeID _YMAddressTypeID;
extern YMTypeID _YMPeerTypeID;
extern YMTypeID _YMStringTypeID;

typedef bool (*ym_read_func)(int,const uint8_t*,size_t);
typedef bool (*ym_write_func)(int,const uint8_t*,size_t);

#define YM_STREAM_INFO(x) ((ym_plexer_stream_user_info_ref)_YMStreamGetUserInfo(x))


#endif /* YMInternal_h */
