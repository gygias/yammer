//
//  YMPrivate.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPrivate_h
#define YMPrivate_h

#include "YMBase.h"

#define YM_EFFICIENT_ALLOC 1
#ifdef YM_EFFICIENT_ALLOC
#define YMMALLOC(x) malloc(x)
#else
#define YMMALLOC(x) calloc(1,(x))
#endif

#define YM_DEBUG_INFO // consolidate extra-curricular stuff under here so it doesn't get forgotten

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

typedef struct __YMTypeRef
{
    YMTypeID _typeID;
} _YMTypeRef;

#endif /* YMPrivate_h */
