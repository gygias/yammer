//
//  YMStream.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMStream.h"
#include "YMPrivate.h"

#include "YMPipe.h"

typedef struct __YMStream
{
    YMTypeID _type;
    
    YMPipeRef upstream;
    bool upstreamWidowed;
    bool upstreamClosed;
    YMPipeRef downstream;
    bool downstreamWidowed;
    bool downstreamClosed;
    char *name;
    
    const void *__userInfo;
    YMSemaphoreRef __dataAvailableSemaphore;
} _YMStream;

YMStreamRef YMStreamCreate(char *name)
{
    YMStreamRef stream = (YMStreamRef)malloc(sizeof(struct __YMStream));
    stream->name = strdup( name ? name : "ymstream" );
    
    size_t nameLen = strlen(name);
    
    int upSuffixLen = 3; // "-up"
    unsigned long upStreamNameLen = nameLen + upSuffixLen + 1;
    char *upStreamName = (char *)malloc(upStreamNameLen);
    strncat(upStreamName, name, nameLen);
    strncat(upStreamName, "-up", upSuffixLen);
    stream->upstream = YMPipeCreate(upStreamName);
    free(upStreamName);
    
    int downSuffixLen = 5; // "-down"
    unsigned long downStreamNameLen = strlen(name) + downSuffixLen + 1;
    char *downStreamName = (char *)malloc(downStreamNameLen);
    strncat(downStreamName, name, nameLen);
    strncat(downStreamName, "-down", downSuffixLen);
    stream->downstream = YMPipeCreate(downStreamName);
    free(downStreamName);
    
    stream->__userInfo = NULL;
    stream->__dataAvailableSemaphore = NULL;
    
    return (YMStreamRef)stream;
}

void _YMStreamFree(YMTypeRef object)
{
    _YMStream *stream = (_YMStream *)object;
    if ( stream->name )
        free(stream->name);
    if ( stream->upstream )
        _YMStreamFree(stream->upstream);
    if ( stream->downstream )
        _YMStreamFree(stream->downstream);
    free(stream);
}

bool YMStreamRead(YMStreamRef stream, void *buffer, size_t length)
{
    _YMStream *_stream = (_YMStream *)stream;
    
    int upstreamRead = YMPipeGetOutputFile(_stream->upstream);
    return YMReadFull(upstreamRead, buffer, length);
}

bool YMStreamWrite(YMStreamRef stream, void *buffer, size_t length)
{
    _YMStream *_stream = (_YMStream *)stream;
    
    int downstreamWrite = YMPipeGetInputFile(_stream->downstream);
    bool okay = YMWriteFull(downstreamWrite, buffer, length);
    
    if ( stream->__dataAvailableSemaphore )
        YMSemaphoreSignal(stream->__dataAvailableSemaphore);
    
    return okay;
}

bool YMStreamClose(YMStreamRef stream)
{
    _YMStream *_stream = (_YMStream *)stream;
    
    int downstreamWrite = YMPipeGetInputFile(_stream->downstream);
    
    int result = close(downstreamWrite);
    return ( result == 0 );
}

void _YMStreamSetUserInfo(YMStreamRef stream, const void *userInfo)
{
    stream->__userInfo = userInfo;
}

const void *_YMStreamGetUserInfo(YMStreamRef stream)
{
    return stream->__userInfo;
}

void _YMStreamSetDataAvailableSemaphore(YMStreamRef stream, YMSemaphoreRef semaphore)
{
    stream->__dataAvailableSemaphore = semaphore;
}
