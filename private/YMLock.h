//
//  YMLock.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMLock_h
#define YMLock_h

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WIN32
# if defined(RPI)
# define __USE_UNIX98
# endif
#include <pthread.h>
#define MUTEX_TYPE pthread_mutex_t
#define MUTEX_PTR_TYPE MUTEX_TYPE *
#else
#define MUTEX_TYPE HANDLE
#define MUTEX_PTR_TYPE MUTEX_TYPE
#endif

typedef enum
{
    YMLockNone = 0,
    YMLockRecursive = 1 << 0,
    YMLockErrorCheck = 1 << 1,
    YMLockOptionsAll = YMLockRecursive | YMLockErrorCheck
} YMLockOptions;

#ifdef DEBUG
#define YMInternalLockType YMLockErrorCheck
#else
#define YMInternalLockType YMLockNone
#endif

typedef const struct __ym_lock *YMLockRef;

YMLockRef YMLockCreate();
YMLockRef YMLockCreateWithOptions(YMLockOptions options);
YMLockRef YMLockCreateWithOptionsAndName(YMLockOptions options, YMStringRef name);

// avoid 'try lock'
void YMLockLock(YMLockRef lock);
void YMLockUnlock(YMLockRef lock);

// for YMSemaphore
MUTEX_PTR_TYPE _YMLockGetMutex(YMLockRef lock);

#ifdef __cplusplus
}
#endif

#endif /* YMLock_h */
