//
//  YMLock.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMLock_h
#define YMLock_h

#ifndef _WINDOWS
#include <pthread.h>
#define MUTEX_TYPE pthread_mutex_t
#else
#include <windows.h>
#define MUTEX_TYPE HANDLE
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
#ifndef WIN32
pthread_mutex_t *_YMLockGetMutex(YMLockRef lock);
#else
HANDLE _YMLockGetMutex(YMLockRef lock);
#endif

#endif /* YMLock_h */
