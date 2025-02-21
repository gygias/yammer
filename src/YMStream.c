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
#define LOG_STREAM_EVENT(x) \
                ymerr("stream[%s,if%d->of%dV,^of%d<-if%d]: %p %sallocating",YMSTR(s->name), s->dI, s->dO, s->uO, s->uI, s, (x) ? "":"de" );
#else
#define LOG_STREAM_EVENT(x) ;
#endif

YM_EXTERN_C_PUSH

typedef struct __ym_stream
{
    _YMType _common;
    
    YMPipeRef upstreamPipe;
    bool upstreamWriteClosed;
    bool upstreamReadClosed;
    YMPipeRef downstreamPipe;
    bool downstreamWriteClosed;
    bool downstreamReadClosed;
    YMStringRef name;
    
    YMCompressionRef downCompression;
    YMCompressionRef upCompression;
    
    ym_stream_user_info_t *userInfo; // weak, plexer

    // we aggressively clear state to find bugs (or, at least maintain the happy status quo), so, this is tedious but worth it imo
#ifdef LOG_STREAM_LIFECYCLE
    YMFILE dI, dO, uI, uO;
#endif
} __ym_stream;
typedef struct __ym_stream __ym_stream_t;

YMIOResult __YMStreamForward(YMStreamRef s, YMFILE file, bool toStream, uint64_t *inBytes, uint64_t *outBytes);

YMStreamRef _YMStreamCreate(YMStringRef name, ym_stream_user_info_t *userInfo, YMFILE *downOut)
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
    
    s->userInfo = userInfo;

    #warning compression is set by the (potentially remote) stream originator, and initialized by rpc at the stream level,\
             this initializer is shared in ways that can make these redundant and short-lived, fix.
    s->downCompression = YMCompressionCreate(YMCompressionNone,YMPipeGetInputFile(s->downstreamPipe),true);
    s->upCompression = YMCompressionCreate(YMCompressionNone,YMPipeGetOutputFile(s->upstreamPipe),false);
    
    if ( downOut ) *downOut = YMPipeGetOutputFile(s->downstreamPipe);
#ifdef LOG_STREAM_LIFECYCLE
    s->dI = YMPipeGetInputFile(s->downstreamPipe);
    s->dO = YMPipeGetOutputFile(s->downstreamPipe);
    s->uO = YMPipeGetOutputFile(s->upstreamPipe);
    s->uI = YMPipeGetInputFile(s->upstreamPipe);
    LOG_STREAM_EVENT(true)
#endif
    
    return s;
}

void _YMStreamFree(YMTypeRef o_)
{
    YMStreamRef s = (__ym_stream_t *)o_;
    
#ifdef LOG_STREAM_LIFECYCLE
    LOG_STREAM_EVENT(false)
#endif
    
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
            #warning leaky codepaths
            YMRelease(s->downCompression); // nocompression on create
            s->downCompression = downCompression;
            YMRelease(s->upCompression);
            s->upCompression = upCompression;
        }
    }
    return okay;
}

YMIOResult YMStreamReadUp(YMStreamRef s, uint8_t *buffer, uint16_t length, uint16_t *outLength)
{
    ymlog("reading %ub stream user data",length);

    size_t off = 0;
    YMIOResult result;
    do {
        size_t o = 0;
        result = YMCompressionRead(s->upCompression,buffer + off,length - off,&o);
        off += o;
        if ( result == YMIOError )
            ymabort("fatal: reading %ub up to user: %d (%s)",length,errno,strerror(errno));
        if ( result == YMIOEOF ) {
            ymerr("EOF: reading %ub up to user:",length);
            break;
        } else if ( length != o ) {
            ymabort("fatal: stream read %d %hu != %zu",result,length,o);
        } else
            ymlog("read %ub stream user data",length);
    } while ( off < length );
    
    if ( outLength ) *outLength = off;
    return result;
}

YMIOResult YMStreamWriteDown(YMStreamRef s, const uint8_t *buffer, uint16_t length)
{
    YM_DEBUG_CHUNK_SIZE(length);
    
    ymlog("writing %hub to downstream",length);
    size_t rawOff = 0, off = 0;
    YMIOResult result;
    do {
        size_t actual = 0, overhead = 0;
        result = YMCompressionWrite(s->downCompression, buffer + rawOff, length - rawOff, &actual, &overhead);
        ymassert((result==YMIOSuccess),"stream compression write failed %d %zu %zu",result,length-rawOff,actual);
        rawOff += length;
        off += actual + overhead;
    } while ( (rawOff < length) && result == YMIOSuccess );
    
    if ( result != YMIOSuccess ) {
        ymerr("failed writing stream chunk with size %ub",length);
        return result;
    }
    
    ymlog("wrote %zub chunk",off);
    
    return result;
}

YM_IO_RESULT _YMStreamPlexReadDown(YMStreamRef s, void *buffer, uint32_t length)
{
    YM_IO_BOILERPLATE

	YMFILE downstreamRead = YMPipeGetOutputFile(s->downstreamPipe);
    ymlog("reading[%d] %ub from downstream",downstreamRead,length);
    YM_READ_FILE(downstreamRead,buffer,length);
    if ( result == 0 ) {
        ymlog("%s read EOF %p %p %u",__FUNCTION__,s,buffer,length);
    } else if ( result == -1 ) {
        ymerr("%s failed reading %ub from downstream",__FUNCTION__,length);
    } else
        ymlog("%s read %ub from downstream",__FUNCTION__,length);

    return result;
}

YMIOResult _YMStreamPlexWriteUp(YMStreamRef s, const void *buffer, uint32_t length)
{
    YM_DEBUG_CHUNK_SIZE(length);

    YMFILE upstreamWrite = YMPipeGetInputFile(s->upstreamPipe);
    size_t o = 0;
    YMIOResult result = YMWriteFull(upstreamWrite, buffer, length, &o);
    if ( result == YMIOError )
        ymerr("fatal: failed writing %u bytes to upstream",length);
    else
        ymlog("%d[%zu] = YMWriteFull(%d, %p, %u, &o) to upstream",result,o,upstreamWrite,buffer,length);

    return result;
}

YMIOResult _YMStreamPlexWriteDown(YMStreamRef s, const uint8_t *buffer, uint16_t length)
{
    YMFILE upstreamWrite = YMPipeGetInputFile(s->downstreamPipe);
    size_t o = 0;
    YMIOResult result = YMWriteFull(upstreamWrite, buffer, length, &o);
    if ( result == YMIOError )
        ymerr("fatal: failed writing %u bytes to upstream",length);
    else
        ymlog("%d[%zu] = YMWriteFull(%d, %p, %u, &o) to upstream",result,o,upstreamWrite,buffer,length);
    
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

void YMStreamGetPerformance(YMStreamRef s_, uint64_t *oDownIn, uint64_t *oDownOut, uint64_t *oUpIn, uint64_t *oUpOut)
{
    __ym_stream_t *s = (__ym_stream_t *)s_;
    YMCompressionGetPerformance(s->downCompression,oDownIn,oDownOut);
    YMCompressionGetPerformance(s->upCompression,oUpIn,oUpOut);
}

// for the love of god break this into at least 2 functions
YMIOResult __YMStreamForward(YMStreamRef stream, YMFILE file, bool fromFileToStream, uint64_t *inBytes, uint64_t *outBytes)
{
    __ym_stream_t *s = (__ym_stream_t *)stream;

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
        ymsoftassert(aResult==YMIOSuccess, "%s write forward init",YMSTR(s->name));
        
        if ( boundedByCallerOrRemote ) {
            _YMStreamBoundType length = *inBytes;
            aResult = YMStreamWriteDown(s, (uint8_t *)&length, sizeof(length));
            ymsoftassert(aResult==YMIOSuccess, "%s write forward bound",YMSTR(s->name));
            ymlog("%s beginning bounded file write (%lub)",YMSTR(s->name),length);
        } else
            ymlog("%s beginning unbounded file write",YMSTR(s->name));
    // if we're writing to file, read the coordination stuff above
    } else {
        _YMStreamCommand peekCommand;
        uint16_t peekLength = sizeof(_YMStreamCommand);
        aResult = YMStreamReadUp(s, (uint8_t *)&peekCommand, peekLength, &debugOutLength);
        ymsoftassert(aResult==YMIOSuccess&&debugOutLength==peekLength,"%s peek forward command",YMSTR(s->name));
        
        if ( peekCommand.command == YMStreamForwardFileBounded ) {
            _YMStreamBoundType length;
            debugOutLength = 0;
            aResult = YMStreamReadUp(s, (uint8_t *)&length, sizeof(length), &debugOutLength);
            ymsoftassert(aResult==YMIOSuccess&&debugOutLength==sizeof(length),"%s peek forward length",YMSTR(s->name));
            
            remainingIfBounded = length;
            boundedByCallerOrRemote = true;
            ymlog("%s beginning remote-bounded file read (%lub)",YMSTR(s->name),remainingIfBounded);
        } else if ( peekCommand.command == YMStreamForwardFileUnbounded ) {
            ymlog("beginning unbounded file read");
            boundedByCallerOrRemote = false; // expect a 'forward end' command
        } else ymsoftassert(false,"%s peek forward command bogus %d (%hu)",YMSTR(s->name),peekCommand.command,debugOutLength);
    }
    
    uint16_t aActualLength = 0;
    do {
        size_t outWritten = 0;
        uint16_t aDesiredLength = boundedByCallerOrRemote ? ( remainingIfBounded < bufferSize ? (uint16_t)remainingIfBounded : bufferSize ) : bufferSize;
        
        if ( fromFileToStream ) {
			size_t actual = 0;
            aResult = YMReadFull(file, buffer, aDesiredLength, &actual);
			aActualLength = (uint16_t)actual;
            ymsoftassert(aResult==YMIOSuccess&&aActualLength==aDesiredLength||aResult==YMIOEOF,"%s read user forward file %d %hu",YMSTR(s->name),aResult,aActualLength);
        } else { // stream to file
            if ( ! boundedByCallerOrRemote ) {
                _YMStreamCommand aCommand;
                debugOutLength = 0;
                aResult = YMStreamReadUp(s, (uint8_t *)&aCommand, sizeof(aCommand), &debugOutLength);
                ymsoftassert(aResult==YMIOSuccess&&debugOutLength==sizeof(aCommand),"%s read a forward command",YMSTR(s->name));
                
                if ( aCommand.command == YMStreamForwardFileEnd ) {
                    aDesiredLength = 0; // omg refactor this function
                    aActualLength = 0;
                    aResult = YMIOEOF;
                    goto unbounded_no_raw_read;
                } else if ( aCommand.command > 0 ) {
                    if ( aCommand.command > UINT16_MAX ) ymabort("%s stream command: %d",YMSTR(s->name),aCommand.command);
                    aDesiredLength = (uint16_t)aCommand.command;
                } else
                    ymsoftassert(false,"%s read a forward command",YMSTR(s->name));
            }
            
            aResult = YMStreamReadUp(s, buffer, aDesiredLength, &aActualLength);
        }
        
    unbounded_no_raw_read:
        ymsoftassert(aDesiredLength==aActualLength||aResult==YMIOEOF,"%s forward early eof",YMSTR(s->name));
        
        if ( aResult == YMIOError ) {
            ymerr("%s f%d%s forward read %lu-%lu: %d (%s)",YMSTR(s->name),file,fromFileToStream?"->":"<-",off,off+aActualLength,errno,strerror(errno));
            break;
        } else if ( aResult == YMIOEOF )
            lastIter = true;
        else if ( fromFileToStream && aDesiredLength != aActualLength )
            ymerr("%s f%d%s forward read %lu-%lu %u != %u: %d (%s)",YMSTR(s->name),file,fromFileToStream?"->":"<-",off,off+aDesiredLength,aActualLength,aDesiredLength,errno,strerror(errno));
        else
            ymlog("%s f%d%s read %ub (%lu) remaining...",YMSTR(s->name),file,fromFileToStream?"F>S":"F<S",aActualLength,remainingIfBounded);
        
        if ( fromFileToStream ) {
            // if we're unbounded, write a chunk header if we got raw data, raw data if it exists, and 'unbounded end' if we hit EOF
            if ( ! boundedByCallerOrRemote && aActualLength > 0 ) {
                _YMStreamCommand aChunkCommand = { aActualLength };
                aResult = YMStreamWriteDown(s, (uint8_t *)&aChunkCommand, sizeof(aChunkCommand));
                ymsoftassert(aResult==YMIOSuccess, "%s write forward chunk length",YMSTR(s->name));
            }
            
            if ( aActualLength > 0 ) {
                aResult = YMStreamWriteDown(s, buffer, aActualLength);
                ymsoftassert(aResult==YMIOSuccess, "%s write forward chunk",YMSTR(s->name));
            }
        }
        else if ( aActualLength > 0 ) {
            aResult = YMWriteFull(file, buffer, aActualLength, &outWritten);
            ymsoftassert(aActualLength==outWritten,"%s write forward file",YMSTR(s->name));
        }
        
        if ( aResult == YMIOError ) {
            ymerr("%s f%d%s forward write %lu-%lu: %d (%s)",YMSTR(s->name),file,fromFileToStream?"->":"<-",off,off+aActualLength,errno,strerror(errno));
            lastIter = true;
        } else if ( ! fromFileToStream && aActualLength != outWritten ) {
            ymerr("%s f%d%s forward write %lu-%lu %zu != %u: %d (%s)",YMSTR(s->name),file,fromFileToStream?"->":"<-",off,off+aActualLength,outWritten,aActualLength,errno,strerror(errno));
            ymsoftassert(false, "forward read/write mismatch");
        }
        
        if ( boundedByCallerOrRemote ) {
            ymsoftassert(aActualLength<=remainingIfBounded,"forward exceeded bounds");
            
            remainingIfBounded -= aActualLength;
            if ( remainingIfBounded == 0 )
                lastIter = true;
        }
        
        ymlog("%s %s wrote [%lu,%ub] (%lu remaining)...",YMSTR(s->name),fromFileToStream?"F>S":"F<S",off,aActualLength,remainingIfBounded);
        
        off += aActualLength;
    } while ( ! lastIter );
    
    YMFREE(buffer);
    
    if ( outBytes )
        *outBytes = off;
    
    if ( ! boundedByCallerOrRemote && fromFileToStream ) {
        _YMStreamCommand endCommand = { YMStreamForwardFileEnd };
        aResult = YMStreamWriteDown(s, (uint8_t *)&endCommand, sizeof(endCommand));
        ymsoftassert(aResult==YMIOSuccess, "%s write forward file end",YMSTR(s->name));
        ymlog("%s wrote ForwardFileEnd",YMSTR(s->name));
    }
    
    if ( inBytes && (off != *inBytes) ) {
        ymerr("%s forwarded %lu bytes of requested %lu",YMSTR(s->name),off,*inBytes);
        ymsoftassert(false,"%s forward bounds mismatch",YMSTR(s->name));
    } else {
        if ( fromFileToStream ) { ymlog("%s forwarded %lu bytes from %d to stream",YMSTR(s->name),off,file); }
        else { ymlog("%s forwarded %lu bytes to stream from %d",YMSTR(s->name),off,file); }
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
