//
//  YMSemaphore.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLock.h"
#include "YMPrivate.h"

#include <pthread.h>

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogThreadSync
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

typedef struct __YMLock
{
    YMTypeID _typeID;
    
    pthread_mutex_t mutex;
    char *name;
} _YMLock;

// private
bool _YMLockGetNewMutexWithOptions(YMLockOptions options, pthread_mutex_t *outMutex);

YMLockRef YMLockCreate()
{
    return YMLockCreateWithOptionsAndName(YMLockDefault, NULL);
}

YMLockRef YMLockCreateWithOptions(YMLockOptions options)
{
    return YMLockCreateWithOptionsAndName(options, NULL);
}

YMLockRef YMLockCreateWithOptionsAndName(YMLockOptions options, const char *name)
{
    pthread_mutex_t mutex;
    if ( ! _YMLockGetNewMutexWithOptions(options, &mutex) )
        return NULL;
    
    _YMLock *lock = (_YMLock *)YMALLOC(sizeof(_YMLock));
    lock->_typeID = _YMLockTypeID;
    
    lock->mutex = mutex;
    lock->name = strdup(name?name:"unnamed");
    
    return (YMLockRef)lock;
}

bool _YMLockGetNewMutexWithOptions(YMLockOptions options, pthread_mutex_t *outMutex)
{
    pthread_mutex_t mutex;
    pthread_mutexattr_t attributes;
    pthread_mutexattr_t *attributesPtr = NULL;
    int result;
    
    if ( options & YMLockRecursive )
    {
        attributesPtr = &attributes;
        result = pthread_mutexattr_init(attributesPtr);
        if ( result != 0 )
        {
            ymerr("YMLock: pthread_mutexattr_init failed: %d (%s)", result, strerror(result));
            return false;
        }
        result = pthread_mutexattr_settype(attributesPtr, PTHREAD_MUTEX_RECURSIVE);
        if ( result != 0 )
        {
            ymerr("YMLock: pthread_mutexattr_settype failed: %d (%s)", result, strerror(result));
            goto catch_release;
        }
    }
    
    result = pthread_mutex_init(&mutex, attributesPtr);
    if ( result != 0 )
    {
        ymerr("YMLock: pthread_mutex_init failed: %d (%s)", result, strerror(result));
        goto catch_release;
    }
    
    *outMutex = mutex;
    
catch_release:
    if ( attributesPtr )
        pthread_mutexattr_destroy(attributesPtr);
    return ( result == 0 );
}

void YMLockLock(YMLockRef lock)
{
    int result = pthread_mutex_lock(&lock->mutex);
    bool okay = true;
    switch(result)
    {
        case 0:
            break;
        case EDEADLK:
            ymerr("fatal: user of YMLock (%s) created deadlock", lock->name);
            okay = false;
            break;
        case EINVAL:
            ymerr("fatal: invalid parameter to YMLock (%s)", lock->name);
            okay = false;
            break;
        default:
            ymerr("warning: unknown error on YMLockLock (%s): %d (%s)", lock->name, result, strerror(result));
            break;
    }
    
#ifndef DUMB_AND_DUMBER
    if ( ! okay )
        abort();
#endif
}

void YMLockUnlock(YMLockRef lock)
{
    int result = pthread_mutex_unlock(&lock->mutex);
    bool okay = true;
    switch(result)
    {
        case 0:
            break;
        case EPERM:
            ymerr("fatal: unlocking thread does not hold YMLock (%s)", lock->name);
            okay = false;
            break;
        case EINVAL:
            ymerr("fatal: invalid parameter to YMLock (%s)", lock->name);
            okay = false;
            break;
        default:
            ymerr("warning: unknown error on YMLockLock (%s): %d (%s)", lock->name, result, strerror(result));
            break;
    }
    
    if ( ! okay )
        abort();
}

void _YMLockFree(YMTypeRef object)
{
    YMLockRef lock = (YMLockRef)object;
    
    int result = pthread_mutex_destroy(&lock->mutex);
    if ( result != 0 )
        ymerr("warning: cannot destroy mutex (%s), something may deadlock", lock->name ? lock->name : "unnamed");
    free(lock->name);
    lock->name = NULL;
    
    free(lock);
}

pthread_mutex_t _YMLockGetMutex(YMLockRef lock)
{
    return lock->mutex;
}
