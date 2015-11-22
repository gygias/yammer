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

//#define LOG_STREAM_LIFECYCLE
#ifdef LOG_STREAM_LIFECYCLE
#undef LOG_STREAM_LIFECYCLE
#define LOG_STREAM_LIFECYCLE(x) \
                ymerr("  stream[%s,i%d->o%dV,^o%d<-i%d]: %p %sallocating",YMSTR(stream->name), \
                      YMPipeGetInputFile(stream->downstreamPipe), \
                      YMPipeGetOutputFile(stream->downstreamPipe), \
                      YMPipeGetOutputFile(stream->upstreamPipe), \
                      YMPipeGetInputFile(stream->upstreamPipe), \
                      stream, (x) ? "":"de" );
#else
#define LOG_STREAM_LIFECYCLE(x) ;
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
    
    stream->name = name ? YMRetain(name) : YMSTRC("*");
    
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
    
    YMRelease(stream->downstreamPipe);
    YMRelease(stream->upstreamPipe);
    YMRelease(stream->name);
}

// because user data is opaque (even to user), this should expose eof
YMIOResult YMStreamReadUp(YMStreamRef stream_, void *buffer, uint16_t length, uint16_t *outLength)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    int upstreamRead = YMPipeGetOutputFile(stream->upstreamPipe);
    
    ymlog("  stream[%s]: reading %ub user data",YMSTR(stream->name),length);
    size_t actualLength = 0;
    YMIOResult result = YMReadFull(upstreamRead, buffer, length, &actualLength);
    if ( outLength )
        *outLength = (uint16_t)actualLength;
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

YMIOResult YMStreamWriteDown(YMStreamRef stream_, const void *buffer, uint16_t length)
{
    YM_DEBUG_CHUNK_SIZE(length);
    
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    YMIOResult result;
    int downstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    
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
    
    int downstreamRead = YMPipeGetOutputFile(stream->downstreamPipe);
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
    
    int upstreamWrite = YMPipeGetInputFile(stream->upstreamPipe);
    
    YMIOResult result = YMWriteFull(upstreamWrite, buffer, length, NULL);
    if ( result == YMIOError )
        ymerr("  stream[%s]: fatal: failed writing %u bytes to upstream",YMSTR(stream->name),length);
    else
        ymlog("  stream[%s]: wrote %ub to upstream",YMSTR(stream->name),length);
    
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
    uint16_t aActualLength = 0;
    do
    {
        size_t outWritten = 0;
        uint16_t aDesiredLength = inBytes ? ( remainingIfInBytes < bufferSize ? (uint16_t)remainingIfInBytes : bufferSize ) : bufferSize;
        
        if ( toStream )
        {
            size_t aActualLongLength = 0;
            aResult = YMReadFull(file, buffer, aDesiredLength, &aActualLongLength);
            if ( aActualLongLength != aDesiredLength && aResult != YMIOEOF )
                abort();
            aActualLength = (uint16_t)aActualLongLength;
        }
        else
            aResult = YMStreamReadUp(stream, buffer, aDesiredLength, &aActualLength);
        
        if ( aDesiredLength != aActualLength && aResult != YMIOEOF )
            abort();
        
        if ( aResult == YMIOError )
        {
            ymerr("  stream[%s]: error: %d%s forward read %llu-%llu: %d (%s)",YMSTR(stream->name),file,toStream?"->":"<-",off,off+aActualLength,errno,strerror(errno));
            break;
        }
        else if ( aResult == YMIOEOF )
            lastIter = true;
        else if ( toStream && aDesiredLength != aActualLength )
            ymerr("  stream[%s]: warning: %d%s forward read %llu-%llu %u != %u: %d (%s)",YMSTR(stream->name),file,toStream?"->":"<-",off,off+aDesiredLength,aActualLength,aDesiredLength,errno,strerror(errno));
        
        if ( toStream )
            YMStreamWriteDown(stream, buffer, aActualLength);
        else
        {
            aResult = YMWriteFull(file, buffer, aActualLength, &outWritten);
            if ( aActualLength != outWritten )
                abort();
        }
        
        if ( aResult == YMIOError )
        {
            ymerr("  stream[%s]: error: %d%s forward write %llu-%llu: %d (%s)",YMSTR(stream->name),file,toStream?"->":"<-",off,off+aActualLength,errno,strerror(errno));
            lastIter = true;
        }
        else if ( ! toStream && aActualLength != outWritten )
            ymerr("  stream[%s]: warning: %d%s forward write %llu-%llu %zu != %u: %d (%s)",YMSTR(stream->name),file,toStream?"->":"<-",off,off+aActualLength,outWritten,aActualLength,errno,strerror(errno));
        
        if ( inBytes )
        {
            if ( aActualLength > remainingIfInBytes ) // todo guard or not, it's all internal
                abort();
            
            remainingIfInBytes -= aActualLength;
            if ( remainingIfInBytes == 0 )
                lastIter = true;
        }
        
        off += aActualLength;
    } while ( ! lastIter );
    
    free(buffer);
    
    if ( outBytes )
        *outBytes = off;
    
    if ( inBytes && off != *inBytes )
        ymerr("  stream[%s]: warning: forwarded %llu bytes of requested %llu",YMSTR(stream->name),*inBytes,off);
    
    return aResult;
}

void _YMStreamCloseReadUpFile(YMStreamRef stream_)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    _YMPipeCloseInputFile(stream->upstreamPipe);
}

void _YMStreamSendClose(YMStreamRef stream_)
{
    __YMStreamRef stream = (__YMStreamRef)stream_;
    
    int downstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    
    YMStreamCommand command = { YMStreamClose };
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
