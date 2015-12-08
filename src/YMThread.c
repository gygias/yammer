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
#define ymlog_type_debug YMLogThreadDebug
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef uint64_t YMThreadDispatchID;
typedef uint64_t YMThreadDispatchThreadID;

typedef struct __ym_thread_t
{
    _YMType _typeID;
    
    YMStringRef name;
    ym_thread_entry userEntryPoint;
    const void *context;
    YM_THREAD_TYPE pthread;
    YMDictionaryRef threadDict;
    
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

YMThreadDispatchThreadID gDispatchThreadIDNext = 0; // todo only for keying dictionary, implement linked list?
YMDictionaryRef gDispatchThreadDefsByID = NULL;
YMLockRef gDispatchThreadListLock = NULL;

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

__YMThreadRef __YMThreadInitCommon(YMStringRef name, const void *context);
void __YMThreadInitThreadDict(__YMThreadRef thread);
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

__YMThreadRef __YMThreadInitCommon(YMStringRef name, const void *context)
{
    __YMThreadRef thread = (__YMThreadRef)_YMAlloc(_YMThreadTypeID,sizeof(struct __ym_thread_t));

	YM_ONCE_DO_LOCAL(__YMThreadDispatchInit);
    
    thread->name = name ? YMRetain(name) : YMSTRC("*");
    thread->context = context;
    thread->pthread = (YM_THREAD_TYPE)NULL;
    thread->threadDict = YMDictionaryCreate();
    
    thread->didStart = false;
    
    return thread;
}

YMThreadRef YMThreadCreate(YMStringRef name, ym_thread_entry entryPoint, const void *context)
{
    __YMThreadRef thread = __YMThreadInitCommon(name, context);
    thread->userEntryPoint = entryPoint;
    thread->isDispatchThread = false;
    
    return thread;
}

YMThreadRef YMThreadDispatchCreate(YMStringRef name)
{
    __YMThreadRef thread = __YMThreadInitCommon(name, NULL);
    thread->userEntryPoint = __ym_thread_dispatch_dispatch_thread_proc;
    thread->isDispatchThread = true;
    
    YMLockLock(gDispatchThreadListLock);
    {
        thread->dispatchThreadID = gDispatchThreadIDNext++;
        ymassert(!YMDictionaryContains(gDispatchThreadDefsByID, thread->dispatchThreadID),"thread[%s,dispatch]: fatal: out of dispatch thread ids",YMSTR(thread->name));
        
        __ym_thread_dispatch_thread_context_ref dispatchThreadDef = (__ym_thread_dispatch_thread_context_ref)YMALLOC(sizeof(struct __ym_thread_dispatch_thread_context_t));
        dispatchThreadDef->ymThread = (__YMThreadRef)YMRetain(thread);
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
    __YMThreadDispatchJoin(thread);
}

__ym_thread_dispatch_thread_context_ref __YMThreadDispatchJoin(__YMThreadRef thread)
{
    __ym_thread_dispatch_thread_context_ref threadDef = NULL;
    
    bool stopped = false;
    YMLockLock(gDispatchThreadListLock);
    {
        if ( YMDictionaryContains(gDispatchThreadDefsByID, thread->dispatchThreadID) )
        {
            threadDef = (__ym_thread_dispatch_thread_context_ref)YMDictionaryRemove(gDispatchThreadDefsByID, thread->dispatchThreadID);
            *(threadDef->stopFlag) = true;
            stopped = true;
            ymlog("thread[%s,dispatch,dt%llu]: flagged pthread to exit on ymfree with %p", YMSTR(thread->name), thread->dispatchThreadID,threadDef->stopFlag);
        }
    }
    YMLockUnlock(gDispatchThreadListLock);
    
    if ( stopped )
    {
        YMSemaphoreSignal(thread->dispatchSemaphore);
        
        if ( _YMThreadGetThreadNumber(thread) != _YMThreadGetCurrentThreadNumber() )
        {
            YMSemaphoreWait(thread->dispatchExitSemaphore);
            ymlog("thread[%s,dispatch,dt%llu]: received exit signal from dispatch thread", YMSTR(thread->name), thread->dispatchThreadID);
        }
        else
            ymlog("thread[%s,dispatch,dt%llu]: joining from pthread_self, proceeding", YMSTR(thread->name), thread->dispatchThreadID);
    }
    
    return threadDef;
}

// this out-of-band-with-initializer setter was added with ForwardFile, so that YMThread could spawn and
// forget the thread and let it deallocate its own YMThread-level stuff (thread ref not known until after construction)
void YMThreadSetContext(YMThreadRef thread_, void *context)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    ymassert(!thread->didStart,"thread[%s]: fatal: cannot set context on a thread that has already started",YMSTR(thread->name));
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
    if ( ! thread->isDispatchThread ) // todo this should be consolidated with dispatch threads' own wrapper context
    {
        context = YMRetain(thread);
        entry = __ym_thread_generic_entry_proc;
    }
    else
    {
        context = thread->context;
        entry = thread->userEntryPoint;
    }
    
#ifndef WIN32
	int result;
    if ( 0 != ( result = pthread_create(&pthread, NULL, entry, (void *)context) ) ) // todo eagain on pi
    {
        ymerr("thread[%s,%s]: error: pthread_create %d %s", YMSTR(thread->name), thread->isDispatchThread?"dispatch":"user", result, strerror(result));
        goto catch_return;
    }
#else
	DWORD threadId;
	pthread = CreateThread(NULL, 0, entry, (LPVOID)context, 0, &threadId);
	if ( pthread == NULL )
	{
		ymerr("thread[%s,%s]: error: CreateThread failed: %x", YMSTR(thread->name), thread->isDispatchThread ? "dispatch" : "user", GetLastError());
        goto catch_return;
	}
#endif
    
    ymlog("thread[%s,%s]: detached", YMSTR(thread->name), thread->isDispatchThread ? "dispatch" : "user");
    
    thread->pthread = pthread;
    okay = true;
    
catch_return:
    YMRelease(thread);
    return okay;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_generic_entry_proc(YM_THREAD_PARAM theThread_)
{
    __YMThreadRef theThread = theThread_;
    
    __YMThreadInitThreadDict(theThread);
    
    ymlog("thread[%s,generic] entered",YMSTR(theThread->name));
    theThread->userEntryPoint((void *)theThread->context);
    ymlog("thread[%s,generic] exiting",YMSTR(theThread->name));
    YMRelease(theThread);
    
    YM_THREAD_END
}

void __YMThreadInitThreadDict(__YMThreadRef thread)
{
    YMDictionaryAdd(thread->threadDict, (YMDictionaryKey)YMThreadDictThreadIDKey, (YMDictionaryValue)_YMThreadGetCurrentThreadNumber());
}

bool YMThreadJoin(YMThreadRef thread_)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    
    if ( _YMThreadGetCurrentThreadNumber() == _YMThreadGetThreadNumber(thread) )
        return false;
        
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
        
        ymdbg("thread[%s,dispatch,dt%llu]: adding dispatch '%s': u %p ctx %p",YMSTR(thread->name),thread->dispatchThreadID,YMSTR(dispatchCopy->description),dispatchCopy,dispatchCopy->context);
        YMDictionaryAdd(thread->dispatchesByID, newDispatch->dispatchID, newDispatch);
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
    ymlog("thread[%s,dispatch,dt%llu,p%llu]: entered", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
    
    while ( true )
    {
        ymdbg("thread[%s,dispatch,dt%llu,p%llu]: begin dispatch loop", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
        YMSemaphoreWait(thread->dispatchSemaphore);
        
        YMLockLock(thread->dispatchListLock);
        {
            // check if we were signaled to exit
            if ( *stopFlag && ( YMDictionaryGetCount(thread->dispatchesByID) == 0 ) )
            {
                ymlog("thread[%s,dispatch,dt%llu,p%llu]: woke for exit", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
                YMLockUnlock(thread->dispatchListLock);
                break;
            }
            
            ymdbg("thread[%s,dispatch,dt%llu,p%llu]: woke for a dispatch", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
            
            // todo this should be in order
            YMDictionaryKey randomKey = YMDictionaryGetRandomKey(thread->dispatchesByID);
            aDispatch = (__ym_thread_dispatch_context_ref)YMDictionaryRemove(thread->dispatchesByID,randomKey);
            ymassert(aDispatch,"thread[%s,dispatch,dt%llu,p%llu]: fatal: thread signaled without target", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
        }
        YMLockUnlock(thread->dispatchListLock);
        
        ymdbg("thread[%s,dispatch,dt%llu,p%llu]: entering dispatch %llu '%s': u %p ctx %p", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber(), aDispatchID, YMSTR(aDispatch->dispatch->description),aDispatch->dispatch,aDispatch->dispatch->context);
        aDispatch->dispatch->dispatchProc(aDispatch->dispatch);
        ymdbg("thread[%s,dispatch,dt%llu,p%llu]: finished dispatch %llu '%s': u %p ctx %p", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber(), aDispatchID, YMSTR(aDispatch->dispatch->description),aDispatch->dispatch,aDispatch->dispatch->context);
        
        __YMThreadFreeDispatchContext(aDispatch);
    }
    
    ymlog("thread[%s,dispatch,dt%llu,p%llu]: exiting", YMSTR(thread->name), thread->dispatchThreadID, _YMThreadGetCurrentThreadNumber());
    
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
    YM_THREAD_TYPE pthread;
    
#if defined(WIN32)
	pthread = GetCurrentThread();
#else
	pthread = pthread_self();
#endif
    
    return (uint64_t)pthread;
}

uint64_t _YMThreadGetThreadNumber(YMThreadRef thread_)
{
    __YMThreadRef thread = (__YMThreadRef)thread_;
    
    while ( ! YMDictionaryContains(thread->threadDict, (YMDictionaryKey)YMThreadDictThreadIDKey) ) {}
    uint64_t threadId = (uint64_t)YMDictionaryGetItem(thread->threadDict, (YMDictionaryKey)YMThreadDictThreadIDKey);
    return threadId;
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
    
    __ym_thread_dispatch_forward_file_async_context_ref context = YMALLOC(sizeof(struct __ym_thread_dispatch_forward_file_async_context_t));
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
        __ym_thread_dispatch_forward_file_proc((YM_THREAD_PARAM)context);
        YMIOResult result = context->result;
        free(context);
        return ( result == YMIOSuccess || ( ! nBytesPtr && result == YMIOEOF ) );
    }
        
    // todo: new thread for all, don't let blockage on either end deny other clients of this api.
    // but, if we wanted to feel good about ourselves, these threads could hang around for a certain amount of time to handle
    // subsequent forwarding requests, to recycle the thread creation overhead.
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    name = YMSTRCF("dispatch-forward-%d%ss%u",file,toStream?"->":"<-",streamID);
    forwardingThread = (__YMThreadRef)YMThreadCreate(name, __ym_thread_dispatch_forward_file_proc, context);
    YMRelease(name);
    if ( ! forwardingThread )
    {
        ymerr("thread[%s]: error: failed to create",YMSTR(name));
        goto rewind_fail;
    }
    context->threadOrNull = forwardingThread;
    
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

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_thread_dispatch_forward_file_proc(YM_THREAD_PARAM ctx_)
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
    
    if ( ! ctx->sync )
        free(ctx);
    else
        ctx->result = result;
    
    YM_THREAD_END
}

YM_EXTERN_C_POP
