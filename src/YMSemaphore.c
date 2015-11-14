//
//  YMSemaphore.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSemaphore.h"

#include "YMUtilities.h"
#include "YMLock.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogThreadSync
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#include <fcntl.h>
//#define PTHREAD_SEMAPHORE
#ifdef PTHREAD_SEMAPHORE
#include <pthread.h>
#else
#include <semaphore.h>
#endif

typedef struct __ym_semaphore
{
    _YMType _typeID;
    
    YMStringRef userName;
    YMStringRef semName;
    YMLockRef lock;
    
#ifdef PTHREAD_SEMAPHORE
    pthread_cond_t cond;
    int value;
#else
    sem_t *sem;
#endif
} ___ym_semaphore;
typedef struct __ym_semaphore __YMSemaphore;
typedef __YMSemaphore *__YMSemaphoreRef;

uint16_t gYMSemaphoreIndex = 40;
YMLockRef gYMSemaphoreIndexLock = NULL;
pthread_once_t gYMSemaphoreIndexInit = PTHREAD_ONCE_INIT;

void _YMSemaphoreInit()
{
    gYMSemaphoreIndexLock = YMLockCreateWithOptionsAndName(YMLockDefault, YM_TOKEN_STR(gYMSemaphoreIndex));
}

YMSemaphoreRef YMSemaphoreCreate(YMStringRef name, int initialValue)
{
    if (initialValue < 0)
    {
        ymerr("semaphore[init]: fatal: initial value cannot be negative");
        abort();
    }
    
#ifdef PTHREAD_SEMAPHORE
    pthread_cond_t cond;
    int result = pthread_cond_init(&cond, NULL); // "FreeBSD doesn't support non-default attributes"
    if ( result != 0 )
    {
        ymerr("semaphore[init]: fatal: pthread_cond_init failed: %d (%s)",result,strerror(errno));
        return NULL;
    }
#endif
    
    pthread_once(&gYMSemaphoreIndexInit, &_YMSemaphoreInit);
    
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)_YMAlloc(_YMSemaphoreTypeID,sizeof(__YMSemaphore));
    
    semaphore->userName = YMStringCreateWithFormat("%s-%p",name?YMSTR(name):"unnamed",semaphore, NULL);
    semaphore->lock = YMLockCreateWithOptionsAndName(YMLockDefault, "__ymsemaphore_mutex");
    
    YMLockLock(gYMSemaphoreIndexLock);
    uint16_t thisIndex = gYMSemaphoreIndex++;
    if ( gYMSemaphoreIndex == 0 )
        ymerr("semaphore[%s,%s]: warning: semaphore name index reset",YMSTR(semaphore->semName),YMSTR(semaphore->userName),NULL);
    semaphore->semName = YMStringCreateWithFormat("ym-%u",thisIndex,NULL);
    ymlog("semaphore[%s,%s]: created",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
    YMLockUnlock(gYMSemaphoreIndexLock);
    
#ifdef PTHREAD_SEMAPHORE
    semaphore->cond = cond;
    semaphore->value = initialValue;
#else
    bool triedUnlink = false;
    
try_again:;
    int open_error;
    semaphore->sem = sem_open(semaphore->semName, O_CREAT|O_EXCL, S_IRUSR|S_IWUSR, initialValue); // todo mode?
    if ( semaphore->sem == SEM_FAILED )
    {
        open_error = errno;
        if ( errno == EEXIST )
        {
            triedUnlink = true;
            if ( sem_unlink(semaphore->semName) == 0 )
            {
                ymerr("sem_unlink[%s]",YMSTR(semaphore->semName));
                goto try_again;
            }
            else
            {
                ymerr("sem_unlink[%s] failed: %d (%s)",YMSTR(semaphore->semName),errno,strerror(errno));
                abort();
            }
        }
        ymlog("semaphore[%s,%s]: fatal: sem_open failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->userName),open_error,strerror(open_error));
        abort(); // since we handle names internally
    }
#endif
    
    return semaphore;
}

void _YMSemaphoreFree(YMTypeRef object)
{
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)object;
    ymlog("semaphore[%s,%s]: deallocating",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
    
#ifdef PTHREAD_SEMAPHORE
    int result = pthread_cond_destroy(&semaphore->cond);
    if ( result != 0 )
    {
        ymerr("semaphore[%s,%s]: fatal: pthread_cond_destroy failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->userName),result,strerror(result));
        abort();
    }
#else
    int result = sem_unlink(semaphore->semName);
    if ( result == -1 )
        ymerr("semaphore[%s,%s]: warning: sem_unlink failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->userName),errno,strerror(errno));
#endif
    
    YMRelease(semaphore->lock);
    YMRelease(semaphore->userName);
    YMRelease(semaphore->semName);
}

void YMSemaphoreWait(YMSemaphoreRef semaphore_)
{
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)semaphore_;
    
#ifdef PTHREAD_SEMAPHORE
    YMLockLock(semaphore->lock);
    
    semaphore->value--;
    if ( semaphore->value < 0 )
    {
        pthread_mutex_t mutex = _YMLockGetMutex(semaphore->lock);
        ymlog("semaphore[%s,%s]: waiting on %p...",YMSTR(semaphore->name),semaphore);
        int result = pthread_cond_wait(&semaphore->cond, &mutex);
        if ( result != 0 )
        {
            ymerr("semaphore[%s,%s]: fatal: pthread_cond_wait failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->name),result,strerror(result));
            abort();
        }
        ymlog("semaphore[%s,%s]: received signal %p...",YMSTR(semaphore->semName),YMSTR(semaphore->name),semaphore);
    }
    
    YMLockUnlock(semaphore->lock);
#else
    ymlog("semaphore[%s,%s]: waiting",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
    int result = sem_wait(semaphore->sem);
    if ( result != 0 )
    {
        ymerr("semaphore[%s,%s]: fatal: sem_wait failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->userName),errno,strerror(errno));
        abort();
    }
    ymlog("semaphore[%s,%s]: waited!->",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
#endif
}

void YMSemaphoreSignal(YMSemaphoreRef semaphore_)
{
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)semaphore_;
    
#ifdef PTHREAD_SEMAPHORE
    YMLockLock(semaphore->lock);
    
    semaphore->value++;
    
    if ( semaphore->value <= 0 )
    {
        ymlog("semaphore[%s,%s]: signaling %p",YMSTR(semaphore->semName),YMSTR(semaphore->name),semaphore);
        int result = pthread_cond_signal(&(semaphore->cond));
        if ( result != 0 )
        {
            ymerr("semaphore[%s,%s]: fatal: pthread_cond_signal failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->name),result,strerror(result));
            abort();
        }
    }
    
    YMLockUnlock(semaphore->lock);
#else
    ymlog("semaphore[%s,%s]: posting",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
    int result = sem_post(semaphore->sem);
    if ( result != 0 )
    {
        ymerr("semaphore[%s,%s]: fatal: sem_post failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->userName),errno,strerror(errno));
        abort();
    }
    ymlog("semaphore[%s,%s]: posted",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
#endif
}
