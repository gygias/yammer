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
    
    YMPipeRef upstream;
    bool upstreamWriteClosed;
    bool upstreamReadClosed;
    YMPipeRef downstream;
    bool downstreamWriteClosed;
    bool downstreamReadClosed;
    char *name;
    
    bool direct;
    
#pragma message "these should be userInfo, but that would entail plexer (session) being passed around by the client in order to WriteDown and signal the semaphore" \
        "instead add a 'write down happened' function ptr that the plexer can use to signal the sema, and a debugUserLogString (for stream id, etc) and all this can truly be opaque 'user data'"
    bool isLocallyOriginated;
    bool isFloating; // has been 'non-final' released, awaiting client RemoteRelease
    YMStreamUserInfoRef __userInfo; // weak, plexer
    YMSemaphoreRef __dataAvailableSemaphore; // weak, plexer
    struct timeval *__lastServiceTime;
    
    // also userInfo?
    bool isUserReleased;
} _YMStream;

YMStreamRef YMStreamCreate(const char *name, bool isLocallyOriginated, YMStreamUserInfoRef userInfo)
{
    YMStreamRef stream = (YMStreamRef)YMMALLOC(sizeof(struct __YMStream));
    stream->_type = _YMStreamTypeID;
    
    stream->name = strdup( name ? name : "unnamed" );
    stream->isLocallyOriginated = isLocallyOriginated;
    
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
    
    stream->__userInfo = userInfo;
    stream->__dataAvailableSemaphore = NULL;
    stream->__lastServiceTime = (struct timeval *)YMMALLOC(sizeof(struct timeval));
    if ( 0 != gettimeofday(stream->__lastServiceTime, NULL) )
    {
        YMLog("warning: error setting initial service time for stream: %d (%s)",errno,strerror(errno));
        YMGetTheBeginningOfPosixTimeForCurrentPlatform(stream->__lastServiceTime);
    }
    
    stream->isUserReleased = false;
    
    YMLog("  stream[%s,i%d->o%dV,^o%d<-i%d,s%u]: ALLOCATING",stream->name,
          YMPipeGetInputFile(stream->downstream),
          YMPipeGetOutputFile(stream->downstream),
          YMPipeGetOutputFile(stream->upstream),
          YMPipeGetInputFile(stream->upstream),
          stream->__userInfo->streamID);
    
    return (YMStreamRef)stream;
}

void __YMStreamCloseFiles(YMStreamRef stream, bool floatUpRead)
{
    int downWrite = YMPipeGetInputFile(stream->downstream);
    int downRead = YMPipeGetOutputFile(stream->downstream);
    int upWrite = YMPipeGetInputFile(stream->upstream);
    int upRead = YMPipeGetOutputFile(stream->upstream);
    YMLog("  stream[%s,i%d->o%dV,^o%d<-i%d,s%u]: DEALLOCATING(%s)",stream->name, downWrite, downRead, upRead, upWrite, stream->__userInfo->streamID,floatUpRead?"float":"final");
    
    int aClose;
    YMLog("  stream[%s]: CLOSING %d",stream->name,upWrite);
    aClose = close(upWrite);
    if ( aClose != 0 )
    {
        YMLog("  stream[%s,i%d->o%dV,^o%d<-i%d!,s%u]: fatal: close failed: %d (%s)",stream->name, downWrite, downRead, upRead, upWrite, stream->__userInfo->streamID,errno,strerror(errno));
        abort();
    }
    
    if ( ! floatUpRead )
    {
        aClose = close(upRead);
        if ( aClose != 0 )
        {
            YMLog("  stream[%s,i%d->o%dV,^o%d!<-i%d,s%u]: fatal: close failed: %d (%s)",stream->name, downWrite, downRead, upRead, upWrite, stream->__userInfo->streamID,errno,strerror(errno));
            abort();
        }
    }
    else
        YMLog("  stream[%s,i%d->o%dV,^o%d!<-i%d,s%u]: floating remotely originated up read %d, client to close",stream->name, downWrite, downRead, upRead, upWrite, stream->__userInfo->streamID, upRead);
    
    aClose = close(downWrite);
    if ( aClose != 0 )
    {
        YMLog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: fatal: close failed: %d (%s)",stream->name, downWrite, downRead, upRead, upWrite, stream->__userInfo->streamID,errno,strerror(errno));
        abort();
    }
    
    aClose = close(downRead);
    if ( aClose != 0 )
    {
        YMLog("  stream[%s,i%d->o%d!V,^o%d!<-i%d,s%u]: fatal: close failed: %d (%s)",stream->name, downWrite, downRead, upRead, upWrite, stream->__userInfo->streamID,errno,strerror(errno));
        abort();
    }
}

void _YMStreamFreeFinal(YMStreamRef stream, bool final)
{
    if ( ! stream->isFloating )
    {
        __YMStreamCloseFiles(stream, !final);
        YMFree(stream->downstream);
    }
    
    if ( final )
    {
        free(stream->name);
        free(stream->__lastServiceTime);
        YMFree(stream->upstream);
        free(stream);
    }
    else
    {
        stream->isFloating = true;
        YMLog("  stream[%s,s%u]: floating remotely originated stream %u",stream->name,stream->__userInfo->streamID,stream->__userInfo->streamID);
    }
}

void _YMStreamFree(YMTypeRef object)
{
    _YMStream *stream = (_YMStream *)object;
    _YMStreamFreeFinal(stream,stream->isLocallyOriginated);
}

void YMStreamWriteDown(YMStreamRef stream, const void *buffer, uint16_t length)
{
    int downstreamWrite = YMPipeGetInputFile(stream->downstream);
    int debugDownstreamRead = YMPipeGetOutputFile(stream->downstream);
    int debugUpstreamWrite = YMPipeGetInputFile(stream->upstream);
    int debugUpstreamRead = YMPipeGetOutputFile(stream->upstream);
    
    YMStreamCommand header = { length };
    
    YMLog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: writing header for command: %ub",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    YMIOResult result = YMWriteFull(downstreamWrite, (void *)&header, sizeof(header));
    if ( result != YMIOSuccess )
    {
        YMLog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: fatal: failed writing header for stream chunk size %ub",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
        abort();
    }
    YMLog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: wrote header for command: %u",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    
    result = YMWriteFull(downstreamWrite, buffer, length);
    if ( result != YMIOSuccess )
    {
        YMLog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: fatal: failed writing stream chunk with size %ub",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
        abort();
    }
    YMLog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: wrote buffer for chunk with size %ub",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    
    // signal the plexer to wake and service this stream
    YMSemaphoreSignal(stream->__dataAvailableSemaphore);
    
    YMLog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: wrote %lub + %ub command",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,sizeof(header),length);
}

// void: only ever does in-process i/o
void _YMStreamReadDown(YMStreamRef stream, void *buffer, uint32_t length)
{
    int debugDownstreamWrite = YMPipeGetInputFile(stream->downstream);
    int downstreamRead = YMPipeGetOutputFile(stream->downstream);
    int debugUpstreamWrite = YMPipeGetInputFile(stream->upstream);
    int debugUpstreamRead = YMPipeGetOutputFile(stream->upstream);
    YMLog("  stream[%s,i%d->o%d!V,^Vo%d<-i%d,s%u]: reading %ub from downstream",stream->name,debugDownstreamWrite,downstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    YMIOResult result = YMReadFull(downstreamRead, buffer, length);
    if ( result != YMIOSuccess )
    {
        YMLog("  stream[%s,i%d->o%d!V,^Vo%d<-i%d,s%u]: error: failed reading %ub from downstream",stream->name,debugDownstreamWrite,downstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
        abort();
    }
    
    YMLog("  stream[%s,i%d->o%d!V,^Vo%d<-i%d,s%u]: read %ub from downstream",stream->name,debugDownstreamWrite,downstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
}

// void: only ever does in-process i/o
void _YMStreamWriteUp(YMStreamRef stream, const void *buffer, uint32_t length)
{
    int debugDownstreamWrite = YMPipeGetInputFile(stream->downstream);
    int downstreamRead = YMPipeGetOutputFile(stream->downstream);
    int upstreamWrite = YMPipeGetInputFile(stream->upstream);
    int debugUpstreamRead = YMPipeGetOutputFile(stream->upstream);
    
    YMIOResult result = YMWriteFull(upstreamWrite, buffer, length);
    if ( result == YMIOError )
    {
        YMLog("  stream[%s,i%d!->o%dV,^Vo%d<-i%d,s%u]: fatal: failed writing %u bytes to upstream",stream->name,debugDownstreamWrite,downstreamRead,debugUpstreamRead,upstreamWrite,stream->__userInfo->streamID,length);
        abort();
    }
}

// because user data is opaque (even to user), this should expose eof
YMIOResult YMStreamReadUp(YMStreamRef stream, void *buffer, uint16_t length)
{
    int debugDownstreamWrite = YMPipeGetInputFile(stream->downstream);
    int downstreamRead = YMPipeGetOutputFile(stream->downstream);
    int debugUpstreamWrite = YMPipeGetInputFile(stream->upstream);
    int upstreamRead = YMPipeGetOutputFile(stream->upstream);
    
    YMLog("  stream[%s,i%d->o%dV,^Vo%d!<-i%d,s%u]: reading %ub user data",stream->name,debugDownstreamWrite,downstreamRead,upstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    YMIOResult result = YMReadFull(upstreamRead, buffer, length);
    if ( result == YMIOError ) // in-process i/o errors are fatal
    {
        YMLog("  stream[%s,i%d->o%dV,^Vo%d!<-i%d,s%u]: fatal: reading %ub user data: %d (%s)",stream->name,debugDownstreamWrite,downstreamRead,upstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length,errno,strerror(errno));
        abort();
    }
    else if ( result == YMIOEOF )
        YMLog("  stream[%s,i%d->o%dV,^Vo%d!<-i%d,s%u]: EOF from upstream",stream->name,debugDownstreamWrite,downstreamRead,upstreamRead,debugUpstreamWrite,stream->__userInfo->streamID);
    else
        YMLog("  stream[%s,i%d->o%dV,^Vo%d!<-i%d,s%u]: read %ub from upstream",stream->name,debugDownstreamWrite,downstreamRead,upstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    
    return result;
}

void _YMStreamClose(YMStreamRef stream)
{
    int downstreamWrite = YMPipeGetInputFile(stream->downstream);
    int debugDownstreamRead = YMPipeGetOutputFile(stream->downstream);
    int debugUpstreamRead = YMPipeGetInputFile(stream->upstream);
    int debugUpstreamWrite = YMPipeGetOutputFile(stream->upstream);
    
    YMStreamCommand command = { YMStreamClose };
    YMIOResult result = YMWriteFull(downstreamWrite, (void *)&command, sizeof(YMStreamClose));
    if ( result != YMIOSuccess )
    {
        YMLog("  stream[%s,Vi%d!->o%d,^o%d<-i%d,s%u]: fatal: writing close byte to plexer: %d (%s)",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,errno,strerror(errno));
        abort();
    }
    YMSemaphoreSignal(stream->__dataAvailableSemaphore);
    
    YMLog("  stream[%s,Vi%d!->o%d,^o%d<-i%d,s%u]: CLOSED downstream",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID);
}

void _YMStreamCloseUp(YMStreamRef stream)
{
    int debugdownstreamWrite = YMPipeGetInputFile(stream->downstream);
    int debugDownstreamRead = YMPipeGetOutputFile(stream->downstream);
    int debugUpstreamRead = YMPipeGetInputFile(stream->upstream);
    int upstreamWrite = YMPipeGetOutputFile(stream->upstream);
    
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

void _YMStreamSetLastServiceTimeNow(YMStreamRef stream)
{
    if ( 0 != gettimeofday(stream->__lastServiceTime, NULL) )
    {
        YMLog("warning: error setting initial service time for stream: %d (%s)",errno,strerror(errno));
        YMGetTheBeginningOfPosixTimeForCurrentPlatform(stream->__lastServiceTime);
    }
}

struct timeval *_YMStreamGetLastServiceTime(YMStreamRef stream)
{
    return stream->__lastServiceTime;
}

bool _YMStreamIsLocallyOriginated(YMStreamRef stream)
{
    return stream->isLocallyOriginated;
}

void _YMStreamSetUserReleased(YMStreamRef stream)
{
    stream->isUserReleased = true;
}
