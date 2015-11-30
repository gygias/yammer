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

#if defined(_MACOS) || defined(RPI)
#define YM_VARGS_SENTINEL_REQUIRED __attribute__((sentinel(0)))
#endif

#if defined(RPI)
#define __printflike(x,y) __attribute__ ((format (printf, x, y)))
#endif

#ifdef WIN32
# ifndef YMAPI
# define YMAPI __declspec( dllimport )
# endif
#define YMFILE HANDLE
#define YMSOCKET SOCKET
#define __printflike(x,y)
#define YM_VARGS_SENTINEL_REQUIRED
#else
#define YMAPI
#define YMFILE int
#define YMSOCKET YMFILE
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

#ifdef __cplusplus
}
#endif

#endif /* YMBase_h */
