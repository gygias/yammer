//
//  YMSemaphore.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMSemaphore.h"

#include "YMPrivate.h"

#include "YMLock.h"

#include <pthread.h>

typedef struct __YMSemaphore
{
    YMTypeID _typeID;
    
    pthread_cond_t cond;
    YMLockRef lock;
} _YMSemaphore;

YMSemaphoreRef YMSemaphoreCreate()
{
    pthread_cond_t cond;
    int result = pthread_cond_init(&cond, NULL); // "FreeBSD doesn't support non-default attributes"
    if ( result != 0 )
    {
        YMLog("fatal: pthread_cond_init failed: %d (%s)",result,strerror(errno));
        return NULL;
    }
    
    YMSemaphoreRef semaphore = (YMSemaphoreRef)malloc(sizeof(struct __YMSemaphore));
    semaphore->_typeID = _YMSemaphoreTypeID;
    
    semaphore->lock = YMLockCreateWithOptionsAndName(YMLockDefault, "__ymsemaphore_mutex");
    semaphore->cond = cond;
    
    return semaphore;
}

void _YMSemaphoreFree(YMTypeRef object)
{
    YMSemaphoreRef semaphore = (YMSemaphoreRef)object;
    
    int result = pthread_cond_destroy(&semaphore->cond);
    if ( result != 0 )
    {
        YMLog("fatal: pthread_cond_destroy failed: %d (%s)",result,strerror(result));
        abort();
    }
    
    free(semaphore);
}

void YMSemaphoreWait(YMSemaphoreRef semaphore)
{
    YMLockLock(semaphore->lock);
    
    pthread_mutex_t mutex = _YMLockGetMutex(semaphore->lock);
    int result = pthread_cond_wait(&semaphore->cond, &mutex);
    if ( result != 0 )
    {
        YMLog("fatal: pthread_cond_wait failed: %d (%s)",result,strerror(result));
        abort();
    }
    
    YMLockUnlock(semaphore->lock);
}

void YMSemaphoreSignal(YMSemaphoreRef semaphore)
{
    int result = pthread_cond_signal(&(semaphore->cond));
    if ( result != 0 )
    {
        YMLog("fatal: pthread_cond_signal failed: %d (%s)",result,strerror(result));
        abort();
    }
}
