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
#include "YMPlexerPriv.h"

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

typedef struct __ym_thread
{
    _YMType _typeID;
    
    YMStringRef name;
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
} ___ym_thread;
typedef struct __ym_thread __YMThread;
typedef __YMThread *__YMThreadRef;

// dispatch stuff
typedef struct __ym_thread_dispatch_thread_context_def
{
    __YMThreadRef ymThread;
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
    __YMThreadRef threadOrNull;
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

YMThreadRef _YMThreadCreate(YMStringRef name, bool isDispatchThread, ym_thread_entry entryPoint, void *context)
{
    __YMThreadRef thread = (__YMThreadRef)_YMAlloc(_YMThreadTypeID,sizeof(__YMThread));
    
    pthread_once(&gDispatchInitOnce, __YMThreadDispatchInit);
    
    thread->name = name ? YMRetain(name) : YMSTRC("unnamed");
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
                ymerr("thread[%s,dispatch]: fatal: out of dispatch thread ids",YMSTR(thread->name));
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
        YMStringRef semName = YMStringCreateWithFormat("%s-dispatch",YMSTR(name),NULL);
        thread->dispatchSemaphore = YMSemaphoreCreate(semName,0);
        YMRelease(semName);
        thread->dispatchIDNext = 0;
    }
    
    return thread;
}

void _YMThreadFree(YMTypeRef object)
{
#pragma message "maybe ymthread should maintain a realistically large enough global set of bools to let threads exit flags reference good memory after deallocation"
    __YMThreadRef thread = (__YMThreadRef)object;
    
    if ( thread->isDispatchThread )
    {
        YMRelease(thread->dispatchListLock);
        YMRelease(thread->dispatchesByID);
        YMRelease(thread->dispatchSemaphore);
        
        YMLockLock(gDispatchThreadListLock);
        {
            __ym_thread_dispatch_thread_context threadDef = (__ym_thread_dispatch_thread_context)YMDictionaryGetItem(gDispatchThreadDefsByID, thread->dispatchThreadID);
            *(threadDef->stopFlag) = true;
            ymlog("thread[%s,dispatch,dt%llu]: flagged pthread to exit on ymfree", YMSTR(thread->name), thread->dispatchThreadID);
        }
        YMLockUnlock(gDispatchThreadListLock);
    }
    // todo is there anything we should reasonably do to user threads here?
    
    YMRelease(thread->name);
}


YMThreadRef YMThreadCreate(YMStringRef name, ym_thread_entry entryPoint, void *context)
{
    return _YMThreadCreate(name, false, entryPoint, context);
}

YMThreadRef YMThreadDispatchCreate(YMStringRef name)
{
    return _YMThreadCreate(name, true, __ym_thread_dispatch_dispatch_thread_proc, NULL);
}

// this out-of-band-with-initializer setter was added with ForwardFile, so that YMThread could spawn and
// forget the thread and let it deallocate its own YMThread-level stuff (thread ref not known until after construction)
void YMThreadSetContext(YMThreadRef thread_, void *context)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    if ( thread->didStart )
    {
        ymerr("thread[%s]: fatal: cannot set context on a thread that has already started",YMSTR(thread->name));
        abort();
    }
    
    thread->context = context;
}

bool YMThreadStart(YMThreadRef thread_)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    
    pthread_t pthread;
    int result;
    
    if ( ( result = pthread_create(&pthread, NULL, (void *(*)(void *))thread->entryPoint, thread->context) ) )
    {
        ymerr("thread[%s,%s]: error: pthread_create %d %s", YMSTR(thread->name), thread->isDispatchThread?"dispatch":"user", result, strerror(result));
        return false;
    }
    
    ymlog("thread[%s,%s]: created", thread->name, thread->isDispatchThread ? "dispatch" : "user");
    thread->pthread = pthread;
    return true;
}

bool YMThreadJoin(YMThreadRef thread_)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    int result;
    if ( ( result = pthread_join(thread->pthread, NULL) ) )
    {
        ymerr("thread[%s,%s]: error: pthread_join %d %s", YMSTR(thread->name), thread->isDispatchThread ? "dispatch" : "user", result, strerror(result));
        return false;
    }
    
    return true;
}

void YMThreadDispatchDispatch(YMThreadRef thread_, ym_thread_dispatch dispatch)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    
    if ( ! thread->isDispatchThread )
    {
        ymerr("thread[%s,dispatch,dt%llu]: fatal: attempt to dispatch to non-dispatch thread",YMSTR(thread->name),thread->dispatchThreadID);
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
            ymerr("thread[%s,dispatch,dt%llu]: fatal: thread is out of dispatch ids (%zu)",YMSTR(thread->name),thread->dispatchThreadID,YMDictionaryGetCount(thread->dispatchesByID));
            abort();
        }
        
        ymlog("thread[%s,dispatch,dt%llu]: adding dispatch '%s': u %p ctx %p",YMSTR(thread->name),thread->dispatchThreadID,dispatchCopy->description,dispatchCopy,dispatchCopy->context);
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
    __YMThreadRef thread = context->ymThread;
    bool *stopFlag = context->stopFlag;
    ymlog("thread[%s,dispatch,dt%llu,p%llu]: entered", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
    
    while ( ! *stopFlag )
    {
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: begin dispatch loop", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
        YMSemaphoreWait(thread->dispatchSemaphore);
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: woke for a dispatch", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
        
        __unused YMThreadDispatchID theDispatchID = -1;
        __ym_thread_dispatch_context theDispatch = NULL;
        YMLockLock(thread->dispatchListLock);
        {
            YMDictionaryKey randomKey = YMDictionaryRandomKey(thread->dispatchesByID);
            theDispatch = (__ym_thread_dispatch_context)YMDictionaryRemove(thread->dispatchesByID,randomKey);
            if ( ! theDispatch )
            {
                ymerr("thread[%s,dispatch,dt%llu,p%llu]: fatal: thread signaled without target", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
                abort();
            }
        }
        YMLockUnlock(thread->dispatchListLock);
        
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: entering dispatch %llu '%s': u %p ctx %p", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber(), theDispatchID, theDispatch->dispatch->description,theDispatch->dispatch,theDispatch->dispatch->context);
        theDispatch->dispatch->dispatchProc(theDispatch->dispatch);
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: finished dispatch %llu '%s': u %p ctx %p", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber(), theDispatchID, theDispatch->dispatch->description,theDispatch->dispatch,theDispatch->dispatch->context);
        
        __YMThreadFreeDispatchContext(theDispatch);
    }
    
    YMLockLock(gDispatchThreadListLock);
    {
        __ym_thread_dispatch_thread_context sanityCheck = (__ym_thread_dispatch_thread_context)YMDictionaryRemove(gDispatchThreadDefsByID, thread->dispatchThreadID);
        
        if ( ! sanityCheck )
        {
            ymerr("thread[%s,dispatch,dt%llu,p%llu]: fatal: dispatch thread def not found on exit", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
            abort();
        }
        
        free(sanityCheck->stopFlag);
        free(sanityCheck);
    }
    YMLockUnlock(gDispatchThreadListLock);
    
    ymlog("thread[%s,dispatch,dt%llu,p%llu]: exiting", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
}

void __YMThreadFreeDispatchContext(__ym_thread_dispatch_context dispatchContext)
{
    if ( dispatchContext->dispatch->freeContextWhenDone )
        free(dispatchContext->dispatch->context);
    else if ( dispatchContext->dispatch->deallocProc )
    {
        dispatchContext->dispatch->deallocProc(dispatchContext->dispatch->context);
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
    __YMThreadRef forwardingThread = NULL;
    YMStringRef name = NULL;
    
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
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    name = YMStringCreateWithFormat("dispatch-forward-%d%ss%u",file,toStream?"->":"<-",streamID, NULL);
    forwardingThread = (__YMThreadRef)YMThreadCreate(name, (void (*)(void *))__ym_thread_dispatch_forward_file_proc, context);
    YMRelease(name);
    if ( ! forwardingThread )
    {
        ymerr("thread[%s]: error: failed to create",YMSTR(name));
        goto rewind_fail;
    }
    YMThreadSetContext(forwardingThread, context);
    bool okay = YMThreadStart(forwardingThread);
    if ( ! okay )
    {
        ymerr("thread[%s]: error: failed to start forwarding thread",YMSTR(name));
        goto rewind_fail;
    }
    
    return true;
    
rewind_fail:
    if ( forwardingThread )
        YMRelease(forwardingThread);
    free(context);
    return false;
}

void *__ym_thread_dispatch_forward_file_proc(__ym_thread_dispatch_forward_file_async_context_ref ctx)
{
    // todo: tired of defining semi-redundant structs for various tasks in here, should go back and take a look
    __YMThreadRef threadOrNull = ctx->threadOrNull;
    YMStringRef threadName = threadOrNull ? threadOrNull->name : YMSTRC("*");
    int file = ctx->file;
    YMStreamRef stream = ctx->stream;
    bool toStream = ctx->toStream;
    bool limited = ctx->limited;
    uint64_t nBytes = ctx->nBytes;
    bool sync = ctx->sync;
    ym_thread_dispatch_forward_file_context callbackInfo = ctx->callbackInfo;
    free(ctx);
    
    uint64_t outBytes = 0;
    
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymlog("thread[%s]: forward: entered for %d%ss%u",YMSTR(threadName),file,toStream?"->":"<-",streamID);
    uint64_t forwardBytes = limited ? nBytes : 0;
    YMIOResult result;
    if ( toStream )
        result = YMStreamReadFromFile(stream, file, limited ? &forwardBytes : NULL, &outBytes);
    else
        result = YMStreamWriteToFile(stream, file, limited ? &forwardBytes : NULL, &outBytes);
    ymlog("thread[%s]: forward: %s %llu bytes from %d%ss%u",YMSTR(threadName), (result == YMIOError)?"error at offset":"finished",outBytes,file,toStream?"->":"<-",streamID);
    
    if ( threadOrNull )
        YMRelease(threadOrNull);
    
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
