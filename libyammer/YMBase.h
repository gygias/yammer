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
# define YM_EXTERN_C_PUSH    extern "C" {
# define YM_EXTERN_C_POP     }
#else
# define YM_EXTERN_C_PUSH
# define YM_EXTERN_C_POP
#endif

YM_EXTERN_C_PUSH

#if defined(__GNUC__) && !defined(__printflike)
# define __printflike(x,y) __attribute__ ((format (printf, x, y)))
#endif

#if defined(__llvm__) || defined(__clang__)
# define YM_VARGS_SENTINEL_REQUIRED __attribute__((sentinel(0)))
# define YM_WPUSH \
			_Pragma("GCC diagnostic push") \
			_Pragma("GCC diagnostic ignored \"-Wall\"")
# define YM_WPPUSH \
			_Pragma("GCC diagnostic push") \
			_Pragma("GCC diagnostic ignored \"-Wpedantic\"")
# define YM_WPOP \
			_Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER) || defined(YMLINUX)
# define YM_VARGS_SENTINEL_REQUIRED
# define YM_WPPUSH
# define YM_WPUSH
# define YM_WPOP
# if !defined(__printflike)
#  define __printflike(x,y)
# endif
#else
# error unknown configuration
#endif

#if defined(YMWIN32)
# ifdef LIBYAMMER_EXPORTS
#  define YMAPI __declspec( dllexport )
# else
#  define YMAPI __declspec( dllimport )
# endif
# define YMFILE HANDLE
#else
# define YMAPI
# define YMFILE int
#endif

typedef const void * YMTypeRef;
typedef char YMTypeID;

YMTypeRef YMAPI YMRetain(YMTypeRef object);
YMTypeRef YMAPI YMAutorelease(YMTypeRef object);
#ifdef YMDEBUG
# define YM_RELEASE_RETURN_TYPE bool
#else
# define YM_RELEASE_RETURN_TYPE void
#endif
YM_RELEASE_RETURN_TYPE YMAPI YMRelease(YMTypeRef object);

bool YMAPI YMIsEqual(YMTypeRef a, YMTypeRef b);

// exposes the recursive lock used for retain/release, as a convenience/optimization for "objects"
// akin to objc @synchronized(object) 
void YMAPI YMSelfLock(YMTypeRef object);
void YMAPI YMSelfUnlock(YMTypeRef object);

typedef enum
{
    YMIOSuccess = 1,
    YMIOEOF = 0,
    YMIOError = -1
} YMIOResult;

typedef enum
{
    LessThan = -1,
    EqualTo = 0,
    GreaterThan = 1
} ComparisonResult;

typedef enum
{
    YMInterfaceUnknown = 0,
    YMInterfaceLoopback = 1,
    YMInterfaceAWDL = 50,
    YMInterfaceIPSEC = 60,
    YMInterfaceCellular = 100,
    YMInterfaceWirelessEthernet = 200,
    YMInterfaceBluetooth = 300,
    YMInterfaceWiredEthernet = 400,
    YMInterfaceFirewire400 = 500,
    YMInterfaceFirewire800 = 501,
    YMInterfaceFirewire1600 = 502,
    YMInterfaceFirewire3200 = 503,
    YMInterfaceThunderbolt = 600
} YMInterfaceType;

typedef enum YMCompressionType {
    YMCompressionNone = 0,
#if defined(YMAPPLE)
    YMCompressionGZ = 100,
    YMCompressionBZ2 = 200,
    YMCompressionLZ = 300
#endif
} YMCompressionType;

void YMAPI YMFreeGlobalResources();

YM_EXTERN_C_POP

#endif /* YMBase_h */
