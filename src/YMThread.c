//
//  YMThread.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMThread.h"
#include "YMThreadPriv.h"

#include "YMUtilities.h"
#include "YMDictionary.h"
#include "YMSemaphore.h"
#include "YMLock.h"

#if !defined(YMWIN32)
# include <pthread.h>
# define YM_THREAD_TYPE pthread_t
#else
# define YM_THREAD_TYPE HANDLE
#endif

#define ymlog_pre "ymthread[%lu]: "
#define ymlog_args _YMThreadGetCurrentThreadNumber()
#define ymlog_type YMLogThread
#define ymlog_type_debug YMLogThreadDebug
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_thread
{
    _YMType _typeID;
    
    YMStringRef name;
    ym_entry_point userEntryPoint;
    void *context;
    YM_THREAD_TYPE pthread;
    uint64_t threadId;
    YMDictionaryRef threadDict;
    
    // thread state
    bool didStart;
} __ym_thread;
typedef struct __ym_thread __ym_thread_t;

#define YMThreadDictThreadIDKey "thread-id"

__ym_thread_t * __YMThreadInitCommon(YMStringRef, void *);
void __YMThreadInitThreadDict(__ym_thread_t *);
YM_THREAD_TYPE _YMThreadGetCurrentThread(void);
YM_ENTRY_POINT(__ym_thread_generic_entry_proc);


__ym_thread_t *__YMThreadInitCommon(YMStringRef name, void *context)
{
    __ym_thread_t *t = (__ym_thread_t *)_YMAlloc(_YMThreadTypeID,sizeof(__ym_thread_t));
    
    t->name = name ? YMRetain(name) : YMSTRC("*");
    t->context = context;
    t->pthread = (YM_THREAD_TYPE)NULL;
    t->threadId = 0;
    t->threadDict = YMDictionaryCreate();
    
    t->didStart = false;
    
    return t;
}

YMThreadRef YMThreadCreate(YMStringRef name, ym_entry_point entryPoint, void *context)
{
    __ym_thread_t *t = __YMThreadInitCommon(name, context);
    t->userEntryPoint = entryPoint;
    
    return t;
}

void _YMThreadFree(YMTypeRef o_)
{
    __ym_thread_t *t = (__ym_thread_t *)o_;
    if ( t->threadDict )
        YMRelease(t->threadDict);
    YMRelease(t->name);
}

bool YMThreadStart(YMThreadRef t_)
{
    __ym_thread_t *t = (__ym_thread_t *)t_;
    
    YMRetain(t); // handle (normal) thread completing (and finalizing) before this method returns
    bool okay = false;
    
    YM_THREAD_TYPE pthread;
    
    const void *context = YMRetain(t);
    ym_entry_point entry = __ym_thread_generic_entry_proc;
    
#if !defined(YMWIN32)
	int result;
    if ( 0 != ( result = pthread_create(&pthread, NULL, (void *(*)(void *))entry, (void *)context) ) ) {
        ymerr("pthread_create %d %s", result, strerror(result));
        goto catch_return;
    }
    t->threadId = (uint64_t)pthread;
#else
	DWORD threadId;
	pthread = CreateThread(NULL, 0, entry, (LPVOID)context, 0, &threadId);
    if ( pthread == NULL ) {
		ymerr("CreateThread failed: %x", GetLastError());
        goto catch_return;
	}
    t->threadId = threadId;
#endif
    
    ymlog("detached");
    
    t->pthread = pthread;
    okay = true;
    
catch_return:
    YMRelease(t);
    return okay;
}

YM_ENTRY_POINT(__ym_thread_generic_entry_proc)
{
    __ym_thread_t *t = context;
    
    __YMThreadInitThreadDict(t);
    
    ymlog("entered");
    t->userEntryPoint((void *)t->context);
    ymlog("exiting");
    YMRelease(t);
}

void __YMThreadInitThreadDict(__ym_thread_t *t)
{
    YMDictionaryAdd(t->threadDict, (YMDictionaryKey)YMThreadDictThreadIDKey, (YMDictionaryValue)_YMThreadGetCurrentThreadNumber());
}

bool YMThreadJoin(YMThreadRef t)
{    
    if ( _YMThreadGetCurrentThreadNumber() == _YMThreadGetThreadNumber(t) )
        return false;
    
#if !defined(YMWIN32)
    int result = pthread_join(t->pthread, NULL);
    if ( result != 0 ) {
        ymerr("pthread_join %d %s", result, strerror(result));
        return false;
    }
#else
	DWORD result = WaitForSingleObject(t->pthread, INFINITE);
    if ( result != WAIT_OBJECT_0 ) {
		ymerr("WaitForSingleObject %x", result);
		return false;
	}
#endif
    
    return true;
}

YM_THREAD_TYPE __YMThreadGetCurrentThread(void)
{
#if defined(YMWIN32)
    return GetCurrentThread();
#else
    return pthread_self();
#endif
}

uint64_t _YMThreadGetCurrentThreadNumber(void)
{
    return (uint64_t)__YMThreadGetCurrentThread();
}

uint64_t _YMThreadGetThreadNumber(YMThreadRef t)
{
    return t->threadId;
}

YM_EXTERN_C_POP
