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

#if !defined(YMWIN32)
# include <pthread.h>
# define YM_THREAD_TYPE pthread_t
#else
# define YM_THREAD_TYPE HANDLE
#endif

#define ymlog_pre "thread[%s,%s,dt%llu]: "
#define ymlog_args thread?YMSTR(thread->name):"&",(thread&&thread->isDispatch)?"dispatch":"&",(thread&&thread->dispatchID)?thread->dispatchID:0
#define ymlog_type YMLogThread
#define ymlog_type_debug YMLogThreadDebug
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef uint64_t YMThreadDispatchID;
typedef uint64_t YMThreaddispatchID;

typedef struct __ym_thread_t
{
    _YMType _typeID;
    
    YMStringRef name;
    ym_thread_entry userEntryPoint;
    void *context;
    YM_THREAD_TYPE pthread;
    uint64_t threadId;
    YMDictionaryRef threadDict;
    
    // thread state
    bool didStart;
    
    // dispatch stuff
    bool isDispatch;
    YMThreaddispatchID dispatchID;
    YMThreadDispatchID dispatchIDNext;
    YMSemaphoreRef dispatchSemaphore;
    YMSemaphoreRef dispatchExitSemaphore;
    YMDictionaryRef dispatchesByID;
    YMLockRef dispatchListLock;
} __ym_thread_t;
typedef struct __ym_thread_t *__YMThreadRef;

#define YMThreadDictThreadIDKey "thread-id"

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

YMThreaddispatchID gdispatchIDNext = 0; // todo only for keying dictionary, implement linked list?
YMDictionaryRef gDispatchThreadDefsByID = NULL;
YMLockRef gDispatchThreadListLock = NULL;
bool gDispatchGlobalModeMain = false;
__YMThreadRef gDispatchGlobalQueue = NULL;
YM_ONCE_OBJ gDispatchGlobalInitOnce = YM_ONCE_INIT;
YM_ONCE_DEF(__YMThreadDispatchInitGlobal);
void __YMThreadDispatchMain();

// private
YM_ONCE_DEF(__YMThreadDispatchInit);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_dispatch_thread_proc(YM_THREAD_PARAM);
typedef struct __ym_thread_dispatch_forward_file_async_context_t
{
    __YMThreadRef threadOrNull;
    YMFILE file;
    YMStreamRef stream;
    bool toStream;
    bool bounded;
    uint64_t nBytes;
    bool sync; // only necessary to free return value
    _ym_thread_forward_file_context_ref callbackInfo;
    
    YMIOResult result;
} ___ym_thread_dispatch_forward_file_async_t;
typedef struct __ym_thread_dispatch_forward_file_async_context_t *__ym_thread_dispatch_forward_file_async_context_ref;

__YMThreadRef __YMThreadInitCommon(YMStringRef name, void *context);
void __YMThreadInitThreadDict(__YMThreadRef thread);
YM_THREAD_TYPE _YMThreadGetCurrentThread();
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_generic_entry_proc(YM_THREAD_PARAM theThread);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_forward_file_proc(YM_THREAD_PARAM forwardCtx);
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

__YMThreadRef __YMThreadInitCommon(YMStringRef name, void *context)
{
    __YMThreadRef thread = (__YMThreadRef)_YMAlloc(_YMThreadTypeID,sizeof(struct __ym_thread_t));

	YM_ONCE_DO_LOCAL(__YMThreadDispatchInit);
    
    thread->name = name ? YMRetain(name) : YMSTRC("*");
    thread->context = context;
    thread->pthread = (YM_THREAD_TYPE)NULL;
    thread->threadId = 0;
    thread->threadDict = YMDictionaryCreate();
    
    thread->didStart = false;
    
    return thread;
}

YMThreadRef YMThreadCreate(YMStringRef name, ym_thread_entry entryPoint, void *context)
{
    __YMThreadRef thread = __YMThreadInitCommon(name, context);
    thread->userEntryPoint = entryPoint;
    thread->isDispatch = false;
    
    return thread;
}

YMThreadRef YMThreadDispatchCreate(YMStringRef name)
{
    __YMThreadRef thread = __YMThreadInitCommon(name, NULL);
    thread->userEntryPoint = __ym_thread_dispatch_dispatch_thread_proc;
    thread->isDispatch = true;
    
    YMLockLock(gDispatchThreadListLock);
    {
        thread->dispatchID = gdispatchIDNext++;
        ymassert(!YMDictionaryContains(gDispatchThreadDefsByID, (YMDictionaryKey)thread->dispatchID),"fatal: out of dispatch thread ids");
        
        __ym_thread_dispatch_thread_context_ref dispatchThreadDef = (__ym_thread_dispatch_thread_context_ref)YMALLOC(sizeof(struct __ym_thread_dispatch_thread_context_t));
        dispatchThreadDef->ymThread = (__YMThreadRef)YMRetain(thread);
        dispatchThreadDef->stopFlag = calloc(1, sizeof(bool));
        
        YMDictionaryAdd(gDispatchThreadDefsByID, (YMDictionaryKey)thread->dispatchID, dispatchThreadDef);
        
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
    
    if ( thread->isDispatch ) {
        __YMThreadDispatchJoin(thread); // join if not already done
        
        YMRelease(thread->dispatchListLock);
        YMRelease(thread->dispatchesByID);
        YMRelease(thread->dispatchSemaphore);
        YMRelease(thread->dispatchExitSemaphore);
    }
    // todo is there anything we should reasonably do to user threads here?
    
    if ( thread->threadDict )
        YMRelease(thread->threadDict);
    YMRelease(thread->name);
}

void YMThreadDispatchJoin(YMThreadRef thread_)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    ymassert(thread->isDispatch,"thread '%s' is not a dispatch thread",YMSTR(thread->name));
    __YMThreadDispatchJoin(thread);
}

__ym_thread_dispatch_thread_context_ref __YMThreadDispatchJoin(__YMThreadRef thread)
{
    __ym_thread_dispatch_thread_context_ref threadDef = NULL;
    
    bool stopped = false;
    YMLockLock(gDispatchThreadListLock);
    {
        if ( YMDictionaryContains(gDispatchThreadDefsByID, (YMDictionaryKey)thread->dispatchID) ) {
            threadDef = (__ym_thread_dispatch_thread_context_ref)YMDictionaryRemove(gDispatchThreadDefsByID, (YMDictionaryKey)thread->dispatchID);
            *(threadDef->stopFlag) = true;
            stopped = true;
            ymlog("flagged pthread to exit on ymfree with %p",threadDef->stopFlag);
        }
    }
    YMLockUnlock(gDispatchThreadListLock);
    
    if ( stopped )
    {
        YMSemaphoreSignal(thread->dispatchSemaphore);
        
        if ( _YMThreadGetThreadNumber(thread) != _YMThreadGetCurrentThreadNumber() ) {
            YMSemaphoreWait(thread->dispatchExitSemaphore);
            ymlog("received exit signal from dispatch thread");
        }
        else
            ymlog("joining from pthread_self, proceeding");
    }
    
    return threadDef;
}

// this out-of-band-with-initializer setter was added with ForwardFile, so that YMThread could spawn and
// forget the thread and let it deallocate its own YMThread-level stuff (thread ref not known until after construction)
void YMThreadSetContext(YMThreadRef thread_, void *context)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    ymassert(!thread->didStart,"fatal: cannot set context on a thread that has already started");
    thread->context = context;
}

bool YMThreadStart(YMThreadRef thread_)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    
    YMRetain(thread); // handle (normal) thread completing (and finalizing) before this method returns
    bool okay = false;
    
    YM_THREAD_TYPE pthread;
    
    const void *context = NULL;
    ym_thread_entry entry = NULL;
    if ( ! thread->isDispatch ) { // todo this should be consolidated with dispatch threads' own wrapper context
        context = YMRetain(thread);
        entry = __ym_thread_generic_entry_proc;
    }
    else {
        context = thread->context;
        entry = thread->userEntryPoint;
    }
    
#if !defined(YMWIN32)
	int result;
    if ( 0 != ( result = pthread_create(&pthread, NULL, entry, (void *)context) ) ) { // todo eagain on pi
        ymerr("pthread_create %d %s", result, strerror(result));
        goto catch_return;
    }
    thread->threadId = (uint64_t)pthread;
#else
	DWORD threadId;
	pthread = CreateThread(NULL, 0, entry, (LPVOID)context, 0, &threadId);
    if ( pthread == NULL ) {
		ymerr("CreateThread failed: %x", GetLastError());
        goto catch_return;
	}
    thread->threadId = threadId;
#endif
    
    ymlog("detached");
    
    thread->pthread = pthread;
    okay = true;
    
catch_return:
    YMRelease(thread);
    return okay;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_generic_entry_proc(YM_THREAD_PARAM thread_)
{
    __YMThreadRef thread = thread_;
    
    __YMThreadInitThreadDict(thread);
    
    ymlog("entered");
    thread->userEntryPoint((void *)thread->context);
    ymlog("exiting");
    YMRelease(thread);
    
    YM_THREAD_END
}

void __YMThreadInitThreadDict(__YMThreadRef thread)
{
    YMDictionaryAdd(thread->threadDict, (YMDictionaryKey)YMThreadDictThreadIDKey, (YMDictionaryValue)_YMThreadGetCurrentThreadNumber());
}

bool YMThreadJoin(YMThreadRef thread_)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    
    ymassert(!thread->isDispatch,"thread '%s' is a dispatch thread",YMSTR(thread->name));
    
    if ( _YMThreadGetCurrentThreadNumber() == _YMThreadGetThreadNumber(thread) )
        return false;
    
#if !defined(YMWIN32)
    int result = pthread_join(thread->pthread, NULL);
    if ( result != 0 ) {
        ymerr("pthread_join %d %s", result, strerror(result));
        return false;
    }
#else
	DWORD result = WaitForSingleObject(thread->pthread, INFINITE);
    if ( result != WAIT_OBJECT_0 ) {
		ymerr("WaitForSingleObject %x", result);
		return false;
	}
#endif
    
    return true;
}

void YMThreadDispatchDispatch(YMThreadRef thread_, struct ym_thread_dispatch_t dispatch)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    
    if ( ! thread->isDispatch ) {
        ymerr("fatal: attempt to dispatch to non-dispatch thread");
        abort();
    }
    
    __ym_thread_dispatch_context_ref newDispatch = NULL;
    YMLockLock(thread->dispatchListLock);
    {
        newDispatch = (__ym_thread_dispatch_context_ref)YMALLOC(sizeof(struct __ym_thread_dispatch_context_t));
        ym_thread_dispatch_ref dispatchCopy = __YMThreadDispatchCopy(&dispatch);
        
        newDispatch->dispatch = dispatchCopy;
        newDispatch->dispatchID = thread->dispatchIDNext++;
        
        if ( YMDictionaryContains(thread->dispatchesByID, (YMDictionaryKey)newDispatch->dispatchID) ) {
            ymerr("fatal: thread is out of dispatch ids (%zu)",YMDictionaryGetCount(thread->dispatchesByID));
            abort();
        }
        
        ymdbg("adding dispatch '%s': u %p ctx %p",YMSTR(dispatchCopy->description),dispatchCopy,dispatchCopy->context);
        YMDictionaryAdd(thread->dispatchesByID, (YMDictionaryKey)newDispatch->dispatchID, newDispatch);
    }
    YMLockUnlock(thread->dispatchListLock);
    
    YMSemaphoreSignal(thread->dispatchSemaphore);
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_dispatch_thread_proc(YM_THREAD_PARAM ctx)
{
    __ym_thread_dispatch_thread_context_ref context = (__ym_thread_dispatch_thread_context_ref)ctx;
    __YMThreadRef thread = context->ymThread;
    
    __YMThreadInitThreadDict(thread);
    
    bool *stopFlag = context->stopFlag;
    __unused YMThreadDispatchID aDispatchID = -1;
    __ym_thread_dispatch_context_ref aDispatch = NULL;
    ymlog("n%llu entered", _YMThreadGetCurrentThreadNumber());
    
    while ( true ) {
        ymdbg("n%llu begin dispatch loop",_YMThreadGetCurrentThreadNumber());
        YMSemaphoreWait(thread->dispatchSemaphore);
        
        YMLockLock(thread->dispatchListLock);
        {
            // check if we were signaled to exit
            if ( *stopFlag && ( YMDictionaryGetCount(thread->dispatchesByID) == 0 ) ) {
                ymlog("n%llu woke for exit", _YMThreadGetCurrentThreadNumber());
                YMLockUnlock(thread->dispatchListLock);
                break;
            }
            
            ymdbg("n%llu woke for a dispatch", _YMThreadGetCurrentThreadNumber());
            
            // todo this should be in order
            YMDictionaryKey randomKey = YMDictionaryGetRandomKey(thread->dispatchesByID);
            aDispatch = (__ym_thread_dispatch_context_ref)YMDictionaryRemove(thread->dispatchesByID,randomKey);
            ymassert(aDispatch,"fatal: n%llu thread signaled without target", _YMThreadGetCurrentThreadNumber());
        }
        YMLockUnlock(thread->dispatchListLock);
        
        ymdbg("n%llu entering dispatch %llu '%s': u %p ctx %p", _YMThreadGetCurrentThreadNumber(), aDispatchID, YMSTR(aDispatch->dispatch->description),aDispatch->dispatch,aDispatch->dispatch->context);
        aDispatch->dispatch->dispatchProc(aDispatch->dispatch);
        ymdbg("n%llu finished dispatch %llu '%s': u %p ctx %p", _YMThreadGetCurrentThreadNumber(), aDispatchID, YMSTR(aDispatch->dispatch->description),aDispatch->dispatch,aDispatch->dispatch->context);
        
        __YMThreadFreeDispatchContext(aDispatch);
    }
    
    ymlog("n%llu exiting", _YMThreadGetCurrentThreadNumber());
    
    // for exit, YMThread signals us after setting stop flag, we signal them back
    // so they know it's safe to deallocate our stuff
    YMSemaphoreSignal(thread->dispatchExitSemaphore);
	
	YMRelease(thread);
    
    free(context->stopFlag);
    free(context);

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
        dispatchContext->dispatch->deallocProc(dispatchContext->dispatch->context);
   
    YMRelease(dispatchContext->dispatch->description);
    
    free(dispatchContext->dispatch);
    free(dispatchContext);
}

YM_THREAD_TYPE __YMThreadGetCurrentThread()
{
#if defined(YMWIN32)
    return GetCurrentThread();
#else
    return pthread_self();
#endif
}

uint64_t _YMThreadGetCurrentThreadNumber()
{
    return (uint64_t)__YMThreadGetCurrentThread();
}

uint64_t _YMThreadGetThreadNumber(YMThreadRef thread_)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    return thread->threadId;
//    while ( ! YMDictionaryContains(thread->threadDict, (YMDictionaryKey)YMThreadDictThreadIDKey) ) {}
//    uint64_t threadId = (uint64_t)YMDictionaryGetItem(thread->threadDict, (YMDictionaryKey)YMThreadDictThreadIDKey);
//    return threadId;
}

YM_ONCE_FUNC(__YMThreadDispatchInitGlobal,
{
    YMStringRef globalName = YMSTRCF("dispatch-global-queue-%s",gDispatchGlobalModeMain?"main":"thread");
    gDispatchGlobalQueue = (__YMThreadRef)YMThreadDispatchCreate(globalName);
    YMRelease(globalName);
    
    if ( ! gDispatchGlobalModeMain )        
        YMThreadStart(gDispatchGlobalQueue);
})

void YMThreadDispatchSetGlobalMode(bool main)
{
    ymassert(!gDispatchGlobalQueue,"global dispatch mode must be set earlier");
    gDispatchGlobalModeMain = main;
    
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMThreadDispatchInitGlobal);
}

void YMThreadDispatchMain()
{
    ymassert(gDispatchGlobalModeMain,"global dispatch queue not set to main");
    ymassert(gDispatchGlobalQueue,"global dispatch queue doesn't exist");
    __YMThreadDispatchMain();
}

YMThreadRef YMThreadDispatchGetGlobal()
{
    YM_ONCE_DO(gDispatchGlobalInitOnce, __YMThreadDispatchInitGlobal);    
    return gDispatchGlobalQueue;
}

void __YMThreadDispatchMain()
{
    gDispatchGlobalQueue->pthread =
#if !defined(YMWIN32)
        pthread_self();
#else
        GetCurrentThread();
#endif
    gDispatchGlobalQueue->userEntryPoint(gDispatchGlobalQueue->context);
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
    __YMThreadRef thread = NULL;
    YMStringRef name = NULL;
    
    __ym_thread_dispatch_forward_file_async_context_ref context = YMALLOC(sizeof(struct __ym_thread_dispatch_forward_file_async_context_t));
    context->threadOrNull = NULL;
    context->file = file;
    context->stream = YMRetain(stream);
    context->toStream = toStream;
    context->bounded = ( nBytesPtr != NULL );
    context->nBytes = nBytesPtr ? *nBytesPtr : 0;
    context->sync = sync;
    context->callbackInfo = callbackInfo;
    
    if ( sync ) {
        __ym_thread_dispatch_forward_file_proc((YM_THREAD_PARAM)context);
        YMIOResult result = context->result;
        free(context);
        return ( result == YMIOSuccess || ( ! nBytesPtr && result == YMIOEOF ) );
    }
        
    // todo: new thread for all, don't let blockage on either end deny other clients of this api.
    // but, if we wanted to feel good about ourselves, these threads could hang around for a certain amount of time to handle
    // subsequent forwarding requests, to recycle the thread creation overhead.
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    name = YMSTRCF("dispatch-forward-%d%ss%llu",file,toStream?"->":"<-",streamID);
    thread = (__YMThreadRef)YMThreadCreate(name, __ym_thread_dispatch_forward_file_proc, context);
    YMRelease(name);
    if ( ! thread ) {
        ymerr("failed to create");
        goto rewind_fail;
    }
    context->threadOrNull = thread;
    
    bool okay = YMThreadStart(thread);
    if ( ! okay ) {
        ymerr("failed to start forwarding thread");
        goto rewind_fail;
    }
    
    return true;
    
rewind_fail:
    if ( thread )
        YMRelease(thread);
    free(context);
    return false;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_forward_file_proc(YM_THREAD_PARAM ctx_)
{
	__ym_thread_dispatch_forward_file_async_context_ref ctx = (__ym_thread_dispatch_forward_file_async_context_ref)ctx_;
    // todo: tired of defining semi-redundant structs for various tasks in here, should go back and take a look
    __YMThreadRef threadOrNull = ctx->threadOrNull;
    __YMThreadRef thread = threadOrNull; // logging boilerplate
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
    ymlog("forward: entered for f%d%ss%llu]",file,toStream?"->":"<-",streamID);
    uint64_t forwardBytes = bounded ? nBytes : 0;
    YMIOResult result;
    if ( toStream )
        result = YMStreamReadFromFile(stream, file, bounded ? &forwardBytes : NULL, &outBytes);
    else
        result = YMStreamWriteToFile(stream, file, bounded ? &forwardBytes : NULL, &outBytes);
    ymlog("forward: %s %llu bytes from f%d%ss%llu", (result == YMIOError)?"error at offset":"finished",outBytes,file,toStream?"->":"<-",streamID);
    
    if ( ! sync && callbackInfo->callback ) {
        YMIOResult effectiveResult = nBytes ? result : ( result == YMIOEOF );
        callbackInfo->callback(callbackInfo->context,effectiveResult,outBytes);
    }
    
    YMRelease(threadName);
    YMRelease(stream);
    if ( threadOrNull )
        YMRelease(threadOrNull);
    free(ctx->callbackInfo);
    
    if ( ! ctx->sync )
        free(ctx);
    else
        ctx->result = result;
    
    YM_THREAD_END
}

YM_EXTERN_C_POP
