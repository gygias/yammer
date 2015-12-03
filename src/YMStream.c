//
//  YMStream.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMStream.h"

#include "YMStreamPriv.h"
#include "YMPipe.h"
#include "YMPipePriv.h"
#include "YMLock.h"

#include "YMUtilities.h"
#include "YMSemaphore.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogStream
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

//#define LOG_STREAM_LIFECYCLE
#ifdef LOG_STREAM_LIFECYCLE
#undef LOG_STREAM_LIFECYCLE
#define LOG_STREAM_LIFECYCLE(x) \
                ymerr("  stream[%s,if%d->of%dV,^of%d<-if%d]: %p %sallocating",YMSTR(stream->name), \
                      YMPipeGetInputFile(stream->downstreamPipe), \
                      YMPipeGetOutputFile(stream->downstreamPipe), \
                      YMPipeGetOutputFile(stream->upstreamPipe), \
                      YMPipeGetInputFile(stream->upstreamPipe), \
                      stream, (x) ? "":"de" );
#else
#define LOG_STREAM_LIFECYCLE(x) ;
#endif

typedef struct __ym_stream_t
{
    _YMType _type;
    
    YMPipeRef upstreamPipe;
    bool upstreamWriteClosed;
    bool upstreamReadClosed;
    YMPipeRef downstreamPipe;
    bool downstreamWriteClosed;
    bool downstreamReadClosed;
    YMStringRef name;
    
    _ym_stream_data_available_func dataAvailableFunc;
    void *dataAvailableContext;
    _ym_stream_free_user_info_func freeUserInfoFunc; // made necessary because clients can hold stream objs longer than we do
    
    bool direct;
    
    ym_stream_user_info_ref userInfo; // weak, plexer
} __ym_stream_t;
typedef struct __ym_stream_t *__YMStreamRef;

void __YMStreamCloseFiles(__YMStreamRef stream);
YMIOResult __YMStreamForward(__YMStreamRef stream, YMFILE file, bool toStream, uint64_t *inBytes, uint64_t *outBytes);

YMStreamRef _YMStreamCreate(YMStringRef name, ym_stream_user_info_ref userInfo, _ym_stream_free_user_info_func callback)
{
    __YMStreamRef stream = (__YMStreamRef)_YMAlloc(_YMStreamTypeID,sizeof(struct __ym_stream_t));
    
    stream->name = name ? YMRetain(name) : YMSTRC("*");
    
    YMStringRef pipeName = YMStringCreateWithFormat("%s-up",YMSTR(name),NULL);
    stream->upstreamPipe = YMPipeCreate(pipeName);
    YMRelease(pipeName);
    stream->upstreamWriteClosed = false;
    stream->upstreamReadClosed = false;
    
    pipeName = YMStringCreateWithFormat("%s-down",YMSTR(name),NULL);
    stream->downstreamPipe = YMPipeCreate(pipeName);
    YMRelease(pipeName);
    stream->downstreamWriteClosed = false;
    stream->downstreamReadClosed = false;
    
    stream->dataAvailableFunc = NULL;
    stream->dataAvailableContext = NULL;
    
    stream->userInfo = userInfo;
    stream->freeUserInfoFunc = callback;
    
    LOG_STREAM_LIFECYCLE(true);
    
    return (YMStreamRef)stream;
}

void _YMStreamSetDataAvailableCallback(YMStreamRef stream_, _ym_stream_data_available_func func, void *ctx)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    stream->dataAvailableFunc = func;
    stream->dataAvailableContext = ctx;
}

void _YMStreamFree(YMTypeRef object)
{
    __YMStreamRef stream = (__YMStreamRef)object;
    
    LOG_STREAM_LIFECYCLE(false);
    
    if ( stream->freeUserInfoFunc )
        stream->freeUserInfoFunc(stream);
    
    YMRelease(stream->downstreamPipe);
    YMRelease(stream->upstreamPipe);
    YMRelease(stream->name);
}

// because user data is opaque (even to user), this should expose eof
YMIOResult YMStreamReadUp(YMStreamRef stream_, void *buffer, uint16_t length, uint16_t *outLength)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    YMFILE upstreamRead = YMPipeGetOutputFile(stream->upstreamPipe);
    
    ymlog("  stream[%s]: reading %ub user data",YMSTR(stream->name),length);
    size_t actualLength = 0;
    YMIOResult result = YMReadFull(upstreamRead, buffer, length, &actualLength);
    if ( outLength )
        *outLength = (uint16_t)actualLength;
    if ( result == YMIOError )
        ymerr("  stream[%s]: fatal: reading %ub user data: %d (%s)",YMSTR(stream->name),length,errno,strerror(errno));
    else if ( result == YMIOEOF )
        ymerr("  stream[%s]: EOF from upstream",YMSTR(stream->name));
    else
        ymlog("  stream[%s]: read %ub from upstream",YMSTR(stream->name),length);
    
    return result;
}

YMIOResult YMStreamWriteDown(YMStreamRef stream_, const void *buffer, uint16_t length)
{
    YM_DEBUG_CHUNK_SIZE(length);
    
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    YMIOResult result;
    YMFILE downstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    
#ifdef USE_STREAM_COMMANDS_DOWN
    YMStreamCommand header = { length };
    ymlog("  stream[%s]: writing header for command: %ub",YMSTR(stream->name),length);
    result = YMWriteFull(downstreamWrite, (void *)&header, sizeof(header), NULL);
    if ( result != YMIOSuccess )
    {
        ymerr("  stream[%s]: failed writing header for stream chunk size %ub",YMSTR(stream->name),length);
        return result;
    }
    ymlog("  stream[%s]: wrote header for command: %u",YMSTR(stream->name),length);
#endif
    
    result = YMWriteFull(downstreamWrite, buffer, length, NULL);
    if ( result != YMIOSuccess )
    {
        ymerr("  stream[%s]: failed writing stream chunk with size %ub",YMSTR(stream->name),length);
        return result;
    }
    ymlog("  stream[%s]: wrote buffer for chunk with size %ub",YMSTR(stream->name),length);
    
    // signal the plexer to wake and service this stream
    stream->dataAvailableFunc(stream,length,stream->dataAvailableContext);
    
    ymlog("  stream[%s]: wrote %ub chunk",YMSTR(stream->name),length);
    
    return result;
}

// void: only ever does in-process i/o
YMIOResult _YMStreamReadDown(YMStreamRef stream_, void *buffer, uint32_t length)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
	YMFILE downstreamRead = YMPipeGetOutputFile(stream->downstreamPipe);
    ymlog("  stream[%s]: reading %ub from downstream",YMSTR(stream->name),length);
    YMIOResult result = YMReadFull(downstreamRead, buffer, length, NULL);
    if ( result != YMIOSuccess )
        ymerr("  stream[%s]: error: failed reading %ub from downstream",YMSTR(stream->name),length);
    else
        ymlog("  stream[%s]: read %ub from downstream",YMSTR(stream->name),length);
    return result;
}

// void: only ever does in-process i/o
YMIOResult _YMStreamWriteUp(YMStreamRef stream_, const void *buffer, uint32_t length)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    YMFILE upstreamWrite = YMPipeGetInputFile(stream->upstreamPipe);
    
    YMIOResult result = YMWriteFull(upstreamWrite, buffer, length, NULL);
    if ( result == YMIOError )
        ymerr("  stream[%s]: fatal: failed writing %u bytes to upstream",YMSTR(stream->name),length);
    else
        ymlog("  stream[%s]: wrote %ub to upstream",YMSTR(stream->name),length);
    
    return result;
}

YMIOResult YMStreamWriteToFile(YMStreamRef stream_, YMFILE file, uint64_t *inBytes, uint64_t *outBytes)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    return __YMStreamForward(stream, file, false, inBytes, outBytes);
}

YMIOResult YMStreamReadFromFile(YMStreamRef stream_, YMFILE file, uint64_t *inBytes, uint64_t *outBytes)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    return __YMStreamForward(stream, file, true, inBytes, outBytes);
}

// for the love of god break this into at least 2 functions
YMIOResult __YMStreamForward(__YMStreamRef stream, YMFILE file, bool fromFileToStream, uint64_t *inBytes, uint64_t *outBytes)
{
    uint64_t off = 0;
    bool boundedByCallerOrRemote = ( inBytes != NULL );
    uint64_t remainingIfBounded = inBytes ? *inBytes : 0;
    
    bool lastIter = false;
    uint16_t bufferSize = 16384;
    void *buffer = YMALLOC(bufferSize);
    uint16_t debugOutLength = 0;
    YMIOResult aResult = YMIOError; // i guess if called with 0 it's an 'error' (?)
        
    // if we're writing to the stream, write whether we're bounded, and if so the file length
    if ( fromFileToStream )
    {
        _YMStreamCommand prefixCommand = { boundedByCallerOrRemote ? YMStreamForwardFileBounded : YMStreamForwardFileUnbounded };
        aResult = YMStreamWriteDown(stream, &prefixCommand, sizeof(prefixCommand));
        ymsoftassert(aResult==YMIOSuccess, "write forward init");
        
        if ( boundedByCallerOrRemote )
        {
            _YMStreamBoundType length = *inBytes;
            aResult = YMStreamWriteDown(stream, &length, sizeof(length));
            ymsoftassert(aResult==YMIOSuccess, "write forward bound");
            ymlog("  stream[%s]: beginning bounded file write (%llub)",YMSTR(stream->name),length);
        }
        else
            ymlog("  stream[%s]: beginning unbounded file write",YMSTR(stream->name));
    }
    // if we're writing to file, read the coordination stuff above
    else
    {
        _YMStreamCommand peekCommand;
        uint16_t peekLength = sizeof(_YMStreamCommand);
        aResult = YMStreamReadUp(stream, &peekCommand, peekLength, &debugOutLength);
        ymsoftassert(aResult==YMIOSuccess&&debugOutLength==peekLength,"peek forward command");
        
        if ( peekCommand.command == YMStreamForwardFileBounded )
        {
            _YMStreamBoundType length;
            debugOutLength = 0;
            aResult = YMStreamReadUp(stream, &length, sizeof(length), &debugOutLength);
            ymsoftassert(aResult==YMIOSuccess&&debugOutLength==sizeof(length),"peek forward length");
            
            remainingIfBounded = length;
            boundedByCallerOrRemote = true;
            ymlog("  stream[%s]: beginning remote-bounded file read (%llub)",YMSTR(stream->name),remainingIfBounded);
        }
        else if ( peekCommand.command == YMStreamForwardFileUnbounded )
        {
            ymlog("  stream[%s]: beginning unbounded file read",YMSTR(stream->name));
            boundedByCallerOrRemote = false; // expect a 'forward end' command
        }
        else ymsoftassert(false,"peek forward command bogus");
    }
    
    uint16_t aActualLength = 0;
    do
    {
        size_t outWritten = 0;
        uint16_t aDesiredLength = boundedByCallerOrRemote ? ( remainingIfBounded < bufferSize ? (uint16_t)remainingIfBounded : bufferSize ) : bufferSize;
        
        if ( fromFileToStream )
        {
            aResult = YMReadFull(file, buffer, aDesiredLength, (size_t *)&aActualLength);
            ymsoftassert(aResult==YMIOSuccess&&aActualLength==aDesiredLength||aResult==YMIOEOF,"read user forward file");
        }
        else // stream to file
        {
            if ( ! boundedByCallerOrRemote )
            {
                _YMStreamCommand aCommand;
                debugOutLength = 0;
                aResult = YMStreamReadUp(stream, &aCommand, sizeof(aCommand), &debugOutLength);
                ymsoftassert(aResult==YMIOSuccess&&debugOutLength==sizeof(aCommand),"read a forward command");
                
                if ( aCommand.command == YMStreamForwardFileEnd )
                {
                    aDesiredLength = 0; // omg refactor this function
                    aActualLength = 0;
                    aResult = YMIOEOF;
                    goto unbounded_no_raw_read;
                }
                else if ( aCommand.command > 0 )
                {
                    if ( aCommand.command > UINT16_MAX ) abort();
                    aDesiredLength = (uint16_t)aCommand.command;
                }
                else ymsoftassert(false,"read a forward command");
            }
            
            aResult = YMStreamReadUp(stream, buffer, aDesiredLength, &aActualLength);
        }
        
    unbounded_no_raw_read:
        ymsoftassert(aDesiredLength==aActualLength||aResult==YMIOEOF,"forward early eof");
        
        if ( aResult == YMIOError )
        {
            ymerr("  stream[%s]: error: f%d%s forward read %llu-%llu: %d (%s)",YMSTR(stream->name),file,fromFileToStream?"->":"<-",off,off+aActualLength,errno,strerror(errno));
            break;
        }
        else if ( aResult == YMIOEOF )
            lastIter = true;
        else if ( fromFileToStream && aDesiredLength != aActualLength )
            ymerr("  stream[%s]: warning: f%d%s forward read %llu-%llu %u != %u: %d (%s)",YMSTR(stream->name),file,fromFileToStream?"->":"<-",off,off+aDesiredLength,aActualLength,aDesiredLength,errno,strerror(errno));
        else
            ymlog("  stream[%s]: f%d%s read %ub (%llu) remaining...",YMSTR(stream->name),file,fromFileToStream?"F>S":"F<S",aActualLength,remainingIfBounded);
        
        if ( fromFileToStream )
        {
            // if we're unbounded, write a chunk header if we got raw data, raw data if it exists, and 'unbounded end' if we hit EOF
            if ( ! boundedByCallerOrRemote && aActualLength > 0 )
            {
                _YMStreamCommand aChunkCommand = { aActualLength };
                aResult = YMStreamWriteDown(stream, &aChunkCommand, sizeof(aChunkCommand));
                ymsoftassert(aResult==YMIOSuccess, "write forward chunk length");
            }
            
            if ( aActualLength > 0 )
            {
                aResult = YMStreamWriteDown(stream, buffer, aActualLength);
                ymsoftassert(aResult==YMIOSuccess, "write forward chunk");
            }
        }
        else
        {
            if ( aActualLength > 0 )
            {
                aResult = YMWriteFull(file, buffer, aActualLength, &outWritten);
                //fsync(file); // REMOVE ME
                ymsoftassert(aActualLength==outWritten,"write forward file");
            }
        }
        
        if ( aResult == YMIOError )
        {
            ymerr("  stream[%s]: error: f%d%s forward write %llu-%llu: %d (%s)",YMSTR(stream->name),file,fromFileToStream?"->":"<-",off,off+aActualLength,errno,strerror(errno));
            lastIter = true;
        }
        else if ( ! fromFileToStream && aActualLength != outWritten )
        {
            ymerr("  stream[%s]: warning: f%d%s forward write %llu-%llu %zu != %u: %d (%s)",YMSTR(stream->name),file,fromFileToStream?"->":"<-",off,off+aActualLength,outWritten,aActualLength,errno,strerror(errno));
            ymsoftassert(false, "forward read/write mismatch");
        }
        
        if ( boundedByCallerOrRemote )
        {
            ymsoftassert(aActualLength<=remainingIfBounded,"forward exceeded bounds");
            
            remainingIfBounded -= aActualLength;
            if ( remainingIfBounded == 0 )
                lastIter = true;
        }
        
        ymlog("  stream[%s]: %s wrote [%llu,%ub] (%llu remaining)...",YMSTR(stream->name),fromFileToStream?"F>S":"F<S",off,aActualLength,remainingIfBounded);
        
        off += aActualLength;
    } while ( ! lastIter );
    
    free(buffer);
    
    if ( outBytes )
        *outBytes = off;
    
    if ( ! boundedByCallerOrRemote && fromFileToStream )
    {
        _YMStreamCommand endCommand = { YMStreamForwardFileEnd };
        aResult = YMStreamWriteDown(stream, &endCommand, sizeof(endCommand));
        ymsoftassert(aResult==YMIOSuccess, "write forward file end");
        ymlog("  stream[%s]: wrote ForwardFileEnd",YMSTR(stream->name));
    }
    
    if ( inBytes && off != *inBytes )
    {
        ymerr("  stream[%s]: warning: forwarded %llu bytes of requested %llu",YMSTR(stream->name),off,*inBytes);
        ymsoftassert(false,"forward bounds mismatch");
    }
    else
    {
        if ( fromFileToStream ) { ymlog("  stream[%s]: forwarded %llu bytes from %llu to stream",YMSTR(stream->name),off,(unsigned long long)file); }
        else { ymlog("  stream[%s]: forwarded %llu bytes to stream from %llu",YMSTR(stream->name),off,(unsigned long long)file); }
    }
    
    return aResult;
}

void _YMStreamCloseWriteUp(YMStreamRef stream_)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    _YMPipeCloseInputFile(stream->upstreamPipe);
}

void _YMStreamSendClose(YMStreamRef stream_)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    YMFILE downstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    
    _YMStreamCommand command = { YMStreamClose };
    YMIOResult result = YMWriteFull(downstreamWrite, (void *)&command, sizeof(command), NULL);
    if ( result != YMIOSuccess )
    {
        ymerr("  stream[%s]: fatal: writing close byte to plexer: %d (%s)",YMSTR(stream->name),errno,strerror(errno));
        abort();
    }
    
    ymlog("  stream[%s]: closing stream",YMSTR(stream->name));
    stream->dataAvailableFunc(stream,sizeof(command),stream->dataAvailableContext);
}

ym_stream_user_info_ref _YMStreamGetUserInfo(YMStreamRef stream_)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    return (ym_stream_user_info_ref)stream->userInfo;
}

YMStringRef _YMStreamGetName(YMStreamRef stream_)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    return stream->name;
}
