//
//  YMSemaphore.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLock.h"

#include "YMUtilities.h"

#include <pthread.h>

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogThreadSync
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

typedef struct __ym_lock
{
    _YMType _type;
    
    pthread_mutex_t mutex;
    YMStringRef name;
} ___ym_lock;
typedef struct __ym_lock __YMLock;
typedef __YMLock *__YMLockRef;

YMLockRef YMLockCreate()
{
    return YMLockCreateWithOptionsAndName(YMLockDefault, NULL);
}

YMLockRef YMLockCreateWithOptions(YMLockOptions options)
{
    return YMLockCreateWithOptionsAndName(options, NULL);
}

YMLockRef YMLockCreateWithOptionsAndName(YMLockOptions options, YMStringRef name)
{
    pthread_mutex_t mutex;
    if ( ! YMCreateMutexWithOptions(options, &mutex) )
        return NULL;
    
    __YMLockRef lock = (__YMLockRef)_YMAlloc(_YMLockTypeID,sizeof(__YMLock));
    
    lock->mutex = mutex;
    lock->name = name ? YMRetain(name) : YMSTRC("unnamed");
    
    return (YMLockRef)lock;
}

YMLockRef _YMLockCreateForYMAlloc(YMLockOptions options, YMStringRef name)
{
    pthread_mutex_t mutex;
    if ( ! YMCreateMutexWithOptions(options, &mutex) )
        return NULL;
    
    __YMLockRef lock = (__YMLockRef)YMALLOC(sizeof(__YMLock));
    lock->_type.type = _YMLockTypeID;
    
    lock->mutex = mutex;
    lock->name = name ? YMRetain(name) : YMSTRC("unnamed");
    
    return (YMLockRef)lock;
}

void YMLockLock(YMLockRef lock_)
{
    __YMLockRef lock = (__YMLockRef)lock_;
    
    ymbool okay = YMLockMutex(lock->mutex);
    
#ifndef DUMB_AND_DUMBER
    if ( ! okay )
        abort();
#endif
}

void YMLockUnlock(YMLockRef lock_)
{
    __YMLockRef lock = (__YMLockRef)lock_;
    
    bool okay = YMUnlockMutex(lock->mutex);
    
    if ( ! okay )
        abort();
}

void _YMLockFree(YMTypeRef object)
{
    __YMLockRef lock = (__YMLockRef)object;
    
    ymbool okay = YMDestroyMutex(lock->mutex);
    if ( ! okay )
        ymerr("warning: cannot destroy mutex (%s), something may deadlock", YMSTR(lock->name));
    
    YMRelease(lock->name);
}

pthread_mutex_t _YMLockGetMutex(YMLockRef lock_)
{
    __YMLockRef lock = (__YMLockRef)lock_;    
    return lock->mutex;
}
