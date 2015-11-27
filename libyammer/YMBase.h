//
//  YMBase.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMBase_h
#define YMBase_h

#ifdef __cplusplus
extern "C" {
#endif

typedef const void *YMTypeRef;
typedef char YMTypeID;

YMAPI YMTypeRef YMRetain(YMTypeRef object);
YMAPI YMTypeRef YMAutorelease(YMTypeRef object);
#ifdef DEBUG
#define YM_RELEASE_RETURN_TYPE bool
#else
#define YM_RELEASE_RETURN_TYPE void
#endif
YMAPI YM_RELEASE_RETURN_TYPE YMRelease(YMTypeRef object);

YMAPI void YMSelfLock(YMTypeRef object);
YMAPI void YMSelfUnlock(YMTypeRef object);

typedef enum
{
    YMIOSuccess = 1,
    YMIOEOF = 0,
    YMIOError = -1
} YMIOResult;

YMAPI void YMFreeGlobalResources();

#ifndef WIN32
#define YM_VARGS_SENTINEL_REQUIRED __attribute__((sentinel(0,1)))
#define YM_WPPUSH \
_Pragma("GCC diagnostic push") \
_Pragma("GCC diagnostic ignored \"-Wpedantic\"")
#define YM_WPOP \
_Pragma("GCC diagnostic pop")
#else
/*#define bool unsigned char
 #define false 0
 #define true 1
 typedef __int32 int32_t;
 typedef unsigned __int32 uint32_t;
 typedef unsigned __int16 uint16_t;
 typedef unsigned __int8 uint8_t;
 typedef unsigned int size_t;*/
#define ssize_t SSIZE_T
#define YM_VARGS_SENTINEL_REQUIRED
#define __printflike(x,y)
#define YM_WPPUSH
#define YM_WPOP
#define __unused
#define typeof decltype
#endif

#ifdef __cplusplus
}
#endif

#endif /* YMBase_h */
