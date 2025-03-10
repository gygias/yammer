//
//  YMLock.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMLock_h
#define YMLock_h

#if !defined(YMWIN32)
# if defined(YMLINUX)
#  define __USE_UNIX98
# endif
# include <pthread.h>
# define MUTEX_TYPE pthread_mutex_t
# define MUTEX_PTR_TYPE MUTEX_TYPE *
#else
# define MUTEX_TYPE HANDLE
# define MUTEX_PTR_TYPE MUTEX_TYPE
#endif

YM_EXTERN_C_PUSH

typedef enum
{
    YMLockNone = 0,
    YMLockRecursive = 1 << 0,
    YMLockErrorCheck = 1 << 1,
    YMLockOptionsAll = YMLockRecursive | YMLockErrorCheck
} YMLockOptions;

#ifdef YMDEBUG
#define YMInternalLockType YMLockErrorCheck
#else
#define YMInternalLockType YMLockNone
#endif

typedef const struct __ym_lock * YMLockRef;

YMLockRef YMAPI YMLockCreate(void);
YMLockRef YMAPI YMLockCreateWithOptions(YMLockOptions options);
YMLockRef YMAPI YMLockCreateWithOptionsAndName(YMLockOptions options, YMStringRef name);

// avoid 'try lock'
void YMAPI YMLockLock(YMLockRef lock);
void YMAPI YMLockUnlock(YMLockRef lock);

YM_EXTERN_C_POP

#endif /* YMLock_h */
