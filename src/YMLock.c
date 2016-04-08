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
typedef struct __ym_lock __ym_lock_t;

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
    
    __ym_lock_t *l = (__ym_lock_t *)_YMAlloc(_YMLockTypeID,sizeof(__ym_lock_t));
    
    l->mutex = mutex;
    l->name = name ? YMRetain(name) : YMSTRC("*");
    
    return l;
}

void YMLockLock(YMLockRef l)
{
    bool okay = YMLockMutex(l->mutex);
    ymassert(okay,"fatal: failed to lock mutex: %p",l->mutex);
}

void YMLockUnlock(YMLockRef l)
{
    bool okay = YMUnlockMutex(l->mutex);
    ymassert(okay,"fatal: mutex unlock failed: %p",l->mutex);
}

void _YMLockFree(YMTypeRef o_)
{
    __ym_lock_t *l = (__ym_lock_t *)o_;
    
    bool okay = YMDestroyMutex(l->mutex);
    if ( ! okay )
        ymerr("warning: cannot destroy mutex (%s), something may deadlock", YMSTR(l->name));
    
    YMRelease(l->name);
}

YM_EXTERN_C_POP
