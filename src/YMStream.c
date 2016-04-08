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
#include "YMCompression.h"

#include "YMUtilities.h"
#include "YMSemaphore.h"

#define ymlog_pre "stream[%s]: "
#define ymlog_args s&&s->name?YMSTR(s->name):"&"
#define ymlog_type YMLogStream
#include "YMLog.h"

//#define LOG_STREAM_LIFECYCLE
#ifdef LOG_STREAM_LIFECYCLE
#undef LOG_STREAM_LIFECYCLE
#define LOG_STREAM_LIFECYCLE(x) \
                ymerr("stream[%s,if%d->of%dV,^of%d<-if%d]: %p %sallocating",YMSTR(s->name), \
                      YMPipeGetInputFile(s->downstreamPipe), \
                      YMPipeGetOutputFile(s->downstreamPipe), \
                      YMPipeGetOutputFile(s->upstreamPipe), \
                      YMPipeGetInputFile(s->upstreamPipe), \
                      s, (x) ? "":"de" );
#else
#define LOG_STREAM_LIFECYCLE(x) ;
#endif

YM_EXTERN_C_PUSH

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
    
    YMCompressionRef downCompression;
    YMCompressionRef upCompression;
    
    _ym_stream_data_available_func dataAvailableFunc;
    void *dataAvailableContext;
    _ym_stream_free_user_info_func freeUserInfoFunc; // made necessary because clients can hold stream objs longer than we do
    
    ym_stream_user_info_t *userInfo; // weak, plexer
} __ym_stream;
typedef struct __ym_stream __ym_stream_t;

YMIOResult __YMStreamForward(YMStreamRef s, YMFILE file, bool toStream, uint64_t *inBytes, uint64_t *outBytes);

YMStreamRef _YMStreamCreate(YMStringRef name, ym_stream_user_info_t *userInfo, _ym_stream_free_user_info_func callback)
{
    __ym_stream_t *s = (__ym_stream_t *)_YMAlloc(_YMStreamTypeID,sizeof(__ym_stream_t));
    
    s->name = name ? YMRetain(name) : YMSTRC("*");
    
    YMStringRef string = YMStringCreateWithFormat("%s-up",YMSTR(name),NULL);
    s->upstreamPipe = YMPipeCreate(string);
    YMRelease(string);
    s->upstreamWriteClosed = false;
    s->upstreamReadClosed = false;
    
    string = YMStringCreateWithFormat("%s-down",YMSTR(name),NULL);
    s->downstreamPipe = YMPipeCreate(string);
    YMRelease(string);
    s->downstreamWriteClosed = false;
    s->downstreamReadClosed = false;
    
    s->dataAvailableFunc = NULL;
    s->dataAvailableContext = NULL;
    
    s->userInfo = userInfo;
    s->freeUserInfoFunc = callback;
    
    s->downCompression = YMCompressionCreate(YMCompressionNone,YMPipeGetInputFile(s->downstreamPipe),true);
    s->upCompression = YMCompressionCreate(YMCompressionNone,YMPipeGetOutputFile(s->upstreamPipe),false);
    
    LOG_STREAM_LIFECYCLE(true);
    
    return s;
}

void _YMStreamSetDataAvailableCallback(YMStreamRef s_, _ym_stream_data_available_func func, void *ctx)
{
    __ym_stream_t *s = (__ym_stream_t *)s_;
    s->dataAvailableFunc = func;
    s->dataAvailableContext = ctx;
}

void _YMStreamFree(YMTypeRef o_)
{
    YMStreamRef s = (__ym_stream_t *)o_;
    
    LOG_STREAM_LIFECYCLE(false);
    
    if ( s->freeUserInfoFunc )
        s->freeUserInfoFunc(s);
    
    YMRelease(s->downstreamPipe);
    YMRelease(s->upstreamPipe);
    YMRelease(s->name);
    YMRelease(s->downCompression);
    YMRelease(s->upCompression);
}

bool _YMStreamSetCompression(YMStreamRef s_, YMCompressionType type)
{
    __ym_stream_t *s = (__ym_stream_t *)s_;
    
    YMCompressionRef downCompression = YMCompressionCreate(type,YMPipeGetInputFile(s->downstreamPipe),true);
    bool okay = YMCompressionInit(downCompression);
    
    if ( okay ) {
        YMCompressionRef upCompression = YMCompressionCreate(type,YMPipeGetOutputFile(s->upstreamPipe),false);
        okay = YMCompressionInit(upCompression);
        if ( okay ) {
            YMRelease(s->downCompression); // nocompression on create
            s->downCompression = downCompression;
            YMRelease(s->upCompression);
            s->upCompression = upCompression;
        }
    }
    return okay;
}

// because user data is opaque (even to user), this should expose eof
YMIOResult YMStreamReadUp(YMStreamRef s, uint8_t *buffer, uint16_t length, uint16_t *outLength)
{
    ymlog("reading %ub user data",length);
    size_t off = 0, iters = 0;
    YMIOResult result;
    do {
        size_t actual = 0;
        result = YMCompressionRead(s->upCompression, buffer + off, length - off, &actual);
        off += actual;
        iters++;
    } while ( (off < length) && result == YMIOSuccess );
    ymassert(off<=UINT16_MAX,"fatal: compression read %zu",off);
    
    if ( outLength )
        *outLength = (uint16_t)off;
    if ( result == YMIOError )
        ymerr("fatal: reading %ub user data: %d (%s)",length,errno,strerror(errno));
    else if ( result == YMIOEOF )
        ymerr("EOF from upstream");
    else
        ymlog("read %ub from upstream",length);
    
    return result;
}

YMIOResult YMStreamWriteDown(YMStreamRef s, const uint8_t *buffer, uint16_t length)
{
    YM_DEBUG_CHUNK_SIZE(length);
    
    size_t off = 0, iters = 0;
    YMIOResult result;
    do {
        size_t actual = 0;
        result = YMCompressionWrite(s->downCompression, buffer, length, &actual);
        off += actual;
        iters++;
    } while ( (off < length) && result == YMIOSuccess );
    
    if ( result != YMIOSuccess ) {
        ymerr("failed writing stream chunk with size %ub",length);
        return result;
    }
    ymlog("wrote buffer for chunk with size %ub",length);
    
    // signal the plexer to wake and service this stream
    s->dataAvailableFunc(s,length,s->dataAvailableContext);
    
    ymlog("wrote %ub chunk",length);
    
    return result;
}

YMIOResult _YMStreamReadDown(YMStreamRef s, void *buffer, uint32_t length)
{
	YMFILE downstreamRead = YMPipeGetOutputFile(s->downstreamPipe);
    ymlog("reading %ub from downstream",length);
    YMIOResult result = YMReadFull(downstreamRead, buffer, length, NULL);
    if ( result != YMIOSuccess )
        ymerr("failed reading %ub from downstream",length);
    else
        ymlog("read %ub from downstream",length);
    return result;
}

YMIOResult _YMStreamWriteUp(YMStreamRef s, const void *buffer, uint32_t length)
{
    YMFILE upstreamWrite = YMPipeGetInputFile(s->upstreamPipe);
    YMIOResult result = YMWriteFull(upstreamWrite, buffer, length, NULL);
    if ( result == YMIOError )
        ymerr("fatal: failed writing %u bytes to upstream",length);
    else
        ymlog("wrote %ub to upstream",length);
    
    return result;
}

YMIOResult YMStreamWriteToFile(YMStreamRef s, YMFILE file, uint64_t *inBytes, uint64_t *outBytes)
{
    return __YMStreamForward(s, file, false, inBytes, outBytes);
}

YMIOResult YMStreamReadFromFile(YMStreamRef s, YMFILE file, uint64_t *inBytes, uint64_t *outBytes)
{
    return __YMStreamForward(s, file, true, inBytes, outBytes);
}

// for the love of god break this into at least 2 functions
YMIOResult __YMStreamForward(YMStreamRef s, YMFILE file, bool fromFileToStream, uint64_t *inBytes, uint64_t *outBytes)
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
    if ( fromFileToStream ) {
        _YMStreamCommand prefixCommand = { boundedByCallerOrRemote ? YMStreamForwardFileBounded : YMStreamForwardFileUnbounded };
        aResult = YMStreamWriteDown(s, (uint8_t *)&prefixCommand, sizeof(prefixCommand));
        ymsoftassert(aResult==YMIOSuccess, "write forward init");
        
        if ( boundedByCallerOrRemote ) {
            _YMStreamBoundType length = *inBytes;
            aResult = YMStreamWriteDown(s, (uint8_t *)&length, sizeof(length));
            ymsoftassert(aResult==YMIOSuccess, "write forward bound");
            ymlog("beginning bounded file write (%llub)",length);
        } else
            ymlog("beginning unbounded file write");
    // if we're writing to file, read the coordination stuff above
    } else {
        _YMStreamCommand peekCommand;
        uint16_t peekLength = sizeof(_YMStreamCommand);
        aResult = YMStreamReadUp(s, (uint8_t *)&peekCommand, peekLength, &debugOutLength);
        ymsoftassert(aResult==YMIOSuccess&&debugOutLength==peekLength,"peek forward command");
        
        if ( peekCommand.command == YMStreamForwardFileBounded ) {
            _YMStreamBoundType length;
            debugOutLength = 0;
            aResult = YMStreamReadUp(s, (uint8_t *)&length, sizeof(length), &debugOutLength);
            ymsoftassert(aResult==YMIOSuccess&&debugOutLength==sizeof(length),"peek forward length");
            
            remainingIfBounded = length;
            boundedByCallerOrRemote = true;
            ymlog("beginning remote-bounded file read (%llub)",remainingIfBounded);
        } else if ( peekCommand.command == YMStreamForwardFileUnbounded ) {
            ymlog("beginning unbounded file read");
            boundedByCallerOrRemote = false; // expect a 'forward end' command
        } else ymsoftassert(false,"peek forward command bogus");
    }
    
    uint16_t aActualLength = 0;
    do {
        size_t outWritten = 0;
        uint16_t aDesiredLength = boundedByCallerOrRemote ? ( remainingIfBounded < bufferSize ? (uint16_t)remainingIfBounded : bufferSize ) : bufferSize;
        
        if ( fromFileToStream ) {
			size_t actual = 0;
            aResult = YMReadFull(file, buffer, aDesiredLength, &actual);
			aActualLength = (uint16_t)actual;
            ymsoftassert(aResult==YMIOSuccess&&aActualLength==aDesiredLength||aResult==YMIOEOF,"read user forward file");
        } else { // stream to file
            if ( ! boundedByCallerOrRemote ) {
                _YMStreamCommand aCommand;
                debugOutLength = 0;
                aResult = YMStreamReadUp(s, (uint8_t *)&aCommand, sizeof(aCommand), &debugOutLength);
                ymsoftassert(aResult==YMIOSuccess&&debugOutLength==sizeof(aCommand),"read a forward command");
                
                if ( aCommand.command == YMStreamForwardFileEnd ) {
                    aDesiredLength = 0; // omg refactor this function
                    aActualLength = 0;
                    aResult = YMIOEOF;
                    goto unbounded_no_raw_read;
                } else if ( aCommand.command > 0 ) {
                    if ( aCommand.command > UINT16_MAX ) ymabort("stream command: %d",aCommand.command);
                    aDesiredLength = (uint16_t)aCommand.command;
                } else
                    ymsoftassert(false,"read a forward command");
            }
            
            aResult = YMStreamReadUp(s, buffer, aDesiredLength, &aActualLength);
        }
        
    unbounded_no_raw_read:
        ymsoftassert(aDesiredLength==aActualLength||aResult==YMIOEOF,"forward early eof");
        
        if ( aResult == YMIOError ) {
            ymerr("f%d%s forward read %llu-%llu: %d (%s)",file,fromFileToStream?"->":"<-",off,off+aActualLength,errno,strerror(errno));
            break;
        } else if ( aResult == YMIOEOF )
            lastIter = true;
        else if ( fromFileToStream && aDesiredLength != aActualLength )
            ymerr("f%d%s forward read %llu-%llu %u != %u: %d (%s)",file,fromFileToStream?"->":"<-",off,off+aDesiredLength,aActualLength,aDesiredLength,errno,strerror(errno));
        else
            ymlog("f%d%s read %ub (%llu) remaining...",file,fromFileToStream?"F>S":"F<S",aActualLength,remainingIfBounded);
        
        if ( fromFileToStream ) {
            // if we're unbounded, write a chunk header if we got raw data, raw data if it exists, and 'unbounded end' if we hit EOF
            if ( ! boundedByCallerOrRemote && aActualLength > 0 ) {
                _YMStreamCommand aChunkCommand = { aActualLength };
                aResult = YMStreamWriteDown(s, (uint8_t *)&aChunkCommand, sizeof(aChunkCommand));
                ymsoftassert(aResult==YMIOSuccess, "write forward chunk length");
            }
            
            if ( aActualLength > 0 ) {
                aResult = YMStreamWriteDown(s, buffer, aActualLength);
                ymsoftassert(aResult==YMIOSuccess, "write forward chunk");
            }
        }
        else if ( aActualLength > 0 ) {
            aResult = YMWriteFull(file, buffer, aActualLength, &outWritten);
            ymsoftassert(aActualLength==outWritten,"write forward file");
        }
        
        if ( aResult == YMIOError ) {
            ymerr("f%d%s forward write %llu-%llu: %d (%s)",file,fromFileToStream?"->":"<-",off,off+aActualLength,errno,strerror(errno));
            lastIter = true;
        } else if ( ! fromFileToStream && aActualLength != outWritten ) {
            ymerr("f%d%s forward write %llu-%llu %zu != %u: %d (%s)",file,fromFileToStream?"->":"<-",off,off+aActualLength,outWritten,aActualLength,errno,strerror(errno));
            ymsoftassert(false, "forward read/write mismatch");
        }
        
        if ( boundedByCallerOrRemote ) {
            ymsoftassert(aActualLength<=remainingIfBounded,"forward exceeded bounds");
            
            remainingIfBounded -= aActualLength;
            if ( remainingIfBounded == 0 )
                lastIter = true;
        }
        
        ymlog("%s wrote [%llu,%ub] (%llu remaining)...",fromFileToStream?"F>S":"F<S",off,aActualLength,remainingIfBounded);
        
        off += aActualLength;
    } while ( ! lastIter );
    
    free(buffer);
    
    if ( outBytes )
        *outBytes = off;
    
    if ( ! boundedByCallerOrRemote && fromFileToStream ) {
        _YMStreamCommand endCommand = { YMStreamForwardFileEnd };
        aResult = YMStreamWriteDown(s, (uint8_t *)&endCommand, sizeof(endCommand));
        ymsoftassert(aResult==YMIOSuccess, "write forward file end");
        ymlog("wrote ForwardFileEnd");
    }
    
    if ( inBytes && (off != *inBytes) ) {
        ymerr("forwarded %llu bytes of requested %llu",off,*inBytes);
        ymsoftassert(false,"forward bounds mismatch");
    } else {
        if ( fromFileToStream ) { ymlog("forwarded %llu bytes from %llu to stream",off,(unsigned long long)file); }
        else { ymlog("forwarded %llu bytes to stream from %llu",off,(unsigned long long)file); }
    }
    
    return aResult;
}

void _YMStreamCloseWriteUp(YMStreamRef s)
{
    _YMPipeCloseInputFile(s->upstreamPipe);
}

ym_stream_user_info_t *_YMStreamGetUserInfo(YMStreamRef s)
{
    return (ym_stream_user_info_t *)s->userInfo;
}

YMStringRef _YMStreamGetName(YMStreamRef s)
{
    return s->name;
}

YM_EXTERN_C_POP
