//
//  YMBase.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMBase_h
#define YMBase_h
    
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#define YM_EXTERN_C_PUSH    extern "C" {
#define YM_EXTERN_C_POP     }
#else
#define YM_EXTERN_C_PUSH
#define YM_EXTERN_C_POP
#endif

YM_EXTERN_C_PUSH

#if defined(__GNUC__) && !defined(__printflike)
#define __printflike(x,y) __attribute__ ((format (printf, x, y)))
#endif

#if defined(__llvm__) || defined(__clang__)
#define YM_VARGS_SENTINEL_REQUIRED __attribute__((sentinel(0)))
#define YM_WPUSH \
			_Pragma("GCC diagnostic push") \
			_Pragma("GCC diagnostic ignored \"-Wall\"")
#define YM_WPPUSH \
			_Pragma("GCC diagnostic push") \
			_Pragma("GCC diagnostic ignored \"-Wpedantic\"")
#define YM_WPOP \
			_Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER) || defined(YMLINUX)
#define YM_VARGS_SENTINEL_REQUIRED
#define YM_WPPUSH
#define YM_WPUSH
#define YM_WPOP
# if !defined(__printflike)
# define __printflike(x,y)
# endif
#else
#error unknown configuration
#endif

#ifdef WIN32
# ifdef LIBYAMMER_EXPORTS
# define YMAPI __declspec( dllexport )
# else
# define YMAPI __declspec( dllimport )
# endif
#define YMFILE HANDLE
#else
#define YMAPI
#define YMFILE int
#endif

typedef const void *YMTypeRef;
typedef char YMTypeID;

YMTypeRef YMAPI YMRetain(YMTypeRef object);
YMTypeRef YMAPI YMAutorelease(YMTypeRef object);
#ifdef YMDEBUG
#define YM_RELEASE_RETURN_TYPE bool
#else
#define YM_RELEASE_RETURN_TYPE void
#endif
YM_RELEASE_RETURN_TYPE YMAPI YMRelease(YMTypeRef object);

void YMAPI YMSelfLock(YMTypeRef object);
void YMAPI YMSelfUnlock(YMTypeRef object);

typedef enum
{
    YMIOSuccess = 1,
    YMIOEOF = 0,
    YMIOError = -1
} YMIOResult;

void YMAPI YMFreeGlobalResources();

YM_EXTERN_C_POP

#endif /* YMBase_h */
