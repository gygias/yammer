#include "YMDispatchUtils.h"

#include "YMStreamPriv.h"
#include "YMPlexerPriv.h"

#define ymlog_type YMLogDispatchForward
#include "YMLog.h"

typedef struct __ym_forward_file_async
{
    YMFILE file;
    YMStreamRef stream;
    bool toStream;
    bool bounded;
    size_t nBytes;
    bool sync; // only necessary to free return value
    ym_forward_file_t *callbackInfo;
    
    YMIOResult result;
} __ym_forward_file_async;
typedef struct __ym_forward_file_async __ym_forward_file_async_t;

YM_ENTRY_POINT(__ym_dispatch_forward_file_proc);
bool __YMThreadDispatchForward(YMDispatchQueueRef, YMStreamRef, YMFILE, bool, size_t *, bool, ym_forward_file_t *);

bool YMDispatchForwardFile(YMDispatchQueueRef queue, YMFILE fromFile, YMStreamRef toStream, size_t *nBytesPtr, bool sync, ym_forward_file_t *userInfo)
{
    return __YMThreadDispatchForward(queue, toStream, fromFile, true, nBytesPtr, sync, userInfo);
}

bool YMDispatchForwardStream(YMDispatchQueueRef queue, YMStreamRef fromStream, YMFILE toFile, size_t *nBytesPtr, bool sync, ym_forward_file_t *userInfo)
{
    return __YMThreadDispatchForward(queue, fromStream, toFile, false, nBytesPtr, sync, userInfo);
}

YM_ENTRY_POINT(__ym_dispatch_utils_join_func)
{
    printf("%s\n",(char *)context);
}

void YMAPI YMDispatchJoin(YMDispatchQueueRef queue)
{
    ym_dispatch_user_t user = { __ym_dispatch_utils_join_func, "join", false, ym_dispatch_user_context_noop };
    YMDispatchSync(queue,&user);
}

bool __YMThreadDispatchForward(YMDispatchQueueRef queue, YMStreamRef stream, YMFILE file, bool toStream, size_t *nBytesPtr, bool sync, ym_forward_file_t *userInfo)
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
        __ym_dispatch_forward_file_proc(context);
        YMIOResult result = context->result;
        YMFREE(context);
        return ( result == YMIOSuccess || ( ! nBytesPtr && result == YMIOEOF ) );
    }
    
    ym_dispatch_user_t user = { __ym_dispatch_forward_file_proc, context, NULL, ym_dispatch_user_context_noop };
    YMDispatchAsync(queue, &user);
    
    return true;
}

YM_ENTRY_POINT(__ym_dispatch_forward_file_proc)
{
	__ym_forward_file_async_t *async = (__ym_forward_file_async_t *)context;
    YMFILE file = async->file;
    YMStreamRef stream = async->stream;
    bool toStream = async->toStream;
    bool bounded = async->bounded;
    size_t nBytes = async->nBytes;
    bool sync = async->sync;
    ym_forward_file_t *callbackInfo = async->callbackInfo;
    
    size_t outBytes = 0;
    
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    ymlog("forward: entered for f%d%ss%"PRIu64,file,toStream?"->":"<-",streamID);
    size_t forwardBytes = bounded ? nBytes : 0;
    YMIOResult result;
    if ( toStream )
        result = YMStreamReadFromFile(stream, file, bounded ? &forwardBytes : NULL, &outBytes);
    else
        result = YMStreamWriteToFile(stream, file, bounded ? &forwardBytes : NULL, &outBytes);
    ymlog("forward: %s %zu bytes from f%d%ss%"PRIu64, (result == YMIOError)?"error at offset":"finished",outBytes,file,toStream?"->":"<-",streamID);
    
    if ( ! sync && callbackInfo->callback ) {
        YMIOResult effectiveResult = nBytes ? result : ( result == YMIOEOF );
        callbackInfo->callback(callbackInfo->context,effectiveResult,outBytes);
    }
    
    YMRelease(stream);
    YMFREE(async->callbackInfo);
    
    if ( ! async->sync )
        YMFREE(async);
    else
        async->result = result;
}

