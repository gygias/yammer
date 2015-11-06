//
//  YMStream.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMStream.h"
#include "YMPrivate.h"
#include "YMStreamPriv.h"

#include "YMPipe.h"

#ifdef USE_FTIME
#include <sys/timeb.h>
#error todo
#else
#include <sys/time.h>
#endif

typedef struct __YMStream
{
    YMTypeID _type;
    
    bool isLocal;
    
    YMPipeRef upstream;
    bool upstreamWidowed;
    bool upstreamClosed;
    YMPipeRef downstream;
    bool downstreamWidowed;
    bool downstreamClosed;
    char *name;
    
    bool direct;
    
    YMStreamUserInfoRef __userInfo; // weak, plexer
    YMSemaphoreRef __dataAvailableSemaphore; // weak, plexer
    struct timeval *__lastServiceTime;
} _YMStream;

YMStreamRef YMStreamCreate(const char *name, bool isLocal)
{
    YMStreamRef stream = (YMStreamRef)malloc(sizeof(struct __YMStream));
    stream->name = strdup( name ? name : "unnamed-stream" );
    
    stream->isLocal = isLocal;
    
    size_t nameLen = strlen(name);
    
    int upSuffixLen = 3; // "-up"
    unsigned long upStreamNameLen = nameLen + upSuffixLen + 1;
    char *upStreamName = (char *)calloc(upStreamNameLen,sizeof(char));
    strncat(upStreamName, name, nameLen + 1);
    strncat(upStreamName, "-up", upSuffixLen);
    stream->upstream = YMPipeCreate(upStreamName);
    free(upStreamName);
    
    int downSuffixLen = 5; // "-down"
    unsigned long downStreamNameLen = strlen(name) + downSuffixLen + 1;
    char *downStreamName = (char *)calloc(downStreamNameLen,sizeof(char));
    strncat(downStreamName, name, nameLen);
    strncat(downStreamName, "-down", downSuffixLen);
    stream->downstream = YMPipeCreate(downStreamName);
    free(downStreamName);
    
    stream->__userInfo = NULL;
    stream->__dataAvailableSemaphore = NULL;
    stream->__lastServiceTime = (struct timeval *)malloc(sizeof(struct timeval));
    if ( 0 != gettimeofday(stream->__lastServiceTime, NULL) )
    {
        YMLog("warning: error setting initial service time for stream: %d (%s)",errno,strerror(errno));
        YMSetTheBeginningOfPosixTimeForCurrentPlatform(stream->__lastServiceTime);
    }
    
    return (YMStreamRef)stream;
}

void _YMStreamFree(YMTypeRef object)
{
    _YMStream *stream = (_YMStream *)object;
    
    free(stream->name);
    free(stream->__lastServiceTime);
    _YMStreamFree(stream->upstream);
    _YMStreamFree(stream->downstream);
    
    free(stream);
}

bool YMStreamWriteDown(YMStreamRef stream, const void *buffer, uint32_t length)
{
    int downstreamWrite = YMPipeGetInputFile(stream->downstream);
    int debugDownstreamRead = YMPipeGetOutputFile(stream->downstream);
    
    YMStreamChunkHeader header = { length };
    
    YMLog("stream[%s,%d<->%d,%u]: writing header for stream chunk size %u",stream->isLocal?"V":"^",downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,length);
    bool okay = YMWriteFull(downstreamWrite, (void *)&header, sizeof(header));
    if ( ! okay )
    {
        YMLog("stream[%s,%d<->%d,%u]: error: failed writing header for stream chunk size %u",stream->isLocal?"V":"^",downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,length);
        return false;
    }
    YMLog("stream[%s,%d<->%d,%u]: wrote header for stream chunk size %u",stream->isLocal?"V":"^",downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,length);
    
    okay = YMWriteFull(downstreamWrite, buffer, length);
    if ( ! okay )
    {
        YMLog("stream[%s,%d<->%d,s%u]: error: failed writing stream chunk with size %u",stream->isLocal?"V":"^",downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,length);
        return false;
    }
    YMLog("stream[%s,%d<->%d,s%u]: wrote buffer for stream chunk size %u",stream->isLocal?"V":"^",downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,length);
    
    // signal the plexer to wake and service this stream
    YMSemaphoreSignal(stream->__dataAvailableSemaphore);
    
    YMLog("stream[%s,%d<->%d,%u]: wrote %lu + %u down stream chunk",stream->isLocal?"V":"^",downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,sizeof(header),length);
    
    return okay;
}

bool _YMStreamReadDown(YMStreamRef stream, void *buffer, uint32_t length)
{
    int downstreamRead = YMPipeGetOutputFile(stream->downstream);
    int debugDownstreamWrite = YMPipeGetInputFile(stream->downstream);
    YMLog("stream[%s,%d<->%d,%u]: reading %u bytes from downstream",stream->isLocal?"V":"^",debugDownstreamWrite,downstreamRead,stream->__userInfo->streamID,length);
    bool okay = YMReadFull(downstreamRead, buffer, length);
    if ( ! okay )
    {
        YMLog("stream[%s,%d<->%d,%u]: error: failed reading %u bytes from downstream",stream->isLocal?"V":"^",debugDownstreamWrite,downstreamRead,stream->__userInfo->streamID,length);
        return false;
    }
    YMLog("stream[%s,%d<->%d,%u]: read %u bytes from downstream",stream->isLocal?"V":"^",debugDownstreamWrite,downstreamRead,stream->__userInfo->streamID,length);
    return okay;
}

bool YMStreamWriteUp(YMStreamRef stream, const void *buffer, uint32_t length)
{
    int upstreamWrite = YMPipeGetInputFile(stream->upstream);
    int debugUpstreamRead = YMPipeGetOutputFile(stream->upstream);
    
    bool okay = YMWriteFull(upstreamWrite, buffer, length);
    if ( ! okay )
    {
        YMLog("stream[%s,%d<->%d,%u]: error: failed writing %u bytes to upstream",stream->isLocal?"V":"^",upstreamWrite,debugUpstreamRead,stream->__userInfo->streamID,length);
        return false;
    }
    return okay;
}

bool YMStreamReadUp(YMStreamRef stream, void *buffer, uint32_t length)
{
    int upstreamRead = YMPipeGetOutputFile(stream->upstream);
    int debugUpstreamWrite = YMPipeGetInputFile(stream->upstream);
    
    bool okay = YMReadFull(upstreamRead, buffer, length);
    if ( ! okay )
    {
        YMLog("stream[%s,%d<->%d,%u]: error: failed reading %u bytes from upstream",stream->isLocal?"V":"^",debugUpstreamWrite,upstreamRead,stream->__userInfo->streamID,length);
        return false;
    }
    return okay;
}

bool _YMStreamClose(YMStreamRef stream)
{
    _YMStream *_stream = (_YMStream *)stream;
    
    int downstreamWrite = YMPipeGetInputFile(_stream->downstream);
    
    int result = close(downstreamWrite);
    return ( result == 0 );
}

int _YMStreamGetDownwardWrite(YMStreamRef stream)
{
    return YMPipeGetInputFile(stream->downstream);
}

int _YMStreamGetDownwardRead(YMStreamRef stream)
{
    return YMPipeGetOutputFile(stream->downstream);
}

int _YMStreamGetUpstreamWrite(YMStreamRef stream)
{
    return YMPipeGetInputFile(stream->upstream);
}

int _YMStreamGetUpstreamRead(YMStreamRef stream)
{
    return YMPipeGetOutputFile(stream->upstream);
}

void _YMStreamSetUserInfo(YMStreamRef stream, YMStreamUserInfoRef userInfo)
{
    stream->__userInfo = userInfo;
}

YMStreamUserInfoRef _YMStreamGetUserInfo(YMStreamRef stream)
{
    return stream->__userInfo;
}

void _YMStreamSetDataAvailableSemaphore(YMStreamRef stream, YMSemaphoreRef semaphore)
{
    stream->__dataAvailableSemaphore = semaphore;
}

void _YMStreamSetLastServiceTimeNow(YMStreamRef stream)
{
    if ( 0 != gettimeofday(stream->__lastServiceTime, NULL) )
    {
        YMLog("warning: error setting initial service time for stream: %d (%s)",errno,strerror(errno));
        YMSetTheBeginningOfPosixTimeForCurrentPlatform(stream->__lastServiceTime);
    }
}

struct timeval *_YMStreamGetLastServiceTime(YMStreamRef stream)
{
    return stream->__lastServiceTime;
}
