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
# define YMALLOC(x) calloc(1,(x))
#else
# define YMALLOC(x) malloc(x)
#endif
# define YMREALLOC(x,y) realloc(x,y)
# define YMFREE(x) free(x)

#if !defined(YMWIN32)
# define YM_ONCE_OBJ pthread_once_t
# define YM_ONCE_INIT PTHREAD_ONCE_INIT
# define YM_ONCE_DEF(x) void x(void)
# define YM_ONCE_FUNC(x,y) void x(void) { y; }
# define YM_ONCE_DO(o,f) pthread_once(&o,f);
# define YM_ONCE_DO_LOCAL(f) { static YM_ONCE_OBJ gLocalInitOnce = YM_ONCE_INIT; YM_ONCE_DO(gLocalInitOnce,f); }
#else
# define YM_ONCE_OBJ INIT_ONCE
# define YM_ONCE_INIT INIT_ONCE_STATIC_INIT
# define YM_ONCE_DEF(x) BOOL CALLBACK x(YM_ONCE_OBJ *InitOnce, PVOID Parameter, PVOID *Context)
# define YM_ONCE_FUNC(x,y) BOOL CALLBACK x(YM_ONCE_OBJ *InitOnce, PVOID Parameter, PVOID *Context) { { y } return true; }
# define YM_ONCE_DO(o,f) InitOnceExecuteOnce(&o, f, NULL, NULL);
# define YM_ONCE_DO2(o,f,p,c) InitOnceExecuteOnce(&o, f, p, c);
# define YM_ONCE_DO_LOCAL(f) { static YM_ONCE_OBJ gLocalInitOnce = YM_ONCE_INIT; YM_ONCE_DO(gLocalInitOnce,f); }
# define YM_ONCE_DO_LOCAL2(f,p,c) { static YM_ONCE_OBJ gLocalInitOnce = YM_ONCE_INIT; YM_ONCE_DO2(gLocalInitOnce,f,p,c); }
#endif

#ifndef YM_SOFT_ASSERTS
# define YM_SOFT_ASSERTS 1
#endif

YM_WPPUSH
#define ymsoftassert(x,y,...) _ymsoftassert(YM_SOFT_ASSERTS,x,y,##__VA_ARGS__)
#define _ymsoftassert(z,x,y,...) { if (!(x)) { ymerrg("soft assert: "y,##__VA_ARGS__); if (z) abort(); } }
YM_WPOP

#ifndef YM_HARD_ASSERT
# define YM_HARD_ASSERT 1
#endif

YM_WPPUSH
#define ymassert(x,y,...) { if (!(x)) { ymerrg("assert: "y,##__VA_ARGS__); abort(); } }
#define ymabort(x,...) ymassert(false,x,##__VA_ARGS__)
YM_WPOP

#define YM_TYPE_RESERVED (128 - sizeof(YMTypeID))

YM_EXTERN_C_PUSH

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
extern YMTypeID _YMDispatchQueueTypeID;
extern YMTypeID _YMNumberTypeID;
extern YMTypeID _YMmDNSServiceTypeID;
extern YMTypeID _YMmDNSBrowserTypeID;
extern YMTypeID _YMLockTypeID;
extern YMTypeID _YMSemaphoreTypeID;
extern YMTypeID _YMDictionaryTypeID;
extern YMTypeID _YMRSAKeyPairTypeID;
extern YMTypeID _YMX509CertificateTypeID;
extern YMTypeID _YMTLSProviderTypeID;
extern YMTypeID _YMLocalSocketPairTypeID;
extern YMTypeID _YMAddressTypeID;
extern YMTypeID _YMPeerTypeID;
extern YMTypeID _YMStringTypeID;
extern YMTypeID _YMTaskTypeID;
extern YMTypeID _YMArrayTypeID;
extern YMTypeID _YMCompressionTypeID;
extern YMTypeID _YMSocketTypeID;

typedef bool (*ym_read_func)(int,const uint8_t*,size_t);
typedef bool (*ym_write_func)(int,const uint8_t*,size_t);

#define YM_STREAM_INFO(x) ((ym_pstream_user_info_t *)_YMStreamGetUserInfo(x))

YM_EXTERN_C_POP

#endif /* YMInternal_h */
