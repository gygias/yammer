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
    bool upstreamWriteClosed;
    bool upstreamReadClosed;
    YMPipeRef downstream;
    bool downstreamWriteClosed;
    bool downstreamReadClosed;
    char *name;
    
    bool direct;
    
    bool __externalClosed;
    YMStreamUserInfoRef __userInfo; // weak, plexer
    YMSemaphoreRef __dataAvailableSemaphore; // weak, plexer
    struct timeval *__lastServiceTime;
} _YMStream;

YMStreamRef YMStreamCreate(const char *name, bool isLocal, YMStreamUserInfoRef userInfo)
{
    YMStreamRef stream = (YMStreamRef)YMMALLOC(sizeof(struct __YMStream));
    stream->name = strdup( name ? name : "unnamed" );
    
    stream->isLocal = isLocal;
    
    char *upStreamName = YMStringCreateWithFormat("%s-up",name);
    stream->upstream = YMPipeCreate(upStreamName);
    free(upStreamName);
    stream->upstreamWriteClosed = false;
    stream->upstreamReadClosed = false;
    
    char *downStreamName = YMStringCreateWithFormat("%s-down",name);
    stream->downstream = YMPipeCreate(downStreamName);
    free(downStreamName);
    stream->downstreamWriteClosed = false;
    stream->downstreamReadClosed = false;
    
    stream->__externalClosed = false;
    stream->__userInfo = userInfo;
    stream->__dataAvailableSemaphore = NULL;
    stream->__lastServiceTime = (struct timeval *)YMMALLOC(sizeof(struct timeval));
    if ( 0 != gettimeofday(stream->__lastServiceTime, NULL) )
    {
        YMLog("warning: error setting initial service time for stream: %d (%s)",errno,strerror(errno));
        YMSetTheBeginningOfPosixTimeForCurrentPlatform(stream->__lastServiceTime);
    }
    
    YMLog("  stream[%s,i%d->o%dV,^i%d<-o%d,s%u]: ALLOCATING",stream->name,
          YMPipeGetInputFile(stream->downstream),
          YMPipeGetOutputFile(stream->downstream),
          YMPipeGetOutputFile(stream->upstream),
          YMPipeGetInputFile(stream->upstream),
          stream->__userInfo->streamID);
    
    return (YMStreamRef)stream;
}

void _YMStreamFree(YMTypeRef object)
{
    _YMStream *stream = (_YMStream *)object;
    
    YMLog("  stream[%s,i%d->o%dV,^i%d<-o%d,s%u]: DEALLOCATING",stream->name,
          YMPipeGetInputFile(stream->downstream),
          YMPipeGetOutputFile(stream->downstream),
          YMPipeGetOutputFile(stream->upstream),
          YMPipeGetInputFile(stream->upstream),
          stream->__userInfo->streamID);
    
    free(stream->name);
    free(stream->__lastServiceTime);
    _YMStreamFree(stream->upstream);
    _YMStreamFree(stream->downstream);
    
    free(stream);
}

void YMStreamWriteDown(YMStreamRef stream, const void *buffer, uint16_t length)
{
    int downstreamWrite = YMPipeGetInputFile(stream->downstream);
    int debugDownstreamRead = YMPipeGetOutputFile(stream->downstream);
    
    YMStreamCommand header = { length };
    
    YMLog("  stream[%s,i%d->o%d!V,s%u]: writing header for command: %u",stream->name,downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,length);
    YMIOResult result = YMWriteFull(downstreamWrite, (void *)&header, sizeof(header));
    if ( result != YMIOSuccess )
    {
        YMLog("  stream[%s,i%d->o%d!V,s%u]: fatal: failed writing header for stream chunk size %u",stream->name,downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,length);
        abort();
    }
    YMLog("  stream[%s,i%d->o%d!V,s%u]: wrote header for command: %u",stream->name,downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,length);
    
    result = YMWriteFull(downstreamWrite, buffer, length);
    if ( result != YMIOSuccess )
    {
        YMLog("  stream[%s,i%d->o%d!V,s%u]: fatal: failed writing stream chunk with size %u",stream->name,downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,length);
        abort();
    }
    YMLog("  stream[%s,i%d->o%d!V,s%u]: wrote buffer for command %u",stream->name,downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,length);
    
    // signal the plexer to wake and service this stream
#pragma message "should this lock, then ensure the sempahore isn't currently signaled before signaling again? wouldn't think so with stream chunk making each read 1-1"
    YMSemaphoreSignal(stream->__dataAvailableSemaphore);
    
    YMLog("  stream[%s,i%d->o%d!V,s%u]: wrote %lu + %u command",stream->name,downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,sizeof(header),length);
}

// void: only ever does in-process i/o
void _YMStreamReadDown(YMStreamRef stream, void *buffer, uint32_t length)
{
    int downstreamRead = YMPipeGetOutputFile(stream->downstream);
    int debugDownstreamWrite = YMPipeGetInputFile(stream->downstream);
    YMLog("  stream[%s,i%d!->o%dV,s%u]: reading %u bytes from downstream",stream->name,debugDownstreamWrite,downstreamRead,stream->__userInfo->streamID,length);
    YMIOResult result = YMReadFull(downstreamRead, buffer, length);
    if ( result != YMIOSuccess )
    {
        YMLog("  stream[%s,i%d!->o%dV,s%u]: error: failed reading %u bytes from downstream",stream->name,debugDownstreamWrite,downstreamRead,stream->__userInfo->streamID,length);
        abort();
    }
    
    YMLog("  stream[%s,i%d!->o%dV,s%u]: read %u bytes from downstream",stream->name,debugDownstreamWrite,downstreamRead,stream->__userInfo->streamID,length);
}

// void: only ever does in-process i/o
void _YMStreamWriteUp(YMStreamRef stream, const void *buffer, uint32_t length)
{
    int upstreamWrite = YMPipeGetInputFile(stream->upstream);
    int debugUpstreamRead = YMPipeGetOutputFile(stream->upstream);
    
    YMIOResult result = YMWriteFull(upstreamWrite, buffer, length);
    if ( result == YMIOError )
    {
        YMLog("  stream[%s,i%d->o%d!^,s%u]: fatal: failed writing %u bytes to upstream",stream->name,upstreamWrite,debugUpstreamRead,stream->__userInfo->streamID,length);
        abort();
    }
}

#pragma message "NOW allow EOF here, after reviewing the rest NOW"
void YMStreamReadUp(YMStreamRef stream, void *buffer, uint16_t length)
{
    int upstreamRead = YMPipeGetOutputFile(stream->upstream);
    int debugUpstreamWrite = YMPipeGetInputFile(stream->upstream);
    
    YMLog("  stream[%s,i%d!->o%d^,s%u]: reading %ub user data",stream->name,debugUpstreamWrite,upstreamRead,stream->__userInfo->streamID,length);
    YMIOResult result = YMReadFull(upstreamRead, buffer, length);
    if ( result != YMIOSuccess ) // in-process i/o errors are fatal
    {
        YMLog("  stream[%s,i%d!->o%d^,s%u]: fatal: reading %ub user data: %d (%s)",stream->name,debugUpstreamWrite,upstreamRead,stream->__userInfo->streamID,length,errno,strerror(errno));
        abort();
    }
    //else if ( result == YMIOEOF )
    //    YMLog("  stream[%s,i%d,o%d,s%u]: EOF from upstream",stream->name,debugUpstreamWrite,upstreamRead,stream->__userInfo->streamID);
    //else
        YMLog("  stream[%s,i%d!->o%d^,s%u]: read %u bytes from upstream",stream->name,debugUpstreamWrite,upstreamRead,stream->__userInfo->streamID,length);
    
    //return result;
}

void _YMStreamClose(YMStreamRef stream)
{
    int downstreamWrite = YMPipeGetInputFile(stream->downstream);
    int debugDownstreamRead = YMPipeGetOutputFile(stream->downstream);
    int debugUpstreamRead = YMPipeGetInputFile(stream->upstream);
    int debugUpstreamWrite = YMPipeGetOutputFile(stream->upstream);
    
#pragma message "does it make more sense to just write a 'close stream command' and then close the fd, rather than force plexer to literally 'read eof'?"
    //stream->downstreamWriteClosed = true;
    
    YMStreamCommand command = { YMStreamClose };
    YMIOResult result = YMWriteFull(downstreamWrite, (void *)&command, sizeof(YMStreamClose));
    if ( result != YMIOSuccess )
    {
        YMLog("  stream[%s,Vi%d!->o%d,^o%d<-i%d,s%u]: fatal: writing close byte to plexer: %d (%s)",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,errno,strerror(errno));
        abort();
    }
    YMSemaphoreSignal(stream->__dataAvailableSemaphore);
    
//    int posixResult = close(downstreamWrite);
//    if ( posixResult != 0 )
//    {
//        YMLog("  stream[%s,%d!->%d,s%u]: fatal: failed closing downstream write: %d (%s) (internally %s)",stream->name,downstreamWrite,debugDownstreamRead,stream->__userInfo->streamID,errno,strerror(errno),stream->downstreamWriteClosed?"closed":"open");
//        abort();
//        return false;
//    }
    
    YMLog("  stream[%s,Vi%d!->o%d,^o%d<-i%d,s%u]: CLOSED downstream",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID);
    //return ( result == 0 );
}

void _YMStreamCloseUp(YMStreamRef stream)
{
    int debugdownstreamWrite = YMPipeGetInputFile(stream->downstream);
    int debugDownstreamRead = YMPipeGetOutputFile(stream->downstream);
    int debugUpstreamRead = YMPipeGetInputFile(stream->upstream);
    int upstreamWrite = YMPipeGetOutputFile(stream->upstream);
    
#pragma message "does it make more sense to just write a 'close stream command' and then close the fd, rather than force plexer to literally 'read eof'?"
    stream->upstreamWriteClosed = true;
    int result = close(upstreamWrite);
    if ( result != 0 )
    {
        YMLog("  stream[%s,i%d->o%dV,^o%d<-i%d!,s%u]: fatal: writing close byte to plexer: %d (%s)",stream->name,debugdownstreamWrite,debugDownstreamRead,debugUpstreamRead,upstreamWrite,stream->__userInfo->streamID,errno,strerror(errno));
        abort();
    }
    
    YMLog("  stream[%s,i%d->o%d!V,^o%d<-i%d!,s%u]: CLOSED downstream",stream->name,debugdownstreamWrite,debugDownstreamRead,debugUpstreamRead,upstreamWrite,stream->__userInfo->streamID);
}

bool _YMStreamIsClosed(YMStreamRef stream)
{
#pragma message "should we lock around methods which touch/read fd state?"
    return stream->downstreamWriteClosed;
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

YMStreamUserInfoRef _YMStreamGetUserInfo(YMStreamRef stream)
{
    return stream->__userInfo;
}

void _YMStreamSetDataAvailableSemaphore(YMStreamRef stream, YMSemaphoreRef semaphore)
{
    stream->__dataAvailableSemaphore = semaphore;
}

#pragma message "SEMAPHORE DEBACLE"
YMSemaphoreRef __YMStreamGetSemaphore(YMStreamRef stream)
{
    return stream->__dataAvailableSemaphore;
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
