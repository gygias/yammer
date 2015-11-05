//
//  YMSemaphore.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMLock.h"
#include "YMPrivate.h"

#include <pthread.h>

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
    return YMLockCreateWithOptionsAndName(YMLockDefault, NULL);
}

YMLockRef YMLockCreateWithOptionsAndName(YMLockOptions options, char *name)
{
    pthread_mutex_t mutex;
    if ( ! _YMLockGetNewMutexWithOptions(options, &mutex) )
        return NULL;
    
    _YMLock *lock = (_YMLock *)malloc(sizeof(_YMLock));
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
            YMLog("YMLock: pthread_mutexattr_init failed: %d (%s)", result, strerror(result));
            return false;
        }
        result = pthread_mutexattr_settype(attributesPtr, PTHREAD_MUTEX_RECURSIVE);
        if ( result != 0 )
        {
            YMLog("YMLock: pthread_mutexattr_settype failed: %d (%s)", result, strerror(result));
            goto catch_release;
        }
    }
    
    result = pthread_mutex_init(&mutex, attributesPtr);
    if ( result != 0 )
    {
        YMLog("YMLock: pthread_mutex_init failed: %d (%s)", result, strerror(result));
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
    _YMLock *_lock = (_YMLock *)lock;
    int result = pthread_mutex_lock(&_lock->mutex);
    bool okay = true;
    switch(result)
    {
        case 0:
            break;
        case EDEADLK:
            YMLog("fatal: user of YMLock (%s) created deadlock", _lock->name);
            okay = false;
            break;
        case EINVAL:
            YMLog("fatal: invalid parameter to YMLock (%s)", _lock->name);
            okay = false;
            break;
        default:
            YMLog("warning: unknown error on YMLockLock (%s): %d (%s)", _lock->name, result, strerror(result));
            break;
    }
    
#ifndef DUMB_AND_DUMBER
    if ( ! okay )
        abort();
#endif
}

void YMLockUnlock(YMLockRef lock)
{
    _YMLock *_lock = (_YMLock *)lock;
    int result = pthread_mutex_unlock(&_lock->mutex);
    bool okay = true;
    switch(result)
    {
        case 0:
            break;
        case EPERM:
            YMLog("fatal: unlocking thread does not hold YMLock (%s)", _lock->name);
            okay = false;
            break;
        case EINVAL:
            YMLog("fatal: invalid parameter to YMLock (%s)", _lock->name);
            okay = false;
            break;
        default:
            YMLog("warning: unknown error on YMLockLock (%s): %d (%s)", _lock->name, result, strerror(result));
            break;
    }
    
#ifndef DUMB_AND_DUMBER
    if ( ! okay )
        abort();
#endif
}

void _YMLockFree(YMTypeRef object)
{
    _YMLock *lock = (_YMLock *)object;
    
    int result = pthread_mutex_destroy(&lock->mutex);
    if ( result != 0 )
        YMLog("warning: cannot destroy mutex (%s), something may be deadlocked", lock->name ? lock->name : "unnamed");
#warning other Free()s should NULL stuff like this and let misuse crash, not guard
    free(lock->name);
    lock->name = NULL;
    
    free(lock);
}