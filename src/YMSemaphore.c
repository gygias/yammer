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
#elif !defined(WIN32)
#include <semaphore.h>
#endif

#if defined(RPI)
#include <sys/stat.h>
#endif

typedef struct __ym_semaphore
{
    _YMType _typeID;
    
    YMStringRef userName;
    YMStringRef semName;
    YMLockRef lock;
    
#ifndef WIN32
#define YM_SEMAPHORE_TYPE sem_t
#else
#define YM_SEMAPHORE_TYPE HANDLE
#endif
    
#define YM_SEM_LOG_PREFIX "semaphore[%s,%d,%s]: "
#define YM_SEM_LOG_DESC YMSTR(semaphore->semName),(int)semaphore->sem,YMSTR(semaphore->userName)

#ifdef PTHREAD_SEMAPHORE
    pthread_cond_t cond;
    int value;
#else
    YM_SEMAPHORE_TYPE *sem;
#endif
} ___ym_semaphore;
typedef struct __ym_semaphore __YMSemaphore;
typedef __YMSemaphore *__YMSemaphoreRef;

uint16_t gYMSemaphoreIndex = 40;
YMLockRef gYMSemaphoreIndexLock = NULL;

YMSemaphoreRef __YMSemaphoreCreate(YMStringRef name, int initialValue);

YM_ONCE_FUNC(__YMSemaphoreInit,
{
    YMStringRef name = YMSTRC(YM_TOKEN_STR(gYMSemaphoreIndex));
    gYMSemaphoreIndexLock = YMLockCreateWithOptionsAndName(YMInternalLockType, name);
    YMRelease(name);
})

YMSemaphoreRef YMSemaphoreCreate(int initialValue)
{
    return __YMSemaphoreCreate(NULL, initialValue);
}

YMSemaphoreRef YMSemaphoreCreateWithName(YMStringRef name, int initialValue)
{
    return __YMSemaphoreCreate(name, initialValue);
}

YMSemaphoreRef __YMSemaphoreCreate(YMStringRef name, int initialValue)
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

	YM_ONCE_DO_LOCAL(__YMSemaphoreInit);
    
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)_YMAlloc(_YMSemaphoreTypeID,sizeof(__YMSemaphore));
    
    semaphore->userName = YMStringCreateWithFormat("%s-%p",name?YMSTR(name):"*",semaphore, NULL);
    YMStringRef memberName = YMSTRC("__ymsemaphore_mutex");
    semaphore->lock = YMLockCreateWithOptionsAndName(YMInternalLockType, memberName);
    YMRelease(memberName);
    
    YMLockLock(gYMSemaphoreIndexLock);
    uint16_t thisIndex = gYMSemaphoreIndex++;
    semaphore->semName = YMStringCreateWithFormat("ym-%u",thisIndex,NULL);
    if ( gYMSemaphoreIndex == 0 )
        ymerr(YM_SEM_LOG_PREFIX "warning: semaphore name index reset",YM_SEM_LOG_DESC);
    ymlog("semaphore[%s,%s]: created",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
    YMLockUnlock(gYMSemaphoreIndexLock);
    
#ifdef PTHREAD_SEMAPHORE
    semaphore->cond = cond;
    semaphore->value = initialValue;
#elif !defined(WIN32)
    
try_again:;
    semaphore->sem = sem_open(YMSTR(semaphore->semName), O_CREAT|O_EXCL, S_IRUSR|S_IWUSR, initialValue); // todo mode?
    if ( semaphore->sem == SEM_FAILED )
    {
        if ( errno == EEXIST )
        {
            if ( sem_unlink(YMSTR(semaphore->semName)) == 0 )
            {
                ymerr(YM_SEM_LOG_PREFIX "exists",YM_SEM_LOG_DESC);
                goto try_again;
            }
            else
            {
                ymerr(YM_SEM_LOG_PREFIX "failed: %d (%s)",YM_SEM_LOG_DESC,errno,strerror(errno));
                abort();
            }
        }
        else
            ymerr(YM_SEM_LOG_PREFIX "fatal: sem_open failed: %d (%s)",YM_SEM_LOG_DESC,errno,strerror(errno));
        abort(); // since we handle names internally
    }
#else
	semaphore->sem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
	if ( semaphore->sem == NULL )
	{
		ymerr(YM_SEM_LOG_PREFIX "fatal: CreateSemaphore failed: %x",YM_SEM_LOG_DESC, GetLastError());
		abort();
	}
#endif
    
    return (YMSemaphoreRef)semaphore;
}

void _YMSemaphoreFree(YMTypeRef object)
{
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)object;
    ymlog("semaphore[%s,%s]: deallocating",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
    
#ifdef PTHREAD_SEMAPHORE
    int result = pthread_cond_destroy(&semaphore->cond);
    if ( result != 0 )
    {
        ymerr(YM_SEM_LOG_PREFIX "fatal: pthread_cond_destroy failed: %d (%s)",YM_SEM_LOG_DESC,result,strerror(result));
        abort();
    }
#else
    int result, error = 0;
    char *errorStr = NULL;
	YM_CLOSE_SEMAPHORE(semaphore);
	if (result == -1)
		ymerr(YM_SEM_LOG_PREFIX "warning: sem_unlink failed: %d (%s)", YM_SEM_LOG_DESC, error, errorStr);
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
        pthread_mutex_t *mutex = _YMLockGetMutex(semaphore->lock);
        ymlog(YM_SEM_LOG_PREFIX "waiting on %p...",YM_SEM_LOG_DESC,semaphore);
        int result = pthread_cond_wait(&semaphore->cond, mutex);
        if ( result != 0 )
        {
            ymerr(YM_SEM_LOG_PREFIX "fatal: pthread_cond_wait failed: %d (%s)",YM_SEM_LOG_DESC,result,strerror(result));
            abort();
        }
        ymlog(YM_SEM_LOG_PREFIX "received signal %p...",YM_SEM_LOG_DESC,semaphore);
    }
    
    YMLockUnlock(semaphore->lock);
#else

sem_retry:;
    
    int result, error = 0;
    char *errorStr = NULL;
	YM_WAIT_SEMAPHORE(semaphore);
	if (result != 0)
	{
        bool retry = YM_RETRY_SEMAPHORE;
		ymerr(YM_SEM_LOG_PREFIX "sem_wait failed%s: %d (%s)", YM_SEM_LOG_DESC, retry ? ", retrying" : "", errno, strerror(errno));
		if (retry)
			goto sem_retry;
		abort();
	}
#endif

	ymlog(YM_SEM_LOG_PREFIX "waited!->",YM_SEM_LOG_DESC);
}

void YMSemaphoreSignal(YMSemaphoreRef semaphore_)
{
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)semaphore_;
    
#ifdef PTHREAD_SEMAPHORE
    YMLockLock(semaphore->lock);
    
    semaphore->value++;
    
    if ( semaphore->value <= 0 )
    {
        ymlog(YM_SEM_LOG_PREFIX "signaling %p",YM_SEM_LOG_DESC,semaphore);
        int result = pthread_cond_signal(&(semaphore->cond));
        if ( result != 0 )
        {
            ymerr(YM_SEM_LOG_PREFIX "fatal: pthread_cond_signal failed: %d (%s)", YM_SEM_LOG_DESC, result, strerror(result));
            abort();
        }
    }
    
    YMLockUnlock(semaphore->lock);
#else
#ifndef WIN32
#else
#endif
	int result, error = 0;
	char *errorStr = NULL;
	YM_POST_SEMAPHORE(semaphore);
	if ( result != 0 )
	{
		ymerr(YM_SEM_LOG_PREFIX "fatal: sem_post failed: %d (%s)", YM_SEM_LOG_DESC, errno, strerror(errno));
		abort();
	}
	ymlog(YM_SEM_LOG_PREFIX "posted", YM_SEM_LOG_DESC);
#endif
}
