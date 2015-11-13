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
#include "YMStreamPriv.h"

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
typedef struct __ym_thread_dispatch_thread_context_def
{
    YMThreadRef ymThread;
    bool *stopFlag;
} ___ym_thread_dispatch_thread_context_def;
typedef struct __ym_thread_dispatch_thread_context_def *__ym_thread_dispatch_thread_context;

typedef struct __ym_thread_dispatch_context_def
{
    ym_thread_dispatch_ref dispatch;
    YMThreadDispatchID dispatchID;
} ___ym_thread_dispatch_context_def;
typedef struct __ym_thread_dispatch_context_def *__ym_thread_dispatch_context;

pthread_once_t gDispatchInitOnce = PTHREAD_ONCE_INIT;
YMThreadDispatchThreadID gDispatchThreadIDNext = 0; // todo only for keying dictionary, implement linked list?
YMDictionaryRef gDispatchThreadDefsByID = NULL;
YMLockRef gDispatchThreadListLock = NULL;

// private
void __YMThreadDispatchInit();
void __ym_thread_dispatch_dispatch_thread_proc(void *);
typedef struct __ym_thread_dispatch_forward_file_async_context_def
{
    YMThreadRef threadOrNull;
    int file;
    YMStreamRef stream;
    bool toStream;
    bool limited;
    uint64_t nBytes;
    bool sync; // only necessary to free return value
    ym_thread_dispatch_forward_file_context callbackInfo;
} ___ym_thread_dispatch_forward_file_async_def;
typedef struct __ym_thread_dispatch_forward_file_async_context_def __ym_thread_dispatch_forward_file_async_context;
typedef __ym_thread_dispatch_forward_file_async_context *__ym_thread_dispatch_forward_file_async_context_ref;
void *__ym_thread_dispatch_forward_file_proc(__ym_thread_dispatch_forward_file_async_context_ref);
uint64_t _YMThreadGetCurrentThreadNumber();
ym_thread_dispatch_ref __YMThreadDispatchCopy(ym_thread_dispatch_ref userDispatchRef);
void __YMThreadFreeDispatchContext(__ym_thread_dispatch_context);
bool __YMThreadDispatchForward(YMStreamRef stream, int file, bool toStream, bool limited, uint64_t byteLimit, bool sync, ym_thread_dispatch_forward_file_context callbackInfo);

YMThreadRef _YMThreadCreate(char *name, bool isDispatchThread, ym_thread_entry entryPoint, void *context)
{
    _YMThread *thread = (_YMThread *)YMALLOC(sizeof(struct __YMThread));
    thread->_typeID = _YMThreadTypeID;
    
    pthread_once(&gDispatchInitOnce, __YMThreadDispatchInit);
    
    thread->name = strdup(name ? name : "unnamed");
    thread->entryPoint = isDispatchThread ? (void (*)(void *))__ym_thread_dispatch_dispatch_thread_proc : entryPoint;
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
            
            __ym_thread_dispatch_thread_context dispatchThreadDef = (__ym_thread_dispatch_thread_context)YMALLOC(sizeof(struct __ym_thread_dispatch_thread_context_def));
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
            __ym_thread_dispatch_thread_context threadDef = (__ym_thread_dispatch_thread_context)YMDictionaryGetItem(gDispatchThreadDefsByID, thread->dispatchThreadID);
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
    return _YMThreadCreate(name, true, __ym_thread_dispatch_dispatch_thread_proc, NULL);
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
    
    if ( ( result = pthread_create(&pthread, NULL, (void *(*)(void *))thread->entryPoint, thread->context) ) )
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

void YMThreadDispatchDispatch(YMThreadRef thread, ym_thread_dispatch dispatch)
{
    if ( ! thread->isDispatchThread )
    {
        ymerr("thread[%s,dispatch,dt%llu]: fatal: attempt to dispatch to non-dispatch thread",thread->name,thread->dispatchThreadID);
        abort();
    }
    
    __ym_thread_dispatch_context newDispatch = NULL;
    YMLockLock(thread->dispatchListLock);
    {
        newDispatch = (__ym_thread_dispatch_context)YMALLOC(sizeof(struct __ym_thread_dispatch_context_def));
        ym_thread_dispatch_ref dispatchCopy = __YMThreadDispatchCopy(&dispatch);
        
        newDispatch->dispatch = dispatchCopy;
        newDispatch->dispatchID = thread->dispatchIDNext++;
        
        if ( YMDictionaryContains(thread->dispatchesByID, newDispatch->dispatchID) )
        {
            ymerr("thread[%s,dispatch,dt%llu]: fatal: thread is out of dispatch ids (%zu)",thread->name,thread->dispatchThreadID,YMDictionaryGetCount(thread->dispatchesByID));
            abort();
        }
        
        ymlog("thread[%s,dispatch,dt%llu]: adding dispatch '%s': u %p ctx %p",thread->name,thread->dispatchThreadID,dispatchCopy->description,dispatchCopy,dispatchCopy->context);
        YMDictionaryAdd(thread->dispatchesByID, newDispatch->dispatchID, newDispatch);
    }
    YMLockUnlock(thread->dispatchListLock);
    
    YMSemaphoreSignal(thread->dispatchSemaphore);
}

ym_thread_dispatch_ref __YMThreadDispatchCopy(ym_thread_dispatch_ref userDispatchRef)
{
    ym_thread_dispatch_ref copy = YMALLOC(sizeof(ym_thread_dispatch));
    copy->dispatchProc = userDispatchRef->dispatchProc;
    copy->context = userDispatchRef->context;
    copy->freeContextWhenDone = userDispatchRef->freeContextWhenDone;
    copy->deallocProc = userDispatchRef->deallocProc;
    copy->description = strdup(userDispatchRef->description ? userDispatchRef->description : "unnamed");
    
    return copy;
}

void __YMThreadDispatchInit()
{
    gDispatchThreadDefsByID = YMDictionaryCreate();
    gDispatchThreadListLock = YMLockCreate("g-dispatch-list");
}

void __ym_thread_dispatch_dispatch_thread_proc(void * ctx)
{
    __ym_thread_dispatch_thread_context context = (__ym_thread_dispatch_thread_context)ctx;
    YMThreadRef thread = context->ymThread;
    bool *stopFlag = context->stopFlag;
    ymlog("thread[%s,dispatch,dt%llu,p%llu]: entered", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
    
    while ( ! *stopFlag )
    {
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: begin dispatch loop", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
        YMSemaphoreWait(thread->dispatchSemaphore);
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: woke for a dispatch", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
        
        __unused YMThreadDispatchID theDispatchID = -1;
        __ym_thread_dispatch_context theDispatch = NULL;
        YMLockLock(thread->dispatchListLock);
        {
            YMDictionaryKey randomKey = YMDictionaryRandomKey(thread->dispatchesByID);
            theDispatch = (__ym_thread_dispatch_context)YMDictionaryRemove(thread->dispatchesByID,randomKey);
            if ( ! theDispatch )
            {
                ymerr("thread[%s,dispatch,dt%llu,p%llu]: fatal: thread signaled without target", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
                abort();
            }
        }
        YMLockUnlock(thread->dispatchListLock);
        
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: entering dispatch %llu '%s': u %p ctx %p", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber(), theDispatchID, theDispatch->dispatch->description,theDispatch->dispatch,theDispatch->dispatch->context);
        __unused void *result = *(ym_thread_entry)(theDispatch->dispatch->dispatchProc)(theDispatch->dispatch);
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: finished dispatch %llu '%s': u %p ctx %p", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber(), theDispatchID, theDispatch->dispatch->description,theDispatch->dispatch,theDispatch->dispatch->context);
        
        __YMThreadFreeDispatchContext(theDispatch);
    }
    
    YMLockLock(gDispatchThreadListLock);
    {
        __ym_thread_dispatch_thread_context sanityCheck = (__ym_thread_dispatch_thread_context)YMDictionaryRemove(gDispatchThreadDefsByID, thread->dispatchThreadID);
        
        if ( ! sanityCheck )
        {
            ymerr("thread[%s,dispatch,dt%llu,p%llu]: fatal: dispatch thread def not found on exit", thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
            abort();
        }
        
        free(sanityCheck->stopFlag);
        free(sanityCheck);
    }
    YMLockUnlock(gDispatchThreadListLock);
    
    ymlog("thread[%s,dispatch,dt%llu,p%llu]: exiting",thread->name, thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
}

void __YMThreadFreeDispatchContext(__ym_thread_dispatch_context dispatchContext)
{
    if ( dispatchContext->dispatch->freeContextWhenDone )
        free(dispatchContext->dispatch->context);
    else if ( dispatchContext->dispatch->deallocProc )
    {
        __unused void *result = *(ym_thread_dispatch_dealloc)(dispatchContext->dispatch->deallocProc)(dispatchContext->dispatch->context);
    }
    
    free((void *)dispatchContext->dispatch->description);
    free(dispatchContext->dispatch);
    free(dispatchContext);
}

// xxx i wonder if this is actually going to be portable
uint64_t _YMThreadGetCurrentThreadNumber()
{
    pthread_t pthread = pthread_self();
    uint64_t threadId = 0;
    memcpy(&threadId, &pthread, YMMIN(sizeof(threadId), sizeof(pthread)));
    return threadId;
}

bool YMThreadDispatchForwardFile(int fromFile, YMStreamRef toStream, bool limited, uint64_t byteLimit, bool sync, ym_thread_dispatch_forward_file_context callbackInfo)
{
    return __YMThreadDispatchForward(toStream, fromFile, true, limited, byteLimit, sync, callbackInfo);
}

bool YMThreadDispatchForwardStream(YMStreamRef fromStream, int toFile, bool limited, uint64_t byteLimit, bool sync, ym_thread_dispatch_forward_file_context callbackInfo)
{
    return __YMThreadDispatchForward(fromStream, toFile, false, limited, byteLimit, sync, callbackInfo);
}

bool __YMThreadDispatchForward(YMStreamRef stream, int file, bool toStream, bool limited, uint64_t byteLimit, bool sync, ym_thread_dispatch_forward_file_context callbackInfo)
{
    YMThreadRef forwardingThread = NULL;
    char *name = NULL;
    
    __ym_thread_dispatch_forward_file_async_context_ref context = YMALLOC(sizeof(__ym_thread_dispatch_forward_file_async_context));
    context->threadOrNull = forwardingThread;
    context->file = file;
    context->stream = stream;
    context->toStream = toStream;
    context->limited = limited;
    context->nBytes = byteLimit;
    context->sync = sync;
    context->callbackInfo = callbackInfo;
    
    if ( sync )
    {
        YMIOResult *result = __ym_thread_dispatch_forward_file_proc(context);
        YMIOResult retResult = ( *result == YMIOSuccess );
        free(result);
        return retResult;
    }
        
    // todo: new thread for all, don't let blockage on either end deny other clients of this api.
    // but, if we wanted to feel good about ourselves, these threads could hang around for a certain amount of time to handle
    // subsequent forwarding requests, to recycle the thread creation overhead.
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    name = YMStringCreateWithFormat("dispatch-forward-%d%ss%u",file,toStream?"->":"<-",streamID);
    forwardingThread = YMThreadCreate(name, (void (*)(void *))__ym_thread_dispatch_forward_file_proc, context);
    free(name);
    if ( ! forwardingThread )
    {
        ymerr("thread[%s]: error: failed to create",name);
        goto rewind_fail;
    }
    YMThreadSetContext(forwardingThread, context);
    bool okay = YMThreadStart(forwardingThread);
    if ( ! okay )
    {
        ymerr("thread[%s]: error: failed to start forwarding thread",name);
        goto rewind_fail;
    }
    
    return true;
    
rewind_fail:
    if ( forwardingThread )
        YMFree(forwardingThread);
    free(context);
    return false;
}

void *__ym_thread_dispatch_forward_file_proc(__ym_thread_dispatch_forward_file_async_context_ref ctx)
{
    // todo: tired of defining semi-redundant structs for various tasks in here, should go back and take a look
    YMThreadRef threadOrNull = ctx->threadOrNull;
    const char *threadName = threadOrNull ? threadOrNull->name : "*";
    int file = ctx->file;
    YMStreamRef stream = ctx->stream;
    bool toStream = ctx->toStream;
    bool limited = ctx->limited;
    uint64_t nBytes = ctx->nBytes;
    bool sync = ctx->sync;
    ym_thread_dispatch_forward_file_context callbackInfo = ctx->callbackInfo;
    free(ctx);
    
    uint64_t outBytes = 0;
    
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    ymlog("thread[%s]: forward: entered for %d%ss%u",threadName,file,toStream?"->":"<-",streamID);
    uint64_t forwardBytes = limited ? nBytes : 0;
    YMIOResult result;
    if ( toStream )
        result = YMStreamReadFromFile(stream, file, limited ? &forwardBytes : NULL, &outBytes);
    else
        result = YMStreamWriteToFile(stream, file, limited ? &forwardBytes : NULL, &outBytes);
    ymlog("thread[%s]: forward: %s %llu bytes from %d%ss%u",threadName, (result == YMIOError)?"error at offset":"finished",outBytes,file,toStream?"->":"<-",streamID);
    
    if ( threadOrNull )
        YMFree(threadOrNull);
    
    bool *ret = NULL;
    if ( sync )
    {
        ret = YMALLOC(sizeof(YMIOResult));
        *ret = result;
    }
    else
    {
        
    }
    
    if ( callbackInfo.callback )
        callbackInfo.callback(callbackInfo.context,outBytes);
    
    return ret;
}
