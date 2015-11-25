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
#elif !defined(_WINDOWS)
#include <semaphore.h>
#endif

typedef struct __ym_semaphore
{
    _YMType _typeID;
    
    YMStringRef userName;
    YMStringRef semName;
    YMLockRef lock;
    
#ifndef _WINDOWS
#define YM_SEMAPHORE_TYPE sem_t
#else
#define YM_SEMAPHORE_TYPE HANDLE
#endif

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

void _YMSemaphoreInit()
{
    YMStringRef name = YMSTRC(YM_TOKEN_STR(gYMSemaphoreIndex));
    gYMSemaphoreIndexLock = YMLockCreateWithOptionsAndName(YMInternalLockType, name);
    YMRelease(name);
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

#ifndef _WINDOWS
	static pthread_once_t gYMSemaphoreIndexInit = PTHREAD_ONCE_INIT;
	pthread_once(&gYMSemaphoreIndexInit, _YMSemaphoreInit);
#else
	static INIT_ONCE gYMSemaphoreIndexInit = INIT_ONCE_STATIC_INIT;
	InitOnceExecuteOnce(&gYMSemaphoreIndexInit, (PINIT_ONCE_FN)_YMSemaphoreInit, NULL, NULL);
#endif
    
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)_YMAlloc(_YMSemaphoreTypeID,sizeof(__YMSemaphore));
    
    semaphore->userName = YMStringCreateWithFormat("%s-%p",name?YMSTR(name):"*",semaphore, NULL);
    YMStringRef memberName = YMSTRC("__ymsemaphore_mutex");
    semaphore->lock = YMLockCreateWithOptionsAndName(YMInternalLockType, memberName);
    YMRelease(memberName);
    
    YMLockLock(gYMSemaphoreIndexLock);
    uint16_t thisIndex = gYMSemaphoreIndex++;
    semaphore->semName = YMStringCreateWithFormat("ym-%u",thisIndex,NULL);
    if ( gYMSemaphoreIndex == 0 )
        ymerr("semaphore[%s,%s]: warning: semaphore name index reset",YMSTR(semaphore->semName),YMSTR(semaphore->userName),NULL);
    ymlog("semaphore[%s,%s]: created",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
    YMLockUnlock(gYMSemaphoreIndexLock);
    
#ifdef PTHREAD_SEMAPHORE
    semaphore->cond = cond;
    semaphore->value = initialValue;
#elif !defined(_WINDOWS)
    
try_again:;
    semaphore->sem = sem_open(YMSTR(semaphore->semName), O_CREAT|O_EXCL, S_IRUSR|S_IWUSR, initialValue); // todo mode?
    if ( semaphore->sem == SEM_FAILED )
    {
        if ( errno == EEXIST )
        {
            if ( sem_unlink(YMSTR(semaphore->semName)) == 0 )
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
        else
            ymerr("semaphore[%s,%s]: fatal: sem_open failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->userName),errno,strerror(errno));
        abort(); // since we handle names internally
    }
#else
	semaphore->sem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
	if ( semaphore->sem == NULL )
	{
		ymerr("semaphore[%s,%s]: fatal: CreateSemaphore failed: %x", YMSTR(semaphore->semName), YMSTR(semaphore->userName), GetLastError());
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
        ymerr("semaphore[%s,%s]: fatal: pthread_cond_destroy failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->userName),result,strerror(result));
        abort();
    }
#elif !defined(_WINDOWS)
    int result = sem_unlink(YMSTR(semaphore->semName));
    if ( result == -1 )
        ymerr("semaphore[%s,%s]: warning: sem_unlink failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->userName),errno,strerror(errno));
#else
	BOOL okay = CloseHandle(semaphore->sem);
	if ( ! okay )
		ymerr("semaphore[%s,%s]: warning: CloseHandle failed: %x", YMSTR(semaphore->semName), YMSTR(semaphore->userName), GetLastError());
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
        ymlog("semaphore[%s,%s]: waiting on %p...",YMSTR(semaphore->name),semaphore);
        int result = pthread_cond_wait(&semaphore->cond, mutex);
        if ( result != 0 )
        {
            ymerr("semaphore[%s,%s]: fatal: pthread_cond_wait failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->name),result,strerror(result));
            abort();
        }
        ymlog("semaphore[%s,%s]: received signal %p...",YMSTR(semaphore->semName),YMSTR(semaphore->name),semaphore);
    }
    
    YMLockUnlock(semaphore->lock);
#elif !defined(_WINDOWS)
sem_retry:
    ymlog("semaphore[%s,%s]: waiting",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
    int result = sem_wait(semaphore->sem);
    if ( result != 0 )
    {
        bool retry = ( errno == EINTR );
        ymerr("semaphore[%s,%s]: sem_wait failed%s: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->userName),retry?", retrying":"",errno,strerror(errno));
        if ( retry )
            goto sem_retry;
        abort();
    }
    ymlog("semaphore[%s,%s]: waited!->",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
#else
	DWORD result = WaitForSingleObject(semaphore->sem, INFINITE);
	if ( result != WAIT_OBJECT_0 )
	{
		ymerr("semaphore[%s,%s]: sem_wait failed: %x", YMSTR(semaphore->semName), YMSTR(semaphore->userName), result);
		abort();
	}
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
#elif !defined(_WINDOWS)
    ymlog("semaphore[%s,%s]: posting",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
    int result = sem_post(semaphore->sem);
    if ( result != 0 )
    {
        ymerr("semaphore[%s,%s]: fatal: sem_post failed: %d (%s)",YMSTR(semaphore->semName),YMSTR(semaphore->userName),errno,strerror(errno));
        abort();
    }
    ymlog("semaphore[%s,%s]: posted",YMSTR(semaphore->semName),YMSTR(semaphore->userName));
#else
	BOOL okay = ReleaseSemaphore(semaphore->sem, 1, NULL);
	if ( ! okay )
	{
		ymerr("semaphore[%s,%s]: fatal: ReleaseSemaphore failed: %x", YMSTR(semaphore->semName), YMSTR(semaphore->userName), GetLastError());
		abort();
	}
#endif
}
