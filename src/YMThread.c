//
//  YMThread.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMUtilities.h"

#include "YMThread.h"
#include "YMDictionary.h"
#include "YMSemaphore.h"
#include "YMLock.h"

#include <pthread.h>

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogThread
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

typedef uint64_t YMThreadDispatchID;
typedef uint64_t YMThreadDispatchThreadID;

typedef struct __YMThread
{
    YMTypeID _typeID;
    
    char *name;
    ym_thread_entry entryPoint;
    void *context;
    pthread_t pthread;
    
    // thread state
    bool didStart;
    
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

typedef struct __YMThreadDispatchInternal
{
    YMThreadDispatchUserInfoRef userDispatchRef;
    YMThreadDispatchID dispatchID;
} _YMThreadDispatchInternal;

pthread_once_t gDispatchInitOnce = PTHREAD_ONCE_INIT;
YMThreadDispatchThreadID gDispatchThreadIDNext = 0; // todo only for keying dictionary, implement linked list?
YMDictionaryRef gDispatchThreadDefsByID = NULL;
YMLockRef gDispatchThreadListLock = NULL;

// private
void _YMThreadDispatchInit();
void *_YMThreadDispatchThreadProc(void *);
uint64_t _YMThreadGetCurrentThreadNumber();
YMThreadDispatchUserInfoRef _YMThreadDispatchCopyUserInfo(YMThreadDispatchUserInfoRef userDispatchRef);
void _YMThreadFreeDispatchInternal(_YMThreadDispatchInternal *dispatchInternal);

YMThreadRef _YMThreadCreate(char *name, bool isDispatchThread, ym_thread_entry entryPoint, void *context)
{
    _YMThread *thread = (_YMThread *)YMALLOC(sizeof(struct __YMThread));
    thread->_typeID = _YMThreadTypeID;
    
    pthread_once(&gDispatchInitOnce, _YMThreadDispatchInit);
    
    thread->name = strdup(name ? name : "unnamed");
    thread->entryPoint = isDispatchThread ? _YMThreadDispatchThreadProc : entryPoint;
    thread->context = context;
    thread->pthread = NULL;
    thread->isDispatchThread = isDispatchThread;
    
    thread->didStart = false;
    
    if ( isDispatchThread )
    {
        YMLockLock(gDispatchThreadListLock);
        {
            thread->dispatchThreadID = gDispatchThreadIDNext++;
            if ( YMDictionaryContains(gDispatchThreadDefsByID, thread->dispatchThreadID) ) // either a bug or pathological
            {
                ymerr("thread[%s,dispatch]: fatal: out of dispatch thread ids",thread->name);
                abort();
            }
            
            _YMThreadDispatchThreadDef *dispatchThreadDef = (_YMThreadDispatchThreadDef *)YMALLOC(sizeof(_YMThreadDispatchThreadDef));
            dispatchThreadDef->ymThread = thread;
            dispatchThreadDef->stopFlag = calloc(1, sizeof(bool));
            
            YMDictionaryAdd(gDispatchThreadDefsByID, thread->dispatchThreadID, dispatchThreadDef);
            
            thread->context = dispatchThreadDef;
        }
        YMLockUnlock(gDispatchThreadListLock);
        
        thread->dispatchListLock = YMLockCreate("dispatch-list");
        thread->dispatchesByID = YMDictionaryCreate();
        char *semName = YMStringCreateWithFormat("%s-dispatch",name);
        thread->dispatchSemaphore = YMSemaphoreCreate(semName,0);
        free(semName);
        thread->dispatchIDNext = 0;
    }
    
    return thread;
}

void _YMThreadFree(YMTypeRef object)
{
    YMThreadRef thread = (YMThreadRef)object;
    
    if ( thread->isDispatchThread )
    {
        YMFree(thread->dispatchListLock);
        YMFree(thread->dispatchesByID);
        YMFree(thread->dispatchSemaphore);
        
        YMLockLock(gDispatchThreadListLock);
        {
            _YMThreadDispatchThreadDef *threadDef = (_YMThreadDispatchThreadDef *)YMDictionaryGetItem(gDispatchThreadDefsByID, thread->dispatchThreadID);
            *(threadDef->stopFlag) = true;
            ymlog("thread[%s,dispatch,dt%llu]: flagged pthread to exit on ymfree", thread->name, thread->dispatchThreadID);
        }
        YMLockUnlock(gDispatchThreadListLock);
    }
    // todo is there anything we should reasonably do to user threads here?
    
    free(thread->name);
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

// this out-of-band-with-initializer setter was added with ForwardFile, so that YMThread could spawn and
// forget the thread and let it deallocate its own YMThread-level stuff (thread ref not known until after construction)
void YMThreadSetContext(YMThreadRef thread, void *context)
{
    if ( thread->didStart )
    {
        ymerr("thread: fatal: cannot set context on a thread that has already started");
        abort();
    }
    
    thread->context = context;
}

bool YMThreadStart(YMThreadRef thread)
{
    pthread_t pthread;
    int result;
    
    if ( ( result = pthread_create(&pthread, NULL, thread->entryPoint, thread->context) ) )
    {
        ymerr("thread[%s,%s]: error: pthread_create %d %s", thread->name, thread->isDispatchThread?"dispatch":"user", result, strerror(result));
        return false;
    }
    
    ymlog("thread[%s,%s]: created", thread->name, thread->isDispatchThread ? "dispatch" : "user");
    thread->pthread = pthread;
    return true;
}

bool YMThreadJoin(YMThreadRef thread)
{
    _YMThread *_thread = (_YMThread *)thread;
    int result;
    if ( ( result = pthread_join(_thread->pthread, NULL) ) )
    {
        ymerr("thread[%s,%s]: error: pthread_join %d %s", thread->name, thread->isDispatchThread ? "dispatch" : "user", result, strerror(result));
        return false;
    }
    
    return true;
}

void YMThreadDispatchDispatch(YMThreadRef thread, YMThreadDispatchUserInfoRef userDispatch)
{
    if ( ! thread->isDispatchThread )
    {
        ymerr("thread[%s,dispatch,dt%llu]: fatal: attempt to dispatch to non-dispatch thread",thread->name,thread->dispatchThreadID);
        abort();
    }
    
    _YMThreadDispatchInternal *newDispatch = NULL;
    YMLockLock(thread->dispatchListLock);
    {
        newDispatch = (_YMThreadDispatchInternal*)YMALLOC(sizeof(struct __YMThreadDispatchInternal));
        YMThreadDispatchUserInfoRef userDispatchCopy = _YMThreadDispatchCopyUserInfo(userDispatch);
        
        newDispatch->userDispatchRef = userDispatchCopy;
        newDispatch->dispatchID = thread->dispatchIDNext++;
        
        if ( YMDictionaryContains(thread->dispatchesByID, newDispatch->dispatchID) )
        {
            ymerr("thread[%s,dispatch,dt%llu]: fatal: thread is out of dispatch ids (%zu)",thread->name,thread->dispatchThreadID,YMDictionaryGetCount(thread->dispatchesByID));
            abort();
        }
        
        ymlog("thread[%s,dispatch,dt%llu]: adding dispatch '%s': u %p ctx %p",thread->name,thread->dispatchThreadID,userDispatchCopy->description,userDispatchCopy,userDispatchCopy->context);
        YMDictionaryAdd(thread->dispatchesByID, newDispatch->dispatchID, newDispatch);
    }
    YMLockUnlock(thread->dispatchListLock);
    
    YMSemaphoreSignal(thread->dispatchSemaphore);
}

YMThreadDispatchUserInfoRef _YMThreadDispatchCopyUserInfo(YMThreadDispatchUserInfoRef userDispatchRef)
{
    YMThreadDispatchUserInfo *copy = (YMThreadDispatchUserInfo *)YMALLOC(sizeof(YMThreadDispatchUserInfo));
    copy->dispatchProc = userDispatchRef->dispatchProc;
    copy->context = userDispatchRef->context;
    copy->freeContextWhenDone = userDispatchRef->freeContextWhenDone;
    copy->deallocProc = userDispatchRef->deallocProc;
    copy->description = strdup(userDispatchRef->description ? userDispatchRef->description : "unnamed");
    
    return copy;
}

void _YMThreadDispatchInit()
{
    gDispatchThreadDefsByID = YMDictionaryCreate();
    gDispatchThreadListLock = YMLockCreate("g-dispatch-list");
}

void *_YMThreadDispatchThreadProc(void *threadDefPtr)
{
    _YMThreadDispatchThreadDef *threadDef = (_YMThreadDispatchThreadDef *)threadDefPtr;
    YMThreadRef thread = threadDef->ymThread;
    ymlog("thread[%s,dispatch,dt%llu,p%llu]: entered", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
    
    while ( ! *(threadDef->stopFlag) )
    {
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: begin dispatch loop", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
        YMSemaphoreWait(thread->dispatchSemaphore);
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: woke for user dispatch", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
        
        __unused YMThreadDispatchID theDispatchID = -1;
        _YMThreadDispatchInternal *theDispatch = NULL;
        YMLockLock(thread->dispatchListLock);
        {
            YMDictionaryKey randomKey = YMDictionaryRandomKey(thread->dispatchesByID);
            theDispatch = (_YMThreadDispatchInternal *)YMDictionaryRemove(thread->dispatchesByID,randomKey);
            if ( ! theDispatch )
            {
                ymerr("thread[%s,dispatch,dt%llu,p%llu]: fatal: thread signaled without target", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
                abort();
            }
        }
        YMLockUnlock(thread->dispatchListLock);
        
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: entering user dispatch %llu '%s': u %p ctx %p", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber(), theDispatchID, theDispatch->userDispatchRef->description,theDispatch->userDispatchRef,theDispatch->userDispatchRef->context);
        __unused void *result = *(ym_thread_dispatch_entry)(theDispatch->userDispatchRef->dispatchProc)(theDispatch->userDispatchRef);
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: finished user dispatch %llu '%s': u %p ctx %p", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber(), theDispatchID, theDispatch->userDispatchRef->description,theDispatch->userDispatchRef,theDispatch->userDispatchRef->context);
        
        _YMThreadFreeDispatchInternal(theDispatch);
    }
    
    YMLockLock(gDispatchThreadListLock);
    {
        threadDef = (_YMThreadDispatchThreadDef *)YMDictionaryRemove(gDispatchThreadDefsByID, thread->dispatchThreadID);
        
        if ( ! threadDef )
        {
            ymerr("thread[%s,dispatch,dt%llu,p%llu]: fatal: dispatch thread def not found on exit", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
            abort();
        }
        
        free(threadDef->stopFlag);
        free(threadDef);
    }
    YMLockUnlock(gDispatchThreadListLock);
    
    ymlog("thread[%s,dispatch,dt%llu,p%llu]: exiting",thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
    return NULL;
}

void _YMThreadFreeDispatchInternal(_YMThreadDispatchInternal *dispatchInternal)
{
    if ( dispatchInternal->userDispatchRef->freeContextWhenDone )
        free(dispatchInternal->userDispatchRef->context);
    else if ( dispatchInternal->userDispatchRef->deallocProc )
    {
        __unused void *result = *(ym_thread_dispatch_dealloc)(dispatchInternal->userDispatchRef->deallocProc)(dispatchInternal->userDispatchRef->context);
    }
    
    free((void *)dispatchInternal->userDispatchRef->description);
    free(dispatchInternal->userDispatchRef);
    free(dispatchInternal);
}

// xxx i wonder if this is actually going to be portable
uint64_t _YMThreadGetCurrentThreadNumber()
{
    pthread_t pthread = pthread_self();
    uint64_t threadId = 0;
    memcpy(&threadId, &pthread, YMMIN(sizeof(threadId), sizeof(pthread)));
    return threadId;
}

void *__YMThreadDispatchForwardFileProc(void *context);
bool YMThreadDispatchForwardFile(int fromFile, int toFile)
{
    char *name = YMStringCreateWithFormat("dispatch-forward-%d->%d",fromFile,toFile);
    
    // todo: new thread for all, don't let blockage on either end deny other clients of this api.
    // but, if we wanted to feel good about ourselves, these threads could hang around for a certain amount of time to handle
    // subsequent forwarding requests, to recycle the thread creation overhead.
    YMThreadRef forwardingThread = YMThreadCreate(name, __YMThreadDispatchForwardFileProc, NULL);
    free(name);
    
    if ( ! forwardingThread )
        return false;
    
    void *context = YMALLOC(sizeof(struct __YMThread) + 2*sizeof(int));
    ((YMThreadRef *)context)[0] = forwardingThread;
    int *filesPtr = context + sizeof(struct __YMThread);
    filesPtr[0] = fromFile;
    filesPtr[1] = toFile;
    
    YMThreadSetContext(forwardingThread, context);
    
    return YMThreadStart(forwardingThread);
}

void *__YMThreadDispatchForwardFileProc(void *context)
{
    // todo: tired of defining semi-redundant structs for various tasks in here, should go back and take a look
    YMThreadRef thread = ((YMThreadRef *)context)[0];
    int *filesPtr = context + sizeof(struct __YMThread);
    int fromFile = filesPtr[0];
    int toFile = filesPtr[1];
    
    uint64_t outBytes = 0;
    YMIOResult result = YMReadWriteFull(fromFile, toFile, &outBytes);
    
    ymlog("dispatch-forward: %s %llu bytes from %d->%d", (result == YMIOError)?"error at offset":"finished",outBytes,fromFile,toFile);
    
    // todo: ymthread is being leaked here, need a way to set context out of band with the constructor
    free(context);
    YMFree(thread);
    
    return NULL;
}
