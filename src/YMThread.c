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
#define ymlog_args t?YMSTR(t->name):"&",(t&&t->isDispatch)?"dispatch":"&",(t&&t->dispatchID)?t->dispatchID:0
#define ymlog_type YMLogThread
#define ymlog_type_debug YMLogThreadDebug
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef uint64_t YMThreadDispatchID;
typedef uint64_t YMThreaddispatchID;

typedef struct __ym_thread
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
} __ym_thread;
typedef struct __ym_thread __ym_thread_t;

#define YMThreadDictThreadIDKey "thread-id"

// dispatch stuff
typedef struct __ym_thread_dispatch_context
{
    __ym_thread_t *t;
    bool *stopFlag;
} ___ym_thread_dispatch_thread_context_t;
typedef struct __ym_thread_dispatch_context __ym_thread_dispatch_context_t;

typedef struct __ym_thread_dispatch
{
    ym_thread_dispatch_user_t *userDispatch;
    YMThreadDispatchID dispatchID;
} ___ym_thread_dispatch_context;
typedef struct __ym_thread_dispatch __ym_thread_dispatch_t;

YMThreaddispatchID gDispatchIDNext = 0; // todo only for keying dictionary, implement linked list?
YMDictionaryRef gDispatchThreadDefsByID = NULL;
YMLockRef gDispatchThreadListLock = NULL;
bool gDispatchGlobalModeMain = false;
__ym_thread_t * gDispatchGlobalQueue = NULL;
YM_ONCE_OBJ gDispatchGlobalInitOnce = YM_ONCE_INIT;
YM_ONCE_DEF(__YMThreadDispatchInitGlobal);
void __YMThreadDispatchMain();

// private
YM_ONCE_DEF(__YMThreadDispatchInit);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_dispatch_thread_proc(YM_THREAD_PARAM);
typedef struct __ym_forward_file_async
{
    __ym_thread_t *threadOrNull;
    YMFILE file;
    YMStreamRef stream;
    bool toStream;
    bool bounded;
    uint64_t nBytes;
    bool sync; // only necessary to free return value
    ym_forward_file_t *callbackInfo;
    
    YMIOResult result;
} __ym_forward_file_async;
typedef struct __ym_forward_file_async __ym_forward_file_async_t;

__ym_thread_t * __YMThreadInitCommon(YMStringRef, void *);
void __YMThreadInitThreadDict(__ym_thread_t *);
YM_THREAD_TYPE _YMThreadGetCurrentThread();
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_generic_entry_proc(YM_THREAD_PARAM);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_forward_file_proc(YM_THREAD_PARAM);
ym_thread_dispatch_user_t *__YMThreadUserDispatchCopy(ym_thread_dispatch_user_t *);
__ym_thread_dispatch_context_t *__YMThreadDispatchJoin(__ym_thread_t *);
void __YMThreadFreeDispatch(__ym_thread_dispatch_t *);
bool __YMThreadDispatchForward(YMStreamRef, YMFILE, bool, const uint64_t *, bool, ym_forward_file_t *);

YM_ONCE_FUNC(__YMThreadDispatchInit,
{
	gDispatchThreadDefsByID = YMDictionaryCreate();
	YMStringRef name = YMSTRC("g-dispatch-list");
	gDispatchThreadListLock = YMLockCreateWithOptionsAndName(YMInternalLockType, name);
	YMRelease(name);
})

__ym_thread_t *__YMThreadInitCommon(YMStringRef name, void *context)
{
    __ym_thread_t *t = (__ym_thread_t *)_YMAlloc(_YMThreadTypeID,sizeof(__ym_thread_t));

	YM_ONCE_DO_LOCAL(__YMThreadDispatchInit);
    
    t->name = name ? YMRetain(name) : YMSTRC("*");
    t->context = context;
    t->pthread = (YM_THREAD_TYPE)NULL;
    t->threadId = 0;
    t->threadDict = YMDictionaryCreate();
    
    t->didStart = false;
    
    return t;
}

YMThreadRef YMThreadCreate(YMStringRef name, ym_thread_entry entryPoint, void *context)
{
    __ym_thread_t *t = __YMThreadInitCommon(name, context);
    t->userEntryPoint = entryPoint;
    t->isDispatch = false;
    
    return t;
}

YMThreadRef YMThreadDispatchCreate(YMStringRef name)
{
    __ym_thread_t *t = __YMThreadInitCommon(name, NULL);
    t->userEntryPoint = __ym_thread_dispatch_dispatch_thread_proc;
    t->isDispatch = true;
    
    YMLockLock(gDispatchThreadListLock);
    {
        t->dispatchID = gDispatchIDNext++;
        ymassert(!YMDictionaryContains(gDispatchThreadDefsByID, (YMDictionaryKey)t->dispatchID),"fatal: out of dispatch thread ids");
        
        __ym_thread_dispatch_context_t *dispatchThreadDef = (__ym_thread_dispatch_context_t *)YMALLOC(sizeof(__ym_thread_dispatch_context_t));
        dispatchThreadDef->t = (__ym_thread_t *)YMRetain(t);
        dispatchThreadDef->stopFlag = calloc(1, sizeof(bool));
        
        YMDictionaryAdd(gDispatchThreadDefsByID, (YMDictionaryKey)t->dispatchID, dispatchThreadDef);
        
        t->context = dispatchThreadDef;
    }
    YMLockUnlock(gDispatchThreadListLock);
    
    t->dispatchesByID = YMDictionaryCreate();
    YMStringRef memberName = YMSTRCF("%s-dispatch",name ? YMSTR(name) : "*");
    t->dispatchSemaphore = YMSemaphoreCreateWithName(memberName,0);
    YMRelease(memberName);
    
    memberName = YMSTRCF("%s-dispatch-exit",name ? YMSTR(name) : "*");
    t->dispatchExitSemaphore = YMSemaphoreCreateWithName(memberName, 0);
    YMRelease(memberName);
    
    t->dispatchIDNext = 0;
    
    return t;
}

void _YMThreadFree(YMTypeRef o_)
{
    __ym_thread_t *t = (__ym_thread_t *)o_;
    
    if ( t->isDispatch ) {
        __YMThreadDispatchJoin(t); // join if not already done
        
        YMRelease(t->dispatchesByID);
        YMRelease(t->dispatchSemaphore);
        YMRelease(t->dispatchExitSemaphore);
    }
    // todo is there anything we should reasonably do to user threads here?
    
    if ( t->threadDict )
        YMRelease(t->threadDict);
    YMRelease(t->name);
}

void YMThreadDispatchJoin(YMThreadRef t)
{
    ymassert(t->isDispatch,"thread '%s' is not a dispatch thread",YMSTR(t->name));
    __YMThreadDispatchJoin((__ym_thread_t *)t);
}

__ym_thread_dispatch_context_t *__YMThreadDispatchJoin(__ym_thread_t *t)
{
    __ym_thread_dispatch_context_t *threadDef = NULL;
    
    bool stopped = false;
    YMLockLock(gDispatchThreadListLock);
    {
        if ( YMDictionaryContains(gDispatchThreadDefsByID, (YMDictionaryKey)t->dispatchID) ) {
            threadDef = (__ym_thread_dispatch_context_t *)YMDictionaryRemove(gDispatchThreadDefsByID, (YMDictionaryKey)t->dispatchID);
            *(threadDef->stopFlag) = true;
            stopped = true;
            ymlog("flagged pthread to exit on ymfree with %p",threadDef->stopFlag);
        }
    }
    YMLockUnlock(gDispatchThreadListLock);
    
    if ( stopped ) {
        YMSemaphoreSignal(t->dispatchSemaphore);
        
        if ( _YMThreadGetThreadNumber(t) != _YMThreadGetCurrentThreadNumber() ) {
            YMSemaphoreWait(t->dispatchExitSemaphore);
            ymlog("received exit signal from dispatch thread");
        }
        else
            ymlog("joining from pthread_self, proceeding");
    }
    
    return threadDef;
}

// this out-of-band-with-initializer setter was added with ForwardFile, so that YMThread could spawn and
// forget the thread and let it deallocate its own YMThread-level stuff (thread ref not known until after construction)
void YMThreadSetContext(YMThreadRef t, void *context)
{
    ymassert(!t->didStart,"fatal: cannot set context on a thread that has already started");
    ((__ym_thread_t *)t)->context = context;
}

bool YMThreadStart(YMThreadRef t_)
{
    __ym_thread_t *t = (__ym_thread_t *)t_;
    
    YMRetain(t); // handle (normal) thread completing (and finalizing) before this method returns
    bool okay = false;
    
    YM_THREAD_TYPE pthread;
    
    const void *context = NULL;
    ym_thread_entry entry = NULL;
    if ( ! t->isDispatch ) { // todo this should be consolidated with dispatch threads' own wrapper context
        context = YMRetain(t);
        entry = __ym_thread_generic_entry_proc;
    }
    else {
        context = t->context;
        entry = t->userEntryPoint;
    }
    
#if !defined(YMWIN32)
	int result;
    if ( 0 != ( result = pthread_create(&pthread, NULL, entry, (void *)context) ) ) { // todo eagain on pi
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

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_generic_entry_proc(YM_THREAD_PARAM ctx)
{
    __ym_thread_t *t = ctx;
    
    __YMThreadInitThreadDict(t);
    
    ymlog("entered");
    t->userEntryPoint((void *)t->context);
    ymlog("exiting");
    YMRelease(t);
    
    YM_THREAD_END
}

void __YMThreadInitThreadDict(__ym_thread_t *t)
{
    YMDictionaryAdd(t->threadDict, (YMDictionaryKey)YMThreadDictThreadIDKey, (YMDictionaryValue)_YMThreadGetCurrentThreadNumber());
}

bool YMThreadJoin(YMThreadRef t)
{
    ymassert(!t->isDispatch,"thread '%s' is a dispatch thread",YMSTR(t->name));
    
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

void YMThreadDispatchDispatch(YMThreadRef t_, ym_thread_dispatch_user_t userDispatch)
{
    __ym_thread_t *t = (__ym_thread_t *)t_;
    
    if ( ! t->isDispatch )
        ymabort("fatal: attempt to dispatch to non-dispatch thread");
    
    __ym_thread_dispatch_t *newDispatch = NULL;
    YMSelfLock(t);
    {
        newDispatch = (__ym_thread_dispatch_t *)YMALLOC(sizeof(__ym_thread_dispatch_t));
        ym_thread_dispatch_user_t *dispatchCopy = __YMThreadUserDispatchCopy(&userDispatch);
        
        newDispatch->userDispatch = dispatchCopy;
        newDispatch->dispatchID = t->dispatchIDNext++;
        
        if ( YMDictionaryContains(t->dispatchesByID, (YMDictionaryKey)newDispatch->dispatchID) )
            ymabort("fatal: thread is out of dispatch ids (%zu)",YMDictionaryGetCount(t->dispatchesByID));
        
        ymdbg("adding dispatch '%s': u %p ctx %p",YMSTR(dispatchCopy->description),dispatchCopy,dispatchCopy->context);
        YMDictionaryAdd(t->dispatchesByID, (YMDictionaryKey)newDispatch->dispatchID, newDispatch);
    }
    YMSelfUnlock(t);
    
    YMSemaphoreSignal(t->dispatchSemaphore);
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_dispatch_thread_proc(YM_THREAD_PARAM ctx)
{
    __ym_thread_dispatch_context_t *context = (__ym_thread_dispatch_context_t *)ctx;
    __ym_thread_t *t = context->t;
    
    __YMThreadInitThreadDict(t);
    
    bool *stopFlag = context->stopFlag;
    __unused YMThreadDispatchID aDispatchID = -1;
    __ym_thread_dispatch_t *aDispatch = NULL;
    ymlog("n%llu entered", _YMThreadGetCurrentThreadNumber());
    
    while ( true ) {
        ymdbg("n%llu begin dispatch loop",_YMThreadGetCurrentThreadNumber());
        YMSemaphoreWait(t->dispatchSemaphore);
        
        YMSelfLock(t);
        {
            // check if we were signaled to exit
            if ( *stopFlag && ( YMDictionaryGetCount(t->dispatchesByID) == 0 ) ) {
                ymlog("n%llu woke for exit", _YMThreadGetCurrentThreadNumber());
                YMSelfUnlock(t);
                break;
            }
            
            ymdbg("n%llu woke for a dispatch", _YMThreadGetCurrentThreadNumber());
            
            // todo this should be in order
            YMDictionaryKey randomKey = YMDictionaryGetRandomKey(t->dispatchesByID);
            aDispatch = (__ym_thread_dispatch_t *)YMDictionaryRemove(t->dispatchesByID,randomKey);
            ymassert(aDispatch,"fatal: n%llu thread signaled without target", _YMThreadGetCurrentThreadNumber());
        }
        YMSelfUnlock(t);
        
        ymdbg("n%llu entering dispatch %llu '%s': u %p ctx %p", _YMThreadGetCurrentThreadNumber(), aDispatchID, YMSTR(aDispatch->userDispatch->description),aDispatch->userDispatch,aDispatch->userDispatch->context);
        aDispatch->userDispatch->dispatchProc(aDispatch->userDispatch->context);
        ymdbg("n%llu finished dispatch %llu '%s': u %p ctx %p", _YMThreadGetCurrentThreadNumber(), aDispatchID, YMSTR(aDispatch->userDispatch->description),aDispatch->userDispatch,aDispatch->userDispatch->context);
        
        __YMThreadFreeDispatch(aDispatch);
    }
    
    ymlog("n%llu exiting", _YMThreadGetCurrentThreadNumber());
    
    // for exit, YMThread signals us after setting stop flag, we signal them back
    // so they know it's safe to deallocate our stuff
    YMSemaphoreSignal(t->dispatchExitSemaphore);
	
	YMRelease(t);
    
    free(context->stopFlag);
    free(context);

	YM_THREAD_END
}

ym_thread_dispatch_user_t *__YMThreadUserDispatchCopy(ym_thread_dispatch_user_t *userDispatch)
{
    ym_thread_dispatch_user_t *copy = YMALLOC(sizeof(ym_thread_dispatch_user_t));
    copy->dispatchProc = userDispatch->dispatchProc;
    copy->context = userDispatch->context;
    copy->freeContextWhenDone = userDispatch->freeContextWhenDone;
    copy->deallocProc = userDispatch->deallocProc;
    copy->description = userDispatch->description ? YMRetain(userDispatch->description) : YMSTRC("*");
    
    return copy;
}

void __YMThreadFreeDispatch(__ym_thread_dispatch_t *dispatch)
{
    if ( dispatch->userDispatch->freeContextWhenDone )
        free(dispatch->userDispatch->context);
    else if ( dispatch->userDispatch->deallocProc )
        dispatch->userDispatch->deallocProc(dispatch->userDispatch->context);
   
    if ( dispatch->userDispatch->description )
        YMRelease(dispatch->userDispatch->description);
    
    free(dispatch->userDispatch);
    free(dispatch);
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

uint64_t _YMThreadGetThreadNumber(YMThreadRef t)
{
    return t->threadId;
}

YM_ONCE_FUNC(__YMThreadDispatchInitGlobal,
{
    YMStringRef globalName = YMSTRCF("dispatch-global-queue-%s",gDispatchGlobalModeMain?"main":"thread");
    gDispatchGlobalQueue = (__ym_thread_t *)YMThreadDispatchCreate(globalName);
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

bool YMThreadDispatchForwardFile(YMFILE fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, ym_forward_file_t *userInfo)
{
    return __YMThreadDispatchForward(toStream, fromFile, true, nBytesPtr, sync, userInfo);
}

bool YMThreadDispatchForwardStream(YMStreamRef fromStream, YMFILE toFile, const uint64_t *nBytesPtr, bool sync, ym_forward_file_t *userInfo)
{
    return __YMThreadDispatchForward(fromStream, toFile, false, nBytesPtr, sync, userInfo);
}

bool __YMThreadDispatchForward(YMStreamRef stream, YMFILE file, bool toStream, const uint64_t *nBytesPtr, bool sync, ym_forward_file_t *userInfo)
{
    __ym_thread_t *t = NULL;
    YMStringRef name = NULL;
    
    __ym_forward_file_async_t *context = YMALLOC(sizeof(__ym_forward_file_async_t));
    context->threadOrNull = NULL;
    context->file = file;
    context->stream = YMRetain(stream);
    context->toStream = toStream;
    context->bounded = ( nBytesPtr != NULL );
    context->nBytes = nBytesPtr ? *nBytesPtr : 0;
    context->sync = sync;
    context->callbackInfo = userInfo;
    
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
    t = (__ym_thread_t *)YMThreadCreate(name, __ym_thread_dispatch_forward_file_proc, context);
    YMRelease(name);
    if ( ! t ) {
        ymerr("failed to create");
        goto rewind_fail;
    }
    context->threadOrNull = t;
    
    bool okay = YMThreadStart(t);
    if ( ! okay ) {
        ymerr("failed to start forwarding thread");
        goto rewind_fail;
    }
    
    return true;
    
rewind_fail:
    if ( t )
        YMRelease(t);
    free(context);
    return false;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_forward_file_proc(YM_THREAD_PARAM ctx_)
{
	__ym_forward_file_async_t *ctx = (__ym_forward_file_async_t *)ctx_;
    // todo: tired of defining semi-redundant structs for various tasks in here, should go back and take a look
    __ym_thread_t *t = ctx->threadOrNull;
    YMStringRef threadName = t ? YMRetain(t->name) : YMSTRC("*");
    YMFILE file = ctx->file;
    YMStreamRef stream = ctx->stream;
    bool toStream = ctx->toStream;
    bool bounded = ctx->bounded;
    uint64_t nBytes = ctx->nBytes;
    bool sync = ctx->sync;
    ym_forward_file_t *callbackInfo = ctx->callbackInfo;
    
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
    if ( t )
        YMRelease(t);
    free(ctx->callbackInfo);
    
    if ( ! ctx->sync )
        free(ctx);
    else
        ctx->result = result;
    
    YM_THREAD_END
}

YM_EXTERN_C_POP
