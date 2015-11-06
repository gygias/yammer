//
//  YMThread.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMThread.h"
#include "YMPrivate.h"

#include "YMDictionary.h"
#include "YMSemaphore.h"
#include "YMLock.h"

#include <pthread.h>

typedef uint64_t YMThreadDispatchID;
typedef uint64_t YMThreadDispatchThreadID;

typedef struct __YMThread
{
    YMTypeID _typeID;
    
    char *name;
    ym_thread_entry entryPoint;
    void *context;
    pthread_t pthread;
    
    // dispatch stuff
    bool isDispatchThread;
    YMThreadDispatchThreadID dispatchThreadID;
    YMThreadDispatchID dispatchIDNext;
    YMSemaphoreRef dispatchSemaphore;
    YMDictionaryRef dispatchesByID;
    YMLockRef dispatchListLock;
} _YMThread;

// dispatch stuff
typedef struct
{
    YMThreadRef ymThread;
    bool *stopFlag;
} _YMThreadDispatchThreadDef;

pthread_once_t gDispatchInitOnce = PTHREAD_ONCE_INIT;
YMThreadDispatchThreadID gDispatchThreadIDNext = 0; // todo only for keying dictionary, implement linked list?
YMDictionaryRef gDispatchThreadDefsByID = NULL;
YMLockRef gDispatchThreadListLock = NULL;

// private
void _YMThreadDispatchInit();
void *_YMThreadDispatchThreadProc(void *);
uint64_t _YMThreadGetCurrentThreadNumber();
YMThreadUserDispatchRef _YMThreadCopyUserDispatch(YMThreadUserDispatchRef userDispatchRef);

YMThreadRef _YMThreadCreate(char *name, bool isDispatchThread, ym_thread_entry entryPoint, void *context)
{
    _YMThread *thread = (_YMThread *)malloc(sizeof(struct __YMThread));
    thread->_typeID = _YMThreadTypeID;
    
    pthread_once(&gDispatchInitOnce, _YMThreadDispatchInit);
    
    thread->name = strdup(name ? name : "unnamed-thread");
    thread->entryPoint = isDispatchThread ? _YMThreadDispatchThreadProc : entryPoint;
    thread->context = context;
    thread->pthread = NULL;
    
    if ( isDispatchThread )
    {
        YMLockLock(gDispatchThreadListLock);
        {
            thread->dispatchThreadID = gDispatchThreadIDNext++;
            if ( YMDictionaryContains(gDispatchThreadDefsByID, thread->dispatchThreadID) ) // either a bug or pathological
            {
                YMLog("thread[dispatch]: fatal: out of dispatch thread ids");
                abort();
            }
            
            _YMThreadDispatchThreadDef *dispatchThreadDef = (_YMThreadDispatchThreadDef *)malloc(sizeof(_YMThreadDispatchThreadDef));
            dispatchThreadDef->ymThread = thread;
            dispatchThreadDef->stopFlag = calloc(1, sizeof(bool));
            
            YMDictionaryAdd(gDispatchThreadDefsByID, thread->dispatchThreadID, dispatchThreadDef);
            
            thread->context = dispatchThreadDef;
        }
        YMLockUnlock(gDispatchThreadListLock);
        
        thread->dispatchListLock = YMLockCreate();
        thread->dispatchesByID = YMDictionaryCreate();
        thread->dispatchSemaphore = YMSemaphoreCreate();
        thread->dispatchIDNext = 0;
    }
    
    return thread;
}

void _YMThreadFree(YMTypeRef object)
{
    YMThreadRef thread = (YMThreadRef)object;
    free(thread->name);
    
    if ( thread->isDispatchThread )
    {
        YMFree(thread->dispatchListLock);
        YMFree(thread->dispatchesByID);
        YMFree(thread->dispatchSemaphore);
        
        YMLockLock(gDispatchThreadListLock);
        {
            _YMThreadDispatchThreadDef *threadDef = (_YMThreadDispatchThreadDef *)YMDictionaryGetItem(gDispatchThreadDefsByID, thread->dispatchThreadID);
            *(threadDef->stopFlag) = false;
            YMLog("thread[dispatch,%llu]: flagged pthread to exit on ymfree",thread->dispatchThreadID);
        }
        YMLockUnlock(gDispatchThreadListLock);
    }
    // todo is there anything we should reasonably do to user threads here?
    
    free(thread);
}


YMThreadRef YMThreadCreate(char *name, ym_thread_entry entryPoint, void *context)
{
    return _YMThreadCreate(name, false, entryPoint, context);
}

YMThreadRef YMThreadDispatchCreate(char *name)
{
    return _YMThreadCreate(name, true, _YMThreadDispatchThreadProc, NULL);
}

bool YMThreadStart(YMThreadRef thread)
{
    pthread_t pthread;
    int result;
    
    if ( ( result = pthread_create(&pthread, NULL, thread->entryPoint, thread->context) ) )
    {
        YMLog("thread[%s]: pthread_create %d %s", thread->isDispatchThread ? "dispatch" : "user", result, strerror(result));
        return false;
    }
    
    YMLog("thread[%s]: created", thread->isDispatchThread ? "dispatch" : "user");
    thread->pthread = pthread;
    return true;
}

bool YMThreadJoin(YMThreadRef thread)
{
    _YMThread *_thread = (_YMThread *)thread;
    int result;
    if ( ( result = pthread_join(_thread->pthread, NULL) ) )
    {
        YMLog("thread[%s]: error: pthread_join %d %s", result, strerror(result));
        return false;
    }
    
    return true;
}

typedef struct __YMThreadDispatchInternal
{
    YMThreadUserDispatchRef userDispatchRef;
    YMThreadDispatchID dispatchID;
} _YMThreadDispatchInternal;

void YMThreadDispatchDispatch(YMThreadRef thread, YMThreadUserDispatchRef userDispatch)
{
    YMLockLock(thread->dispatchListLock);
    {
        _YMThreadDispatchInternal *newDispatch = (_YMThreadDispatchInternal*)malloc(sizeof(struct __YMThreadDispatchInternal));
        YMThreadUserDispatchRef userDispatchCopy = _YMThreadCopyUserDispatch(userDispatch);
        
        newDispatch->userDispatchRef = userDispatchCopy;
        newDispatch->dispatchID = thread->dispatchIDNext++;
        
        if ( YMDictionaryContains(thread->dispatchesByID, newDispatch->dispatchID) )
        {
            YMLog("thread[dispatch,%llu]: fatal: thread is out of dispatch ids (%d)",thread->dispatchThreadID,YMDictionaryGetCount(thread->dispatchesByID));
            abort();
        }
        
        YMLog("thread[dispatch:%llu]: adding new dispatch with description '%s'",userDispatchCopy->description);
        YMDictionaryAdd(thread->dispatchesByID, newDispatch->dispatchID, newDispatch);
    }
    YMLockUnlock(thread->dispatchListLock);
    
    YMSemaphoreSignal(thread->dispatchSemaphore);
    
#warning todo vet ownership of all this stuff
}

YMThreadUserDispatchRef _YMThreadCopyUserDispatch(YMThreadUserDispatchRef userDispatchRef)
{
    YMThreadUserDispatch *copy = (YMThreadUserDispatch *)malloc(sizeof(YMThreadUserDispatch));
    copy->func = userDispatchRef->func;
    copy->description = strdup(userDispatchRef->description ? userDispatchRef->description : "unnamed-dispatch");
    copy->context = userDispatchRef->context;
    
    return copy;
}

void _YMThreadDispatchInit()
{
    gDispatchThreadDefsByID = YMDictionaryCreate();
    gDispatchThreadListLock = YMLockCreate();
}

void *_YMThreadDispatchThreadProc(void *threadDefPtr)
{
    _YMThreadDispatchThreadDef *threadDef = (_YMThreadDispatchThreadDef *)threadDefPtr;
    YMThreadRef thread = threadDef->ymThread;
    YMLog("thread[dispatch,d%llu,p%llu]: entered",thread->dispatchThreadID,_YMThreadGetCurrentThreadNumber());
    
    while ( ! threadDef->stopFlag )
    {
        YMLog("thread[dispatch,dt%llu,p%llu]: begin dispatch loop",thread->dispatchThreadID,_YMThreadGetCurrentThreadNumber());
        YMSemaphoreWait(thread->dispatchSemaphore);
        
        YMThreadDispatchID theDispatchID;
        _YMThreadDispatchInternal *theDispatch = NULL;
        YMLockLock(thread->dispatchListLock);
        {
            bool okay = YMDictionaryPopKeyValue(thread->dispatchesByID, true, &theDispatchID, (YMDictionaryValue *)&theDispatch);
            if ( ! okay )
            {
                YMLog("thread[dispatch,dt%llu,p%llu]: fatal: thread signaled without a user dispatch to execute",thread->dispatchThreadID,_YMThreadGetCurrentThreadNumber());
                abort();
            }
        }
        YMLockUnlock(thread->dispatchListLock);
        
        YMLog("thread[dispatch,dt%llu,p%llu,ud%llu]: woke for user dispatch",thread->dispatchThreadID,_YMThreadGetCurrentThreadNumber(),theDispatchID);
    }
    
    YMLockLock(gDispatchThreadListLock);
    {
#warning todo stopFlag is the only way to terminate a dispatch thread, so we should be correct to free these guys here, but make sure \
        sometime when you haven't written 500 lines in an hour.
        _YMThreadDispatchThreadDef *threadDef = (_YMThreadDispatchThreadDef *)YMDictionaryRemove(gDispatchThreadDefsByID, thread->dispatchThreadID);
        
        if ( ! threadDef )
        {
            YMLog("thread[dispatch,dt%llu,p%llu]: exiting",thread->dispatchThreadID,_YMThreadGetCurrentThreadNumber());
            abort();
        }
        
        free(threadDef->stopFlag);
        free(threadDef);
    }
    YMLockUnlock(gDispatchThreadListLock);
    
    YMLog("thread[dispatch,dt%llu,p%llu]: exiting",thread->dispatchThreadID,_YMThreadGetCurrentThreadNumber());
    return NULL;
}

// xxx i wonder if this is actually going to be portable
uint64_t _YMThreadGetCurrentThreadNumber()
{
    pthread_t pthread = pthread_self();
    uint64_t threadId = 0;
    memcpy(&threadId, &pthread, YMMIN(sizeof(threadId), sizeof(pthread)));
    return threadId;
}
