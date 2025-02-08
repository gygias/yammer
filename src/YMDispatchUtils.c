#include "YMDispatchUtils.h"

#include "YMStreamPriv.h"
#include "YMPlexerPriv.h"

#include "YMLog.h"

typedef struct __ym_forward_file_async
{
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

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_dispatch_forward_file_proc(YM_THREAD_PARAM);
bool __YMThreadDispatchForward(YMDispatchQueueRef, YMStreamRef, YMFILE, bool, const uint64_t *, bool, ym_forward_file_t *);

bool YMDispatchForwardFile(YMDispatchQueueRef queue, YMFILE fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, ym_forward_file_t *userInfo)
{
    return __YMThreadDispatchForward(queue, toStream, fromFile, true, nBytesPtr, sync, userInfo);
}

bool YMDispatchForwardStream(YMDispatchQueueRef queue, YMStreamRef fromStream, YMFILE toFile, const uint64_t *nBytesPtr, bool sync, ym_forward_file_t *userInfo)
{
    return __YMThreadDispatchForward(queue, fromStream, toFile, false, nBytesPtr, sync, userInfo);
}

void __ym_dispatch_utils_join_func(void *ctx)
{
    printf("%s\n",(char *)ctx);
}

void YMAPI YMDispatchJoin(YMDispatchQueueRef queue)
{
    ym_dispatch_user_t user = { __ym_dispatch_utils_join_func, "join", false, ym_dispatch_user_context_noop };
    YMDispatchSync(queue,&user);
}

bool __YMThreadDispatchForward(YMDispatchQueueRef queue, YMStreamRef stream, YMFILE file, bool toStream, const uint64_t *nBytesPtr, bool sync, ym_forward_file_t *userInfo)
{
    __ym_forward_file_async_t *context = YMALLOC(sizeof(__ym_forward_file_async_t));
    context->file = file;
    context->stream = YMRetain(stream);
    context->toStream = toStream;
    context->bounded = ( nBytesPtr != NULL );
    context->nBytes = nBytesPtr ? *nBytesPtr : 0;
    context->sync = sync;
    context->callbackInfo = userInfo;
    
    if ( sync ) {
        __ym_dispatch_forward_file_proc((YM_THREAD_PARAM)context);
        YMIOResult result = context->result;
        YMFREE(context);
        return ( result == YMIOSuccess || ( ! nBytesPtr && result == YMIOEOF ) );
    }
    
    ym_dispatch_user_t user = {(void (*)(void *)) __ym_dispatch_forward_file_proc, context, NULL, ym_dispatch_user_context_noop };
    YMDispatchAsync(queue, &user);
    
    return true;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_dispatch_forward_file_proc(YM_THREAD_PARAM ctx_)
{
	__ym_forward_file_async_t *ctx = (__ym_forward_file_async_t *)ctx_;
    YMFILE file = ctx->file;
    YMStreamRef stream = ctx->stream;
    bool toStream = ctx->toStream;
    bool bounded = ctx->bounded;
    uint64_t nBytes = ctx->nBytes;
    bool sync = ctx->sync;
    ym_forward_file_t *callbackInfo = ctx->callbackInfo;
    
    uint64_t outBytes = 0;
    
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymlog("forward: entered for f%d%ss%lu",file,toStream?"->":"<-",streamID);
    uint64_t forwardBytes = bounded ? nBytes : 0;
    YMIOResult result;
    if ( toStream )
        result = YMStreamReadFromFile(stream, file, bounded ? &forwardBytes : NULL, &outBytes);
    else
        result = YMStreamWriteToFile(stream, file, bounded ? &forwardBytes : NULL, &outBytes);
    ymlog("forward: %s %lu bytes from f%d%ss%lu", (result == YMIOError)?"error at offset":"finished",outBytes,file,toStream?"->":"<-",streamID);
    
    if ( ! sync && callbackInfo->callback ) {
        YMIOResult effectiveResult = nBytes ? result : ( result == YMIOEOF );
        callbackInfo->callback(callbackInfo->context,effectiveResult,outBytes);
    }
    
    YMRelease(stream);
    YMFREE(ctx->callbackInfo);
    
    if ( ! ctx->sync )
        YMFREE(ctx);
    else
        ctx->result = result;
    
    YM_THREAD_END
}