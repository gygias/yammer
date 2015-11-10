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

typedef struct __YMStream
{
    YMTypeID _type;
    
    YMPipeRef upstreamPipe;
    bool upstreamWriteClosed;
    bool upstreamReadClosed;
    YMPipeRef downstreamPipe;
    bool downstreamWriteClosed;
    bool downstreamReadClosed;
    char *name;
    
    bool direct;
    
#pragma message "these should be userInfo, but that would entail plexer (session) being passed around by the client in order to WriteDown and signal the semaphore" \
        "instead add a 'write down happened' function ptr that the plexer can use to signal the sema, and a debugUserLogString (for stream id, etc) and all this can truly be opaque 'user data'"
    bool isLocallyOriginated;
    YMStreamUserInfoRef __userInfo; // weak, plexer
    YMSemaphoreRef __dataAvailableSemaphore; // weak, plexer
    struct timeval *__lastServiceTime; // free me
    
    // also userInfo?
    YMLockRef retainLock; // used to synchronize between local stream writing 'close' & plexer free'ing, and between remote exiting -newStream & plexer free'ing
    bool isPlexerReleased;
    bool isUserReleased;
    bool isDeallocated;
} _YMStream;

void __YMStreamFree(YMStreamRef stream);
void __YMStreamCloseFiles(YMStreamRef stream);

YMStreamRef YMStreamCreate(const char *name, bool isLocallyOriginated, YMStreamUserInfoRef userInfo)
{
    YMStreamRef stream = (YMStreamRef)YMMALLOC(sizeof(struct __YMStream));
    stream->_type = _YMStreamTypeID;
    
    stream->name = strdup( name ? name : "unnamed" );
    stream->isLocallyOriginated = isLocallyOriginated;
    
    char *upStreamName = YMStringCreateWithFormat("%s-up",name);
    stream->upstreamPipe = YMPipeCreate(upStreamName);
    free(upStreamName);
    stream->upstreamWriteClosed = false;
    stream->upstreamReadClosed = false;
    
    char *downStreamName = YMStringCreateWithFormat("%s-down",name);
    stream->downstreamPipe = YMPipeCreate(downStreamName);
    free(downStreamName);
    stream->downstreamWriteClosed = false;
    stream->downstreamReadClosed = false;
    
    stream->__userInfo = userInfo;
    stream->__dataAvailableSemaphore = NULL;
    stream->__lastServiceTime = (struct timeval *)YMMALLOC(sizeof(struct timeval));
    if ( 0 != gettimeofday(stream->__lastServiceTime, NULL) )
    {
        ymlog("warning: error setting initial service time for stream: %d (%s)",errno,strerror(errno));
        YMGetTheBeginningOfPosixTimeForCurrentPlatform(stream->__lastServiceTime);
    }
    
    stream->retainLock = YMLockCreateWithOptionsAndName(YMLockDefault, stream->name);
    stream->isUserReleased = false;
    stream->isPlexerReleased = false;
    stream->isDeallocated = false;
    
    if ( ymlog_stream_lifecycle )
        YMLogType(YMLogStreamLifecycle,"  stream[%s,i%d->o%dV,^o%d<-i%d,s%u]: %p allocating",stream->name,
          YMPipeGetInputFile(stream->downstreamPipe),
          YMPipeGetOutputFile(stream->downstreamPipe),
          YMPipeGetOutputFile(stream->upstreamPipe),
          YMPipeGetInputFile(stream->upstreamPipe),
          stream->__userInfo->streamID,
          stream);
    
    return (YMStreamRef)stream;
}

void _YMStreamFree(YMTypeRef object)
{
    YMStreamRef stream = (YMStreamRef)object;
    ymerr("  stream[%s,s%u]: fatal: stream cannot be free'd directly",stream->name,stream->__userInfo->streamID);
}

void _YMStreamDesignatedFree(YMStreamRef stream )
{
    if ( ! stream->isLocallyOriginated )
    {
        ymerr("  stream[%s,s%u]: fatal: _YMStreamFree called on remote-originated",stream->name,stream->__userInfo->streamID);
        abort();
    }
    
    __YMStreamFree(stream);
}

void __YMStreamFree(YMStreamRef stream)
{
    if ( ymlog_stream_lifecycle )
        YMLogType(YMLogStreamLifecycle,"  stream[%s,i%d->o%dV,^o%d<-i%d,s%u]: %p deallocating",stream->name,
          YMPipeGetInputFile(stream->downstreamPipe),
          YMPipeGetOutputFile(stream->downstreamPipe),
          YMPipeGetOutputFile(stream->upstreamPipe),
          YMPipeGetInputFile(stream->upstreamPipe),
          stream->__userInfo->streamID,
          stream);
    
    YMFree(stream->downstreamPipe);
    YMFree(stream->upstreamPipe);
    YMFree(stream->retainLock);
    
    free(stream->name);
    if ( stream->__userInfo ) // optional
        free(stream->__userInfo);
    free(stream->__lastServiceTime);
    free(stream);
    
}

void YMStreamWriteDown(YMStreamRef stream, const void *buffer, uint16_t length)
{
    int downstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    int debugDownstreamRead = YMPipeGetOutputFile(stream->downstreamPipe);
    int debugUpstreamWrite = YMPipeGetInputFile(stream->upstreamPipe);
    int debugUpstreamRead = YMPipeGetOutputFile(stream->upstreamPipe);
    
    YMStreamCommand header = { length };
    
    ymlog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: writing header for command: %ub",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    YMIOResult result = YMWriteFull(downstreamWrite, (void *)&header, sizeof(header));
    if ( result != YMIOSuccess )
    {
        ymerr("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: fatal: failed writing header for stream chunk size %ub",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
        abort();
    }
    ymlog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: wrote header for command: %u",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    
    result = YMWriteFull(downstreamWrite, buffer, length);
    if ( result != YMIOSuccess )
    {
        ymerr("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: fatal: failed writing stream chunk with size %ub",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
        abort();
    }
    ymlog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: wrote buffer for chunk with size %ub",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    
    // signal the plexer to wake and service this stream
    YMSemaphoreSignal(stream->__dataAvailableSemaphore);
    
    ymlog("  stream[%s,i%d!->o%dV,^o%d<-i%d,s%u]: wrote %lub + %ub command",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,sizeof(header),length);
}

// void: only ever does in-process i/o
void _YMStreamReadDown(YMStreamRef stream, void *buffer, uint32_t length)
{
    int debugDownstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    int downstreamRead = YMPipeGetOutputFile(stream->downstreamPipe);
    int debugUpstreamWrite = YMPipeGetInputFile(stream->upstreamPipe);
    int debugUpstreamRead = YMPipeGetOutputFile(stream->upstreamPipe);
    ymlog("  stream[%s,i%d->o%d!V,^Vo%d<-i%d,s%u]: reading %ub from downstream",stream->name,debugDownstreamWrite,downstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    YMIOResult result = YMReadFull(downstreamRead, buffer, length);
    if ( result != YMIOSuccess )
    {
        ymerr("  stream[%s,i%d->o%d!V,^Vo%d<-i%d,s%u]: error: failed reading %ub from downstream",stream->name,debugDownstreamWrite,downstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
        abort();
    }
    
    ymlog("  stream[%s,i%d->o%d!V,^Vo%d<-i%d,s%u]: read %ub from downstream",stream->name,debugDownstreamWrite,downstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
}

// void: only ever does in-process i/o
void _YMStreamWriteUp(YMStreamRef stream, const void *buffer, uint32_t length)
{
    int debugDownstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    int downstreamRead = YMPipeGetOutputFile(stream->downstreamPipe);
    int upstreamWrite = YMPipeGetInputFile(stream->upstreamPipe);
    int debugUpstreamRead = YMPipeGetOutputFile(stream->upstreamPipe);
    
    YMIOResult result = YMWriteFull(upstreamWrite, buffer, length);
    if ( result == YMIOError )
    {
        ymerr("  stream[%s,i%d!->o%dV,^Vo%d<-i%d,s%u]: fatal: failed writing %u bytes to upstream",stream->name,debugDownstreamWrite,downstreamRead,debugUpstreamRead,upstreamWrite,stream->__userInfo->streamID,length);
        abort();
    }
}

// because user data is opaque (even to user), this should expose eof
YMIOResult YMStreamReadUp(YMStreamRef stream, void *buffer, uint16_t length)
{
    int debugDownstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    int downstreamRead = YMPipeGetOutputFile(stream->downstreamPipe);
    int debugUpstreamWrite = YMPipeGetInputFile(stream->upstreamPipe);
    int upstreamRead = YMPipeGetOutputFile(stream->upstreamPipe);
    
    ymlog("  stream[%s,i%d->o%dV,^Vo%d!<-i%d,s%u]: reading %ub user data",stream->name,debugDownstreamWrite,downstreamRead,upstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    YMIOResult result = YMReadFull(upstreamRead, buffer, length);
    if ( result == YMIOError ) // in-process i/o errors are fatal
    {
        ymerr("  stream[%s,i%d->o%dV,^Vo%d!<-i%d,s%u]: fatal: reading %ub user data: %d (%s)",stream->name,debugDownstreamWrite,downstreamRead,upstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length,errno,strerror(errno));
        abort();
    }
    else if ( result == YMIOEOF )
        ymerr("  stream[%s,i%d->o%dV,^Vo%d!<-i%d,s%u]: EOF from upstream",stream->name,debugDownstreamWrite,downstreamRead,upstreamRead,debugUpstreamWrite,stream->__userInfo->streamID);
    else
        ymlog("  stream[%s,i%d->o%dV,^Vo%d!<-i%d,s%u]: read %ub from upstream",stream->name,debugDownstreamWrite,downstreamRead,upstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,length);
    
    return result;
}

void _YMStreamClose(YMStreamRef stream)
{
    int downstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    int debugDownstreamRead = YMPipeGetOutputFile(stream->downstreamPipe);
    int debugUpstreamRead = YMPipeGetInputFile(stream->upstreamPipe);
    int debugUpstreamWrite = YMPipeGetOutputFile(stream->upstreamPipe);
    
    // if we are closing an outgoing stream, the plexer can race us between our write of the 'stream close' command here,
    // and freeing this stream object. synchronization is not guaranteed by our semaphore signal below, as all stream signals
    // will wake the plexer, and we may win the 'oldest unserviced' selection before we exit this function and the client
    // fully relinquishes ownership.
    YMLockLock(stream->retainLock);
    {
        YMStreamCommand command = { YMStreamClose };
        YMIOResult result = YMWriteFull(downstreamWrite, (void *)&command, sizeof(YMStreamClose));
        if ( result != YMIOSuccess )
        {
            ymerr("  stream[%s,Vi%d!->o%d,^o%d<-i%d,s%u]: fatal: writing close byte to plexer: %d (%s)",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID,errno,strerror(errno));
            abort();
        }
        
        ymlog("  stream[%s,Vi%d!->o%d,^o%d<-i%d,s%u]: closing stream",stream->name,downstreamWrite,debugDownstreamRead,debugUpstreamRead,debugUpstreamWrite,stream->__userInfo->streamID);
        YMSemaphoreSignal(stream->__dataAvailableSemaphore);
    }
    YMLockUnlock(stream->retainLock);
}

void _YMStreamCloseUp(YMStreamRef stream)
{
    int debugdownstreamWrite = YMPipeGetInputFile(stream->downstreamPipe);
    int debugDownstreamRead = YMPipeGetOutputFile(stream->downstreamPipe);
    int debugUpstreamRead = YMPipeGetInputFile(stream->upstreamPipe);
    int upstreamWrite = YMPipeGetOutputFile(stream->upstreamPipe);
    
    stream->upstreamWriteClosed = true;
    int result = close(upstreamWrite);
    if ( result != 0 )
    {
        ymerr("  stream[%s,i%d->o%dV,^o%d<-i%d!,s%u]: fatal: writing close byte to plexer: %d (%s)",stream->name,debugdownstreamWrite,debugDownstreamRead,debugUpstreamRead,upstreamWrite,stream->__userInfo->streamID,errno,strerror(errno));
        abort();
    }
    
    ymlog("  stream[%s,i%d->o%d!V,^o%d<-i%d!,s%u]: CLOSED downstream",stream->name,debugdownstreamWrite,debugDownstreamRead,debugUpstreamRead,upstreamWrite,stream->__userInfo->streamID);
}

bool _YMStreamIsClosed(YMStreamRef stream)
{
    return stream->downstreamWriteClosed;
}

int _YMStreamGetDownwardWrite(YMStreamRef stream)
{
    return YMPipeGetInputFile(stream->downstreamPipe);
}

int _YMStreamGetDownwardRead(YMStreamRef stream)
{
    return YMPipeGetOutputFile(stream->downstreamPipe);
}

int _YMStreamGetUpstreamWrite(YMStreamRef stream)
{
    return YMPipeGetInputFile(stream->upstreamPipe);
}

int _YMStreamGetUpstreamRead(YMStreamRef stream)
{
    return YMPipeGetOutputFile(stream->upstreamPipe);
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
        ymerr("warning: error setting initial service time for stream: %d (%s)",errno,strerror(errno));
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

const char *_YMStreamGetName(YMStreamRef stream)
{
    return stream->name;
}

#pragma mark stream retain & release

void __YMStreamCheckAndRelease(YMStreamRef stream)
{
    bool dealloc = false;
    YMLockLock(stream->retainLock);
    {
        if ( stream->isDeallocated )
            return;
        
        if ( stream->isPlexerReleased && stream->isUserReleased )
        {
            dealloc = true;
            stream->isDeallocated = true;
        }
    }
    YMLockUnlock(stream->retainLock);
    
    if ( dealloc )
        __YMStreamFree(stream);
}

void _YMStreamRemoteSetPlexerReleased(YMStreamRef stream)
{
    stream->isPlexerReleased = true;
    ymlog("  stream[%s,s%u]: plexer released",stream->name,stream->__userInfo->streamID);
    __YMStreamCheckAndRelease(stream);
}

void _YMStreamRemoteSetUserReleased(YMStreamRef stream)
{
    stream->isUserReleased = true;
    ymlog("  stream[%s,s%u]: user released",stream->name,stream->__userInfo->streamID);
    __YMStreamCheckAndRelease(stream);
}

YMLockRef _YMStreamGetRetainLock(YMStreamRef stream)
{
    return stream->retainLock;
}
