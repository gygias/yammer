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

//#define PTHREAD_SEMAPHORE
#ifdef PTHREAD_SEMAPHORE
#include <pthread.h>
#else
#include <semaphore.h>
#endif

typedef struct __YMSemaphore
{
    YMTypeID _typeID;
    
    char *userName;
    char *semName;
    YMLockRef lock;
    
#ifdef PTHREAD_SEMAPHORE
    pthread_cond_t cond;
    int value;
#else
    sem_t *sem;
#endif
} _YMSemaphore;

uint16_t gYMSemaphoreIndex = 0;
YMLockRef gYMSemaphoreIndexLock = NULL;
pthread_once_t gYMSemaphoreIndexInit = PTHREAD_ONCE_INIT;

void _YMSemaphoreInit()
{
    gYMSemaphoreIndexLock = YMLockCreateWithOptionsAndName(YMLockDefault, YM_TOKEN_STRING(gYMSemaphoreIndex));
}

YMSemaphoreRef YMSemaphoreCreate(const char *name, int initialValue)
{
    if (initialValue < 0)
    {
        YMLog("semaphore[init]: fatal: initial value cannot be negative");
        abort();
    }
    
#ifdef PTHREAD_SEMAPHORE
    pthread_cond_t cond;
    int result = pthread_cond_init(&cond, NULL); // "FreeBSD doesn't support non-default attributes"
    if ( result != 0 )
    {
        YMLog("semaphore[init]: fatal: pthread_cond_init failed: %d (%s)",result,strerror(errno));
        return NULL;
    }
#endif
    
    pthread_once(&gYMSemaphoreIndexInit, &_YMSemaphoreInit);
    
    YMSemaphoreRef semaphore = (YMSemaphoreRef)YMMALLOC(sizeof(struct __YMSemaphore));
    semaphore->_typeID = _YMSemaphoreTypeID;
    
    semaphore->userName = YMStringCreateWithFormat("%s-%p",name?name:"unnamed",semaphore);
    semaphore->lock = YMLockCreateWithOptionsAndName(YMLockDefault, "__ymsemaphore_mutex");
    
    YMLockLock(gYMSemaphoreIndexLock);
    uint16_t thisIndex = gYMSemaphoreIndex++;
    if ( gYMSemaphoreIndex == 0 )
        YMLog("semaphore[%s,%s]: warning: semaphore name index reset",semaphore->semName,semaphore->userName);
    semaphore->semName = YMStringCreateWithFormat("ym-%u",thisIndex);
    YMLog("semaphore[%s,%s]: created",semaphore->semName,semaphore->userName);
    YMLockUnlock(gYMSemaphoreIndexLock);
    
#ifdef PTHREAD_SEMAPHORE
    semaphore->cond = cond;
    semaphore->value = initialValue;
#else
    semaphore->sem = sem_open(semaphore->semName, O_CREAT, 0700, initialValue); // todo mode?
    if ( semaphore->sem == SEM_FAILED )
    {
        YMLog("semaphore[%s,%s]: fatal: sem_open failed: %d (%s)",semaphore->semName,semaphore->userName,errno,strerror(errno));
        abort(); // since we handle names internally
    }
#endif
    
    return semaphore;
}

void _YMSemaphoreFree(YMTypeRef object)
{
    YMSemaphoreRef semaphore = (YMSemaphoreRef)object;
    YMLog("semaphore[%s,%s]: deallocating",semaphore->semName,semaphore->userName);
    
#ifdef PTHREAD_SEMAPHORE
    int result = pthread_cond_destroy(&semaphore->cond);
    if ( result != 0 )
    {
        YMLog("semaphore[%s,%s]: fatal: pthread_cond_destroy failed: %d (%s)",semaphore->semName,semaphore->userName,result,strerror(result));
        abort();
    }
#else
    int result = sem_unlink(semaphore->semName);
    if ( result == -1 )
        YMLog("semaphore[%s,%s]: warning: sem_unlink failed: %d (%s)",semaphore->semName,semaphore->userName,errno,strerror(errno));
#endif
    
    YMFree(semaphore->lock);
    
    free(semaphore->userName);
    free(semaphore->semName);
    free(semaphore);
}

void YMSemaphoreWait(YMSemaphoreRef semaphore)
{
#ifdef PTHREAD_SEMAPHORE
    YMLockLock(semaphore->lock);
    
    semaphore->value--;
    if ( semaphore->value < 0 )
    {
        pthread_mutex_t mutex = _YMLockGetMutex(semaphore->lock);
        YMLog("semaphore[%s,%s]: waiting on %p...",semaphore->name,semaphore);
        int result = pthread_cond_wait(&semaphore->cond, &mutex);
        if ( result != 0 )
        {
            YMLog("semaphore[%s,%s]: fatal: pthread_cond_wait failed: %d (%s)",semaphore->semName,semaphore->name,result,strerror(result));
            abort();
        }
        YMLog("semaphore[%s,%s]: received signal %p...",semaphore->semName,semaphore->name,semaphore);
    }
    
    YMLockUnlock(semaphore->lock);
#else
    YMLog("semaphore[%s,%s]: waiting",semaphore->semName,semaphore->userName);
    int result = sem_wait(semaphore->sem);
    if ( result != 0 )
    {
        YMLog("semaphore[%s,%s]: fatal: sem_wait failed: %d (%s)",semaphore->semName,semaphore->userName,errno,strerror(errno));
        abort();
    }
    YMLog("semaphore[%s,%s]: waited",semaphore->semName,semaphore->userName);
#endif
}

void YMSemaphoreSignal(YMSemaphoreRef semaphore)
{
#ifdef PTHREAD_SEMAPHORE
    YMLockLock(semaphore->lock);
    
    semaphore->value++;
    
    if ( semaphore->value <= 0 )
    {
        YMLog("semaphore[%s,%s]: signaling %p",semaphore->semName,semaphore->name,semaphore);
        int result = pthread_cond_signal(&(semaphore->cond));
        if ( result != 0 )
        {
            YMLog("semaphore[%s,%s]: fatal: pthread_cond_signal failed: %d (%s)",semaphore->semName,semaphore->name,result,strerror(result));
            abort();
        }
    }
    
    YMLockUnlock(semaphore->lock);
#else
    YMLog("semaphore[%s,%s]: posting",semaphore->semName,semaphore->userName);
    int result = sem_post(semaphore->sem);
    if ( result != 0 )
    {
        YMLog("semaphore[%s,%s]: fatal: sem_post failed: %d (%s)",semaphore->semName,semaphore->userName,errno,strerror(errno));
        abort();
    }
    YMLog("semaphore[%s,%s]: posted",semaphore->semName,semaphore->userName);
#endif
}
