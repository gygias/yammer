//
//  YMSemaphore.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLock.h"

#include "YMUtilities.h"

#define ymlog_type YMLogThreadSync
#include "YMLog.h"

typedef struct __ym_lock
{
    _YMType _type;
    
	MUTEX_PTR_TYPE mutex;
    YMStringRef name;
} ___ym_lock;
typedef struct __ym_lock __YMLock;
typedef __YMLock *__YMLockRef;

YM_EXTERN_C_PUSH

YMLockRef YMLockCreate()
{
    return YMLockCreateWithOptionsAndName(YMLockNone, NULL);
}

YMLockRef YMLockCreateWithOptions(YMLockOptions options)
{
    return YMLockCreateWithOptionsAndName(options, NULL);
}

YMLockRef YMLockCreateWithOptionsAndName(YMLockOptions options, YMStringRef name)
{
	MUTEX_PTR_TYPE mutex = YMCreateMutexWithOptions(options);
    if ( ! mutex )
        return NULL;
    
    __YMLockRef lock = (__YMLockRef)_YMAlloc(_YMLockTypeID,sizeof(__YMLock));
    
    lock->mutex = mutex;
    lock->name = name ? YMRetain(name) : YMSTRC("*");
    
    return (YMLockRef)lock;
}

void YMLockLock(YMLockRef lock_)
{
    __YMLockRef lock = (__YMLockRef)lock_;
    
    bool okay = YMLockMutex(lock->mutex);
    ymassert(okay,"fatal: failed to lock mutex");
}

void YMLockUnlock(YMLockRef lock_)
{
    __YMLockRef lock = (__YMLockRef)lock_;
    
    bool okay = YMUnlockMutex(lock->mutex);
    ymassert(okay,"fatal: mutex unlock failed: %p",lock->mutex);
}

void _YMLockFree(YMTypeRef object)
{
    __YMLockRef lock = (__YMLockRef)object;
    
    bool okay = YMDestroyMutex(lock->mutex);
    if ( ! okay )
        ymerr("warning: cannot destroy mutex (%s), something may deadlock", YMSTR(lock->name));
    
    YMRelease(lock->name);
}

YM_EXTERN_C_POP
