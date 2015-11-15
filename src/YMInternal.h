//
//  YMInternal.h
//  yammer
//
//  Created by david on 11/13/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
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

#define YM_DEBUG_INFO // consolidate extra-curricular stuff under here so it doesn't get forgotten

#pragma message "does this have to be hard code?"
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
