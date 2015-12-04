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
#include "YMStreamPriv.h"
#include "YMPlexerPriv.h"

#ifndef WIN32
#include <pthread.h>
#define YM_THREAD_TYPE pthread_t
#else
#define YM_THREAD_TYPE HANDLE
#endif

#define ymlog_type YMLogThread
#include "YMLog.h"

typedef uint64_t YMThreadDispatchID;
typedef uint64_t YMThreadDispatchThreadID;

typedef struct __ym_thread_t
{
    _YMType _typeID;
    
    YMStringRef name;
    ym_thread_entry entryPoint;
    const void *context;
    YM_THREAD_TYPE pthread;
    
    // thread state
    bool didStart;
    
    // dispatch stuff
    bool isDispatchThread;
    YMThreadDispatchThreadID dispatchThreadID;
    YMThreadDispatchID dispatchIDNext;
    YMSemaphoreRef dispatchSemaphore;
    YMSemaphoreRef dispatchExitSemaphore;
    YMDictionaryRef dispatchesByID;
    YMLockRef dispatchListLock;
} __ym_thread_t;
typedef struct __ym_thread_t *__YMThreadRef;

// dispatch stuff
typedef struct __ym_thread_dispatch_thread_context_t
{
    __YMThreadRef ymThread;
    bool *stopFlag;
} ___ym_thread_dispatch_thread_context_t;
typedef struct __ym_thread_dispatch_thread_context_t *__ym_thread_dispatch_thread_context_ref;

typedef struct __ym_thread_dispatch_context_t
{
    ym_thread_dispatch_ref dispatch;
    YMThreadDispatchID dispatchID;
} ___ym_thread_dispatch_context_t;
typedef struct __ym_thread_dispatch_context_t *__ym_thread_dispatch_context_ref;

YMThreadDispatchThreadID gDispatchThreadIDNext = 0; // todo only for keying dictionary, implement linked list?
YMDictionaryRef gDispatchThreadDefsByID = NULL;
YMLockRef gDispatchThreadListLock = NULL;

// private
YM_ONCE_DEF(__YMThreadDispatchInit);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_dispatch_thread_proc(YM_THREAD_PARAM);
typedef struct __ym_thread_dispatch_forward_file_async_context_def
{
    __YMThreadRef threadOrNull;
    YMFILE file;
    YMStreamRef stream;
    bool toStream;
    bool bounded;
    uint64_t nBytes;
    bool sync; // only necessary to free return value
    _ym_thread_forward_file_context_ref callbackInfo;
} ___ym_thread_dispatch_forward_file_async_def;
typedef struct __ym_thread_dispatch_forward_file_async_context_def __ym_thread_dispatch_forward_file_async_context;
typedef __ym_thread_dispatch_forward_file_async_context *__ym_thread_dispatch_forward_file_async_context_ref;

__YMThreadRef __YMThreadInitCommon(YMStringRef name, const void *context);
YMIOResult __ym_thread_dispatch_forward_file_proc(void *);
ym_thread_dispatch_ref __YMThreadDispatchCopy(ym_thread_dispatch_ref userDispatchRef);
__ym_thread_dispatch_thread_context_ref __YMThreadDispatchJoin(__YMThreadRef thread);
void __YMThreadFreeDispatchContext(__ym_thread_dispatch_context_ref);
bool __YMThreadDispatchForward(YMStreamRef stream, YMFILE file, bool toStream, const uint64_t *nBytesPtr, bool sync, _ym_thread_forward_file_context_ref callbackInfo);

YM_ONCE_FUNC(__YMThreadDispatchInit,
{
	gDispatchThreadDefsByID = YMDictionaryCreate();
	YMStringRef name = YMSTRC("g-dispatch-list");
	gDispatchThreadListLock = YMLockCreateWithOptionsAndName(YMInternalLockType, name);
	YMRelease(name);
})

__YMThreadRef __YMThreadInitCommon(YMStringRef name, const void *context)
{
    __YMThreadRef thread = (__YMThreadRef)_YMAlloc(_YMThreadTypeID,sizeof(struct __ym_thread_t));

	YM_ONCE_DO_LOCAL(__YMThreadDispatchInit);
    
    thread->name = name ? YMRetain(name) : YMSTRC("*");
    thread->context = context;
    thread->pthread = (YM_THREAD_TYPE)NULL;
    
    thread->didStart = false;
    
    return thread;
}

YMThreadRef YMThreadCreate(YMStringRef name, ym_thread_entry entryPoint, const void *context)
{
    __YMThreadRef thread = __YMThreadInitCommon(name, context);
    thread->entryPoint = entryPoint;
    thread->isDispatchThread = false;
    
    return thread;
}

YMThreadRef YMThreadDispatchCreate(YMStringRef name)
{
    __YMThreadRef thread = __YMThreadInitCommon(name, NULL);
    thread->entryPoint = __ym_thread_dispatch_dispatch_thread_proc;
    thread->isDispatchThread = true;
    
    YMLockLock(gDispatchThreadListLock);
    {
        thread->dispatchThreadID = gDispatchThreadIDNext++;
        if ( YMDictionaryContains(gDispatchThreadDefsByID, thread->dispatchThreadID) ) // either a bug or pathological
        {
            ymerr("thread[%s,dispatch]: fatal: out of dispatch thread ids",YMSTR(thread->name));
            abort();
        }
        
        __ym_thread_dispatch_thread_context_ref dispatchThreadDef = (__ym_thread_dispatch_thread_context_ref)YMALLOC(sizeof(struct __ym_thread_dispatch_thread_context_t));
        dispatchThreadDef->ymThread = thread;
        dispatchThreadDef->stopFlag = calloc(1, sizeof(bool));
        
        YMDictionaryAdd(gDispatchThreadDefsByID, thread->dispatchThreadID, dispatchThreadDef);
        
        thread->context = dispatchThreadDef;
    }
    YMLockUnlock(gDispatchThreadListLock);
    
    YMStringRef memberName = YMSTRC("dispatch-list");
    thread->dispatchListLock = YMLockCreateWithOptionsAndName(YMInternalLockType,memberName);
    YMRelease(memberName);
    
    thread->dispatchesByID = YMDictionaryCreate();
    memberName = YMSTRCF("%s-dispatch",name ? YMSTR(name) : "*");
    thread->dispatchSemaphore = YMSemaphoreCreateWithName(memberName,0);
    YMRelease(memberName);
    
    memberName = YMSTRCF("%s-dispatch-exit",name ? YMSTR(name) : "*");
    thread->dispatchExitSemaphore = YMSemaphoreCreateWithName(memberName, 0);
    YMRelease(memberName);
    
    thread->dispatchIDNext = 0;
    
    return thread;
}

void _YMThreadFree(YMTypeRef object)
{
    __YMThreadRef thread = (__YMThreadRef)object;
    
    if ( thread->isDispatchThread )
    {
        __ym_thread_dispatch_thread_context_ref threadDef = __YMThreadDispatchJoin(thread);
        
        free(threadDef->stopFlag);
        free(threadDef);
        
        YMRelease(thread->dispatchListLock);
        YMRelease(thread->dispatchesByID);
        YMRelease(thread->dispatchSemaphore);
        YMRelease(thread->dispatchExitSemaphore);
    }
    // todo is there anything we should reasonably do to user threads here?
    
    YMRelease(thread->name);
}

void YMThreadDispatchJoin(YMThreadRef thread_)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    __YMThreadDispatchJoin(thread);
}

__ym_thread_dispatch_thread_context_ref __YMThreadDispatchJoin(__YMThreadRef thread)
{
    __ym_thread_dispatch_thread_context_ref threadDef = NULL;
    YMLockLock(gDispatchThreadListLock);
    {
        threadDef = (__ym_thread_dispatch_thread_context_ref)YMDictionaryRemove(gDispatchThreadDefsByID, thread->dispatchThreadID);
        *(threadDef->stopFlag) = true;
        ymlog("thread[%s,dispatch,dt%llu]: flagged pthread to exit on ymfree with %p", YMSTR(thread->name), thread->dispatchThreadID,threadDef->stopFlag);
    }
    YMLockUnlock(gDispatchThreadListLock);
    
    YMSemaphoreSignal(thread->dispatchSemaphore);
    YMSemaphoreWait(thread->dispatchExitSemaphore);
    ymlog("thread[%s,dispatch,dt%llu]: received signal from exiting thread on ymfree", YMSTR(thread->name), thread->dispatchThreadID);
    
    return threadDef;
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
    
    YM_THREAD_TYPE pthread;
    
#ifndef WIN32
	int result;
    if ( ( result = pthread_create(&pthread, NULL, (void *(*)(void *))thread->entryPoint, (void *)thread->context) ) )
    {
        ymerr("thread[%s,%s]: error: pthread_create %d %s", YMSTR(thread->name), thread->isDispatchThread?"dispatch":"user", result, strerror(result));
        return false;
    }
#else
	DWORD threadId;
	pthread = CreateThread(NULL, 0, thread->entryPoint, (LPVOID)thread->context, 0, &threadId);
	if ( pthread == NULL )
	{
		ymerr("thread[%s,%s]: error: CreateThread failed: %x", YMSTR(thread->name), thread->isDispatchThread ? "dispatch" : "user", GetLastError());
		return false;
	}
#endif
    
    ymlog("thread[%s,%s]: created", YMSTR(thread->name), thread->isDispatchThread ? "dispatch" : "user");
    thread->pthread = pthread;
    return true;
}

bool YMThreadJoin(YMThreadRef thread_)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
#ifndef WIN32
    int result = pthread_join(thread->pthread, NULL);
    if ( result != 0 )
    {
        ymerr("thread[%s,%s]: error: pthread_join %d %s", YMSTR(thread->name), thread->isDispatchThread ? "dispatch" : "user", result, strerror(result));
        return false;
    }
#else
	DWORD result = WaitForSingleObject(thread->pthread, INFINITE);
	if ( result != WAIT_OBJECT_0 )
	{
		ymerr("thread[%s,%s]: error: WaitForSingleObject %x", YMSTR(thread->name), thread->isDispatchThread ? "dispatch" : "user", result);
		return false;
	}
#endif
    
    return true;
}

void YMThreadDispatchDispatch(YMThreadRef thread_, struct ym_thread_dispatch_t dispatch)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    
    if ( ! thread->isDispatchThread )
    {
        ymerr("thread[%s,dispatch,dt%llu]: fatal: attempt to dispatch to non-dispatch thread",YMSTR(thread->name),thread->dispatchThreadID);
        abort();
    }
    
    __ym_thread_dispatch_context_ref newDispatch = NULL;
    YMLockLock(thread->dispatchListLock);
    {
        newDispatch = (__ym_thread_dispatch_context_ref)YMALLOC(sizeof(struct __ym_thread_dispatch_context_t));
        ym_thread_dispatch_ref dispatchCopy = __YMThreadDispatchCopy(&dispatch);
        
        newDispatch->dispatch = dispatchCopy;
        newDispatch->dispatchID = thread->dispatchIDNext++;
        
        if ( YMDictionaryContains(thread->dispatchesByID, newDispatch->dispatchID) )
        {
            ymerr("thread[%s,dispatch,dt%llu]: fatal: thread is out of dispatch ids (%zu)",YMSTR(thread->name),thread->dispatchThreadID,YMDictionaryGetCount(thread->dispatchesByID));
            abort();
        }
        
        ymlog("thread[%s,dispatch,dt%llu]: adding dispatch '%s': u %p ctx %p",YMSTR(thread->name),thread->dispatchThreadID,YMSTR(dispatchCopy->description),dispatchCopy,dispatchCopy->context);
        YMDictionaryAdd(thread->dispatchesByID, newDispatch->dispatchID, newDispatch);
    }
    YMLockUnlock(thread->dispatchListLock);
    
    YMSemaphoreSignal(thread->dispatchSemaphore);
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_dispatch_thread_proc(YM_THREAD_PARAM ctx)
{
    __ym_thread_dispatch_thread_context_ref context = (__ym_thread_dispatch_thread_context_ref)ctx;
    __YMThreadRef thread = context->ymThread;
    bool *stopFlag = context->stopFlag;
    ymlog("thread[%s,dispatch,dt%llu,p%llu]: entered", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
    
    while ( ! *stopFlag )
    {
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: begin dispatch loop", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
        YMSemaphoreWait(thread->dispatchSemaphore);
        
        // check if we were signaled to exit
        if ( *stopFlag )
        {
            ymlog("thread[%s,dispatch,dt%llu,p%llu]: woke for exit", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
            break;
        }
        
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: woke for a dispatch", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
        
        __unused YMThreadDispatchID aDispatchID = -1;
        __ym_thread_dispatch_context_ref aDispatch = NULL;
        YMLockLock(thread->dispatchListLock);
        {
            // todo this should be in order
            YMDictionaryKey randomKey = YMDictionaryGetRandomKey(thread->dispatchesByID);
            aDispatch = (__ym_thread_dispatch_context_ref)YMDictionaryRemove(thread->dispatchesByID,randomKey);
            if ( ! aDispatch )
            {
                ymerr("thread[%s,dispatch,dt%llu,p%llu]: fatal: thread signaled without target", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
                abort();
            }
        }
        YMLockUnlock(thread->dispatchListLock);
        
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: entering dispatch %llu '%s': u %p ctx %p", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber(), aDispatchID, YMSTR(aDispatch->dispatch->description),aDispatch->dispatch,aDispatch->dispatch->context);
        aDispatch->dispatch->dispatchProc(aDispatch->dispatch);
        ymlog("thread[%s,dispatch,dt%llu,p%llu]: finished dispatch %llu '%s': u %p ctx %p", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber(), aDispatchID, YMSTR(aDispatch->dispatch->description),aDispatch->dispatch,aDispatch->dispatch->context);
        
        __YMThreadFreeDispatchContext(aDispatch);
    }
    
    ymlog("thread[%s,dispatch,dt%llu,p%llu]: exiting", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
    
    // for exit, YMThread signals us after setting stop flag, we signal them back
    // so they know it's safe to deallocate our stuff
    YMSemaphoreSignal(thread->dispatchExitSemaphore);

	YM_THREAD_END
}

ym_thread_dispatch_ref __YMThreadDispatchCopy(ym_thread_dispatch_ref userDispatchRef)
{
    ym_thread_dispatch_ref copy = YMALLOC(sizeof(struct ym_thread_dispatch_t));
    copy->dispatchProc = userDispatchRef->dispatchProc;
    copy->context = userDispatchRef->context;
    copy->freeContextWhenDone = userDispatchRef->freeContextWhenDone;
    copy->deallocProc = userDispatchRef->deallocProc;
    copy->description = userDispatchRef->description ? YMRetain(userDispatchRef->description) : YMSTRC("*");
    
    return copy;
}

void __YMThreadFreeDispatchContext(__ym_thread_dispatch_context_ref dispatchContext)
{
    if ( dispatchContext->dispatch->freeContextWhenDone )
        free(dispatchContext->dispatch->context);
    else if ( dispatchContext->dispatch->deallocProc )
    {
        dispatchContext->dispatch->deallocProc(dispatchContext->dispatch->context);
    }
   
    YMRelease(dispatchContext->dispatch->description);
    
    free(dispatchContext->dispatch);
    free(dispatchContext);
}

// xxx i wonder if this is actually going to be portable
uint64_t _YMThreadGetCurrentThreadNumber()
{
#ifdef _MACOS
    uint64_t threadId = 0;
	pthread_threadid_np(pthread_self(), &threadId);
    return threadId;
#elif defined(WIN32)
	return GetCurrentThreadId();
#else
	return pthread_self();
#endif

}

bool YMThreadDispatchForwardFile(YMFILE fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, _ym_thread_forward_file_context_ref callbackInfo)
{
    return __YMThreadDispatchForward(toStream, fromFile, true, nBytesPtr, sync, callbackInfo);
}

bool YMThreadDispatchForwardStream(YMStreamRef fromStream, YMFILE toFile, const uint64_t *nBytesPtr, bool sync, _ym_thread_forward_file_context_ref callbackInfo)
{
    return __YMThreadDispatchForward(fromStream, toFile, false, nBytesPtr, sync, callbackInfo);
}

bool __YMThreadDispatchForward(YMStreamRef stream, YMFILE file, bool toStream, const uint64_t *nBytesPtr, bool sync, _ym_thread_forward_file_context_ref callbackInfo)
{
    __YMThreadRef forwardingThread = NULL;
    YMStringRef name = NULL;
    
    __ym_thread_dispatch_forward_file_async_context_ref context = YMALLOC(sizeof(__ym_thread_dispatch_forward_file_async_context));
    context->threadOrNull = NULL;
    context->file = file;
    context->stream = YMRetain(stream);
    context->toStream = toStream;
    context->bounded = ( nBytesPtr != NULL );
    context->nBytes = nBytesPtr ? *nBytesPtr : 0;
    context->sync = sync;
    context->callbackInfo = callbackInfo;
    
    if ( sync )
    {
        YMIOResult result = __ym_thread_dispatch_forward_file_proc(context);
        return ( result == YMIOSuccess || ( ! nBytesPtr && result == YMIOEOF ) );
    }
        
    // todo: new thread for all, don't let blockage on either end deny other clients of this api.
    // but, if we wanted to feel good about ourselves, these threads could hang around for a certain amount of time to handle
    // subsequent forwarding requests, to recycle the thread creation overhead.
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    name = YMSTRCF("dispatch-forward-%d%ss%u",file,toStream?"->":"<-",streamID);
    forwardingThread = (__YMThreadRef)YMThreadCreate(name, (ym_thread_entry)__ym_thread_dispatch_forward_file_proc, context);
    YMRelease(name);
    if ( ! forwardingThread )
    {
        ymerr("thread[%s]: error: failed to create",YMSTR(name));
        goto rewind_fail;
    }
    context->threadOrNull = forwardingThread;
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

YMIOResult __ym_thread_dispatch_forward_file_proc(void *ctx_)
{
	__ym_thread_dispatch_forward_file_async_context_ref ctx = (__ym_thread_dispatch_forward_file_async_context_ref)ctx_;
    // todo: tired of defining semi-redundant structs for various tasks in here, should go back and take a look
    __YMThreadRef threadOrNull = ctx->threadOrNull;
    YMStringRef threadName = threadOrNull ? YMRetain(threadOrNull->name) : YMSTRC("*");
    YMFILE file = ctx->file;
    YMStreamRef stream = ctx->stream;
    bool toStream = ctx->toStream;
    bool bounded = ctx->bounded;
    uint64_t nBytes = ctx->nBytes;
    bool sync = ctx->sync;
    _ym_thread_forward_file_context_ref callbackInfo = ctx->callbackInfo;
    
    uint64_t outBytes = 0;
    
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymlog("thread[%s]: forward: entered for f%d%ss%u",YMSTR(threadName),file,toStream?"->":"<-",streamID);
    uint64_t forwardBytes = bounded ? nBytes : 0;
    YMIOResult result;
    if ( toStream )
        result = YMStreamReadFromFile(stream, file, bounded ? &forwardBytes : NULL, &outBytes);
    else
        result = YMStreamWriteToFile(stream, file, bounded ? &forwardBytes : NULL, &outBytes);
    ymlog("thread[%s]: forward: %s %llu bytes from f%d%ss%u",YMSTR(threadName), (result == YMIOError)?"error at offset":"finished",outBytes,file,toStream?"->":"<-",streamID);
    
    if ( ! sync && callbackInfo->callback )
    {
        YMIOResult effectiveResult = nBytes ? result : ( result == YMIOEOF );
        callbackInfo->callback(callbackInfo->context,effectiveResult,outBytes);
    }
    
    YMRelease(threadName);
    YMRelease(stream);
    if ( threadOrNull )
        YMRelease(threadOrNull);
    free(ctx->callbackInfo);
    free(ctx);
    return result;
}
