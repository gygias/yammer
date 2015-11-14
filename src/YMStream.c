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
#include "YMLock.h"

#ifdef USE_FTIME
#include <sys/timeb.h>
#error todo
#else
#include <sys/time.h>
#endif

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogStream
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

typedef struct __ym_stream
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
    
    bool direct;
    
    ym_stream_user_info_ref userInfo; // weak, plexer
} ___ym_stream;
typedef struct __ym_stream __YMStream;
typedef __YMStream *__YMStreamRef;

void __YMStreamCloseFiles(__YMStreamRef stream);
YMIOResult __YMStreamForward(__YMStreamRef stream, int file, bool toStream, uint64_t *inBytes, uint64_t *outBytes);

YMStreamRef _YMStreamCreate(YMStringRef name, ym_stream_user_info_ref userInfo)
{
    __YMStreamRef stream = (__YMStreamRef)_YMAlloc(_YMStreamTypeID,sizeof(__YMStream));
    
    stream->name = name ? YMRetain(name) : YMSTRC("unnamed");
    
    YMStringRef streamName = YMStringCreateWithFormat("%s-up",YMSTR(name),NULL);
    stream->upstreamPipe = YMPipeCreate(streamName);
    YMRelease(streamName);
    stream->upstreamWriteClosed = false;
    stream->upstreamReadClosed = false;
    
    streamName = YMStringCreateWithFormat("%s-down",YMSTR(name),NULL);
    stream->downstreamPipe = YMPipeCreate(streamName);
    YMRelease(streamName);
    stream->downstreamWriteClosed = false;
    stream->downstreamReadClosed = false;
    
    stream->dataAvailableFunc = NULL;
    stream->dataAvailableContext = NULL;
    
    stream->userInfo = userInfo;
//    stream->__dataAvailableSemaphore = NULL;
//    stream->__lastServiceTime = (struct timeval *)YMALLOC(sizeof(struct timeval));
//    if ( 0 != gettimeofday(stream->__lastServiceTime, NULL) )
//    {
//        ymlog("warning: error setting initial service time for stream: %d (%s)",errno,strerror(errno));
//        YMGetTheBeginningOfPosixTimeForCurrentPlatform(stream->__lastServiceTime);
//    }
//    
//    stream->retainLock = YMLockCreateWithOptionsAndName(YMLockDefault, stream->name);
//    stream->isUserReleased = false;
//    stream->isPlexerReleased = false;
//    stream->isDeallocated = false;
    
    if ( ymlog_stream_lifecycle )
        YMLogType(YMLogStreamLifecycle,"  stream[%s,i%d->o%dV,^o%d<-i%d]: %p allocating",YMSTR(stream->name),
          YMPipeGetInputFile(stream->downstreamPipe),
          YMPipeGetOutputFile(stream->downstreamPipe),
          YMPipeGetOutputFile(stream->upstreamPipe),
          YMPipeGetInputFile(stream->upstreamPipe),
          stream);
    
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
    ymerr("  stream[%s,i%d->o%dV,^o%d<-i%d]: %p deallocating",YMSTR(stream->name),
          YMPipeGetInputFile(stream->downstreamPipe),
          YMPipeGetOutputFile(stream->downstreamPipe),
          YMPipeGetOutputFile(stream->upstreamPipe),
          YMPipeGetInputFile(stream->upstreamPipe),
          stream);
    
    if ( ymlog_stream_lifecycle )
        YMLogType(YMLogStreamLifecycle,"  stream[%s,i%d->o%dV,^o%d<-i%d]: %p deallocating",YMSTR(stream->name),
                  YMPipeGetInputFile(stream->downstreamPipe),
                  YMPipeGetOutputFile(stream->downstreamPipe),
                  YMPipeGetOutputFile(stream->upstreamPipe),
                  YMPipeGetInputFile(stream->upstreamPipe),
                  stream);
    
    YMRelease(stream->downstreamPipe);
    YMRelease(stream->upstreamPipe);
    YMRelease(stream->name);
}

void YMStreamWriteDown(YMStreamRef stream_, const void *buffer, uint16_t length)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    int downstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    
    YMStreamCommand header = { length };
    ymlog("  stream[%s]: writing header for command: %ub",YMSTR(stream->name),length);
    YMIOResult result = YMWriteFull(downstreamWrite, (void *)&header, sizeof(header), NULL);
    if ( result != YMIOSuccess )
    {
        ymerr("  stream[%s]: fatal: failed writing header for stream chunk size %ub",YMSTR(stream->name),length);
        abort();
    }
    ymlog("  stream[%s]: wrote header for command: %u",YMSTR(stream->name),length);
    
    result = YMWriteFull(downstreamWrite, buffer, length, NULL);
    if ( result != YMIOSuccess )
    {
        ymerr("  stream[%s]: fatal: failed writing stream chunk with size %ub",YMSTR(stream->name),length);
        abort();
    }
    ymlog("  stream[%s]: wrote buffer for chunk with size %ub",YMSTR(stream->name),length);
    
    // signal the plexer to wake and service this stream
    stream->dataAvailableFunc(stream->dataAvailableContext);
    
    ymlog("  stream[%s]: wrote %lub + %ub command",YMSTR(stream->name),sizeof(header),length);
}

// void: only ever does in-process i/o
void _YMStreamReadDown(YMStreamRef stream_, void *buffer, uint32_t length)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    int downstreamRead = YMPipeGetOutputFile(stream->downstreamPipe);
    ymlog("  stream[%s]: reading %ub from downstream",YMSTR(stream->name),length);
    YMIOResult result = YMReadFull(downstreamRead, buffer, length, NULL);
    if ( result != YMIOSuccess )
    {
        ymerr("  stream[%s]: error: failed reading %ub from downstream",YMSTR(stream->name),length);
        abort();
    }
    
    ymlog("  stream[%s]: read %ub from downstream",YMSTR(stream->name),length);
}

// void: only ever does in-process i/o
void _YMStreamWriteUp(YMStreamRef stream_, const void *buffer, uint32_t length)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    int upstreamWrite = YMPipeGetInputFile(stream->upstreamPipe);
    
    YMIOResult result = YMWriteFull(upstreamWrite, buffer, length, NULL);
    if ( result == YMIOError )
    {
        ymerr("  stream[%s]: fatal: failed writing %u bytes to upstream",YMSTR(stream->name),length);
        abort();
    }
}

// because user data is opaque (even to user), this should expose eof
YMIOResult YMStreamReadUp(YMStreamRef stream_, void *buffer, uint16_t length)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    int upstreamRead = YMPipeGetOutputFile(stream->upstreamPipe);
    
    ymlog("  stream[%s]: reading %ub user data",stream->name,length);
    YMIOResult result = YMReadFull(upstreamRead, buffer, length, NULL);
    if ( result == YMIOError ) // in-process i/o errors are fatal
    {
        ymerr("  stream[%s]: fatal: reading %ub user data: %d (%s)",YMSTR(stream->name),length,errno,strerror(errno));
        abort();
    }
    else if ( result == YMIOEOF )
        ymerr("  stream[%s]: EOF from upstream",YMSTR(stream->name));
    else
        ymlog("  stream[%s]: read %ub from upstream",YMSTR(stream->name),length);
    
    return result;
}

YMIOResult YMStreamWriteToFile(YMStreamRef stream_, int file, uint64_t *inBytes, uint64_t *outBytes)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    return __YMStreamForward(stream, file, false, inBytes, outBytes);
}

YMIOResult YMStreamReadFromFile(YMStreamRef stream_, int file, uint64_t *inBytes, uint64_t *outBytes)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    return __YMStreamForward(stream, file, true, inBytes, outBytes);
}

YMIOResult __YMStreamForward(__YMStreamRef stream, int file, bool toStream, uint64_t *inBytes, uint64_t *outBytes)
{
    uint64_t off = 0;
    uint64_t remainingIfInBytes = inBytes ? *inBytes : 0;
    
    bool lastIter = false;
    uint16_t bufferSize = 16384;
    void *buffer = YMALLOC(bufferSize);
    
    YMIOResult aResult = YMIOError; // i guess if called with 0 it's an 'error' (?)
    size_t aBytes = 0;
    do
    {
        uint16_t aChunkSize = inBytes ? ( remainingIfInBytes < bufferSize ? (uint16_t)remainingIfInBytes : bufferSize ) : bufferSize;
        
        if ( toStream )
            aResult = YMReadFull(file, buffer, aChunkSize, &aBytes);
        else
            aResult = YMStreamReadUp(stream, buffer, aChunkSize);
            
        if ( aResult == YMIOError )
        {
            ymerr("stream[%s]: error: %d%s forward read %llu-%llu: %d (%s)",YMSTR(stream->name),file,toStream?"->":"<-",off,off+aChunkSize,errno,strerror(errno));
            break;
        }
        else if ( aResult == YMIOEOF )
            lastIter = true;
        else if ( toStream && aChunkSize != aBytes )
            ymerr("stream[%s]: warning: %d%s forward read %llu-%llu %u != %zu: %d (%s)",YMSTR(stream->name),file,toStream?"->":"<-",off,off+aChunkSize,aChunkSize,aBytes,errno,strerror(errno));
        else if ( ! toStream )
            aBytes = aChunkSize;
        
        if ( aBytes > UINT16_MAX )
            abort();
        
        size_t outWritten = 0;
        if ( toStream )
            YMStreamWriteDown(stream, buffer, (uint16_t)aBytes);
        else
        {
            aResult = YMWriteFull(file, buffer, aBytes, &outWritten);
            if ( aBytes != outWritten )
                abort();
        }
        
        if ( aResult == YMIOError )
        {
            ymerr("stream[%s]: error: %d%s forward write %llu-%llu: %d (%s)",YMSTR(stream->name),file,toStream?"->":"<-",off,off+aChunkSize,errno,strerror(errno));
            lastIter = true;
        }
        else if ( ! toStream && aBytes != outWritten )
            ymerr("stream[%s]: warning: %d%s forward write %llu-%llu %zu != %zu: %d (%s)",YMSTR(stream->name),file,toStream?"->":"<-",off,off+aChunkSize,outWritten,aBytes,errno,strerror(errno));
        
        if ( inBytes )
        {
            if ( aBytes > remainingIfInBytes ) // todo guard or not, it's all internal
                abort();
            
            remainingIfInBytes -= aBytes;
            if ( remainingIfInBytes == 0 )
                lastIter = true;
        }
        
        off += aChunkSize;
    } while ( ! lastIter );
    
    free(buffer);
    
    if ( outBytes )
        *outBytes = off;
    
    if ( inBytes && off != *inBytes )
        ymerr("stream[%s]: warning: forwarded %llu bytes of requested %llu",YMSTR(stream->name),*inBytes,off);
    
    return aResult;
}

void _YMStreamClose(YMStreamRef stream_)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    int downstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    
//    // if we are closing an outgoing stream, the plexer can race us between our write of the 'stream close' command here,
//    // and freeing this stream object. synchronization is not guaranteed by our semaphore signal below, as all stream signals
//    // will wake the plexer, and we may win the 'oldest unserviced' selection before we exit this function and the client
//    // fully relinquishes ownership.
//    YMLockLock(stream->retainLock);
//    {
        YMStreamCommand command = { YMStreamClose };
        YMIOResult result = YMWriteFull(downstreamWrite, (void *)&command, sizeof(YMStreamClose), NULL);
        if ( result != YMIOSuccess )
        {
            ymerr("  stream[%s]: fatal: writing close byte to plexer: %d (%s)",YMSTR(stream->name),errno,strerror(errno));
            abort();
        }
        
        ymlog("  stream[%s]: closing stream",YMSTR(stream->name));
        stream->dataAvailableFunc(stream->dataAvailableContext);
    
    ymerr("IS RETAIN/RELEASE WORKING YET?");
//    }
//    YMLockUnlock(stream->retainLock);
}

int _YMStreamGetDownwardRead(YMStreamRef stream_)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    return YMPipeGetOutputFile(stream->downstreamPipe);
}

int _YMStreamGetUpstreamWrite(YMStreamRef stream_)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    return YMPipeGetInputFile(stream->upstreamPipe);
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
