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
# include <sys/time.h>
# include <time.h>
#endif

#if !defined(YMWIN32)
# define YM_SEMAPHORE_TYPE sem_t
#else
# define YM_SEMAPHORE_TYPE HANDLE
#endif

// dispatch circular dependency
//#define ymlog_pre "sem[%s,%d,%s]: "
//#define ymlog_args YMSTR(s->semName),(int)s->sem,YMSTR(s->userName)
//#define ymlog_type YMLogThreadSync
//#include "YMLog.h"

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
    if ( initialValue < 0 ) {
        printf("fatal: semaphore initial value cannot be negative\n");
        abort();
    }

	YM_ONCE_DO_LOCAL(__YMSemaphoreInit);
    
    __ym_semaphore_t *s = (__ym_semaphore_t *)_YMAlloc(_YMSemaphoreTypeID,sizeof(__ym_semaphore_t));
    
    s->userName = YMStringCreateWithFormat("%s-%p",name?YMSTR(name):"*",s, NULL);
    
    YMLockLock(gYMSemaphoreIndexLock);
    uint32_t thisIndex = gYMSemaphoreIndex++;
    s->semName = YMStringCreateWithFormat("ym-%u",thisIndex,NULL);
    if ( gYMSemaphoreIndex == 0 )
        printf("warning: semaphore name index reset\n");
    YMLockUnlock(gYMSemaphoreIndexLock);

#if !defined(YMWIN32)
try_again:; // XXX
    s->sem = sem_open(YMSTR(s->semName), O_CREAT|O_EXCL, S_IRUSR|S_IWUSR, initialValue); // todo mode?
    if ( s->sem == SEM_FAILED ) {
        if ( errno == EEXIST ) {
            if ( sem_unlink(YMSTR(s->semName)) == 0 ) {
                //printf("error: sem_unlink(%s) %s exists\n",YMSTR(s->semName),YMSTR(s->userName));
                goto try_again;
            } else {
				printf("sem_unlink(%s) %s failed %d (%s)\n",YMSTR(s->semName),YMSTR(s->userName),errno,strerror(errno));
				goto try_again;
			}
        }
        else {
            printf("fatal: sem_open(%s) %s failed: %d (%s)\n",YMSTR(s->semName),YMSTR(s->userName),errno,strerror(errno));
            abort();
        }
    }
#else
	s->sem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
	if ( s->sem == NULL ) {
		printf("fatal: CreateSemaphore failed: %x\n", GetLastError());
        abort();
    }
#endif
    
    return s;
}

void _YMSemaphoreFree(YMSemaphoreRef s)
{
    YM_IO_BOILERPLATE

#if defined(YMAPPLE)
#warning i believe sem_unlink is/was 10 years ago what you were supposed to do on macos with named semaphores, \
        no idea if that's still the case, on my current debian-based it leaks open files, though the man page isn't specific
    YM_UNLINK_SEMAPHORE(s);
#else
	YM_CLOSE_SEMAPHORE(s);
#endif
	if ( result != 0 )
		printf("warning: YM_CLOSE_SEMAPHORE(?) %s %s failed: %d (%s)\n",YMSTR(s->semName),YMSTR(s->userName), error, errorStr);
    
    YMRelease(s->userName);
    YMRelease(s->semName);
}

void YMSemaphoreWait(__ym_semaphore_t *s)
{
    YM_IO_BOILERPLATE

    bool retry = true;
    while ( retry ) {
        YM_WAIT_SEMAPHORE(s->sem);
        if (result != 0) {
            retry = YM_RETRY_SEMAPHORE;
            printf("sem_wait(%s) %s failed%s: %d (%s)\n",YMSTR(s->semName),YMSTR(s->userName), retry ? ", retrying" : "", errno, strerror(errno));
            abort();
        } else
            break;
    }
}

bool YMSemaphoreTest(__ym_semaphore_t *s)
{
    YM_IO_BOILERPLATE

    YM_TRYWAIT_SEMAPHORE(s->sem);
    if ( result != 0 ) {
        if (error != EAGAIN) {
            printf("trywait sem(%s) %s failed, but errno is %d (%s)\n",YMSTR(s->semName),YMSTR(s->userName),error,errorStr);
            abort();
        }
        return false;
    }

    YM_POST_SEMAPHORE(s->sem);
	if ( result != 0 ) {
        printf("fatal: sem trywait(%s) %s repost failed: %d (%s)\n",YMSTR(s->semName),YMSTR(s->userName), error, errorStr);
        abort();
    }
    return true;
}

void YMSemaphoreSignal(__ym_semaphore_t *s)
{
    YM_IO_BOILERPLATE

	YM_POST_SEMAPHORE(s->sem);
	if ( result != 0 ) {
        printf("fatal: sem_post(%s) %s failed: %d (%s)\n",YMSTR(s->semName),YMSTR(s->userName), error, errorStr);
        abort();
    }
}

YM_EXTERN_C_POP
