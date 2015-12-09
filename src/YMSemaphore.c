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

#define ymlog_type YMLogThreadSync
#include "YMLog.h"

#include <fcntl.h>
//#define PTHREAD_SEMAPHORE
#ifdef PTHREAD_SEMAPHORE
# include <pthread.h>
#elif !defined(WIN32)
# include <semaphore.h>
#endif

#if defined(YMLINUX)
# include <sys/stat.h>
#endif

#ifndef WIN32
# define YM_SEMAPHORE_TYPE sem_t
#else
# define YM_SEMAPHORE_TYPE HANDLE
#endif

#define YM_SEM_LOG_PREFIX "semaphore[%s,%d,%s]: "
#define YM_SEM_LOG_DESC YMSTR(semaphore->semName),(int)semaphore->sem,YMSTR(semaphore->userName)

YM_EXTERN_C_PUSH

typedef struct __ym_semaphore
{
    _YMType _typeID;
    
    YMStringRef userName;
    YMStringRef semName;
    YMLockRef lock;
    YM_SEMAPHORE_TYPE *sem;
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
    ymlog(YM_SEM_LOG_PREFIX "created",YM_SEM_LOG_DESC);
    YMLockUnlock(gYMSemaphoreIndexLock);

#ifndef WIN32
try_again:;
    semaphore->sem = sem_open(YMSTR(semaphore->semName), O_CREAT|O_EXCL, S_IRUSR|S_IWUSR, initialValue); // todo mode?
    if ( semaphore->sem == SEM_FAILED )
    {
        if ( errno == EEXIST )
        {
            if ( sem_unlink(YMSTR(semaphore->semName)) == 0 )
            {
                ymlog(YM_SEM_LOG_PREFIX "exists",YM_SEM_LOG_DESC);
                goto try_again;
            }
            else
			{
				ymerr(YM_SEM_LOG_PREFIX "sem_unlink failed %d (%s)",YM_SEM_LOG_DESC,errno,strerror(errno));
				goto try_again;
			}
        }
        else
            ymabort(YM_SEM_LOG_PREFIX "fatal: sem_open failed: %d (%s)",YM_SEM_LOG_DESC,errno,strerror(errno));
    }
#else
	semaphore->sem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
	if (semaphore->sem == NULL)
		ymabort(YM_SEM_LOG_PREFIX "fatal: CreateSemaphore failed: %x", YM_SEM_LOG_DESC, GetLastError());
#endif
    
    return (YMSemaphoreRef)semaphore;
}

void _YMSemaphoreFree(YMTypeRef object)
{
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)object;
    ymlog(YM_SEM_LOG_PREFIX "deallocating",YM_SEM_LOG_DESC);
    
    int result, error = 0;
    const char *errorStr = NULL;
	YM_CLOSE_SEMAPHORE(semaphore);
	if (result == -1)
		ymerr(YM_SEM_LOG_PREFIX "warning: sem_unlink failed: %d (%s)", YM_SEM_LOG_DESC, error, errorStr);
    
    YMRelease(semaphore->lock);
    YMRelease(semaphore->userName);
    YMRelease(semaphore->semName);
}

void YMSemaphoreWait(YMSemaphoreRef semaphore_)
{
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)semaphore_;
    
    bool retry = true;
    while ( retry )
    {
        int result, error = 0;
        const char *errorStr = NULL;
        YM_WAIT_SEMAPHORE(semaphore->sem);
        if (result != 0)
        {
            retry = YM_RETRY_SEMAPHORE;
            ymerr(YM_SEM_LOG_PREFIX "sem_wait failed%s: %d (%s)", YM_SEM_LOG_DESC, retry ? ", retrying" : "", errno, strerror(errno));
            ymassert(retry,"sem_wait");
        }
        else
            break;
    }

	ymlog(YM_SEM_LOG_PREFIX "waited!->",YM_SEM_LOG_DESC);
}

void YMSemaphoreSignal(YMSemaphoreRef semaphore_)
{
    __YMSemaphoreRef semaphore = (__YMSemaphoreRef)semaphore_;
    
	int result, error = 0;
	const char *errorStr = NULL;
	YM_POST_SEMAPHORE(semaphore->sem);
	ymassert(result==0, YM_SEM_LOG_PREFIX "fatal: sem_post failed: %d (%s)", YM_SEM_LOG_DESC, errno, strerror(errno));
	
	ymlog(YM_SEM_LOG_PREFIX "posted", YM_SEM_LOG_DESC);
}

YM_EXTERN_C_POP
