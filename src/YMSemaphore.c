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

#include <fcntl.h>
//#define PTHREAD_SEMAPHORE
#ifdef PTHREAD_SEMAPHORE
# include <pthread.h>
#elif !defined(YMWIN32)
# include <semaphore.h>
#endif

#if defined(YMLINUX)
# include <sys/stat.h>
#endif

#if !defined(YMWIN32)
# define YM_SEMAPHORE_TYPE sem_t
#else
# define YM_SEMAPHORE_TYPE HANDLE
#endif

#define ymlog_pre "sem[%s,%d,%s]: "
#define ymlog_args YMSTR(s->semName),(int)s->sem,YMSTR(s->userName)
#define ymlog_type YMLogThreadSync
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_semaphore
{
    _YMType _typeID;
    
    YMStringRef userName;
    YMStringRef semName;
    YM_SEMAPHORE_TYPE *sem;
} ___ym_semaphore;
typedef struct __ym_semaphore __ym_semaphore_t;

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
        ymabort("fatal: semaphore initial value cannot be negative");

	YM_ONCE_DO_LOCAL(__YMSemaphoreInit);
    
    __ym_semaphore_t *s = (__ym_semaphore_t *)_YMAlloc(_YMSemaphoreTypeID,sizeof(__ym_semaphore_t));
    
    s->userName = YMStringCreateWithFormat("%s-%p",name?YMSTR(name):"*",s, NULL);
    
    YMLockLock(gYMSemaphoreIndexLock);
    uint16_t thisIndex = gYMSemaphoreIndex++;
    s->semName = YMStringCreateWithFormat("ym-%u",thisIndex,NULL);
    if ( gYMSemaphoreIndex == 0 )
        ymerr("warning: semaphore name index reset");
    ymlog("created");
    YMLockUnlock(gYMSemaphoreIndexLock);

#if !defined(YMWIN32)
try_again:;
    s->sem = sem_open(YMSTR(s->semName), O_CREAT|O_EXCL, S_IRUSR|S_IWUSR, initialValue); // todo mode?
    if ( s->sem == SEM_FAILED ) {
        if ( errno == EEXIST ) {
            if ( sem_unlink(YMSTR(s->semName)) == 0 ) {
                ymlog("exists");
                goto try_again;
            } else {
				ymerr("sem_unlink failed %d (%s)",errno,strerror(errno));
				goto try_again;
			}
        }
        else
            ymabort("fatal: sem_open failed: %d (%s)",errno,strerror(errno));
    }
#else
	s->sem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
	if (s->sem == NULL)
		ymabort("fatal: CreateSemaphore failed: %x", GetLastError());
#endif
    
    return s;
}

void _YMSemaphoreFree(YMSemaphoreRef s)
{
    ymlog("deallocating");
    
    int result, error = 0;
    const char *errorStr = NULL;
	YM_CLOSE_SEMAPHORE(s);
	if (result == -1)
		ymerr("warning: sem_unlink failed: %d (%s)", error, errorStr);
    
    YMRelease(s->userName);
    YMRelease(s->semName);
}

void YMSemaphoreWait(__ym_semaphore_t *s)
{
    bool retry = true;
    while ( retry ) {
        int result, error = 0;
        const char *errorStr = NULL;
        YM_WAIT_SEMAPHORE(s->sem);
        if (result != 0) {
            retry = YM_RETRY_SEMAPHORE;
            ymerr("sem_wait failed%s: %d (%s)", retry ? ", retrying" : "", errno, strerror(errno));
            ymassert(retry,"sem_wait");
        }
        else
            break;
    }

	ymlog("released");
}

void YMSemaphoreSignal(__ym_semaphore_t *s)
{
	int result, error = 0;
	const char *errorStr = NULL;
	YM_POST_SEMAPHORE(s->sem);
	ymassert(result==0, "fatal: sem_post failed: %d (%s)", errno, strerror(errno));
	
	ymlog("posted");
}

YM_EXTERN_C_POP
