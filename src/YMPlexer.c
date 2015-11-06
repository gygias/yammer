//
//  YMPlexer.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPlexer.h"
#include "YMPrivate.h"
#include "YMStreamPriv.h"

#include "YMSecurityProvider.h"
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMThread.h"

#include <sys/select.h>

#ifdef USE_FTIME
#include <sys/timeb.h>
#error todo
#else
#include <sys/time.h>
#endif

#define YMPlexerBuiltInVersion ((uint32_t)1)

typedef struct {
    uint32_t protocolVersion;
    uint32_t masterStreamIDMin;
    uint32_t masterStreamIDMax;
} YMPlexerMasterInitializer;

typedef struct {
    uint32_t protocolVersion;
} YMPlexerSlaveAck;

typedef uint32_t YMPlexerChunkSize;
typedef struct {
    YMStreamID streamID;
    YMPlexerChunkSize length;
} YMPlexerChunkHeader;

bool _YMPlexerStartServiceThreads(YMPlexerRef plexer);
bool _YMPlexerDoInitialization(YMPlexerRef plexer, bool master);
bool _YMPlexerInitAsMaster(YMPlexerRef plexer);
bool _YMPlexerInitAsSlave(YMPlexerRef plexer);
void *_YMPlexerServiceDownstreamThread(void *context);
void *_YMPlexerServiceUpstreamThread(void *context);
YMStreamRef _YMPlexerChooseDownstream(YMPlexerRef plexer, int *outReadyStreams);
bool _YMPlexerServiceDownstream(YMPlexerRef plexer, YMStreamRef servicingStream);
YMStreamRef _YMPlexerGetOrCreateStreamWithID(YMPlexerRef plexer, YMStreamID streamID);
void _YMPlexerInterrupt(YMPlexerRef plexer);

typedef struct __YMPlexer
{
    YMTypeID _typeID;
    
    int inFd;
    int outFd;
    char *name;
    bool active; // intialized and happy
    bool master;
    YMSecurityProviderRef provider;
    
    // the downstream
    YMDictionaryRef localStreamsByID;
    YMLockRef localAccessLock;
    uint8_t *localPlexBuffer;
    uint32_t localPlexBufferSize;
    YMStreamID localStreamIDMin;
    YMStreamID localStreamIDMax;
    YMStreamID localStreamIDLast;
    
    // the upstream
    YMDictionaryRef remoteStreamsByID;
    YMLockRef remoteAccessLock;
    uint8_t *remotePlexBuffer;
    uint32_t remotePlexBufferSize;
    YMStreamID remoteStreamIDMin;
    YMStreamID remoteStreamIDMax;
    
    YMThreadRef localServiceThread;
    YMThreadRef remoteServiceThread;
    YMThreadRef eventDeliveryThread;
    YMLockRef interruptionLock;
    YMSemaphoreRef localDataAvailableSemaphore;
    
    // user
    ym_plexer_interrupted_func interruptedFunc;
    ym_plexer_new_upstream_func newIncomingFunc;
    ym_plexer_stream_closing_func closingFunc;
} _YMPlexer;

#define YMPlexerDefaultBufferSize (1e+6)

YMPlexerRef YMPlexerCreateWithFullDuplexFile(int fd, bool master)
{
    return YMPlexerCreate(fd, fd, master);
}

YMPlexerRef YMPlexerCreate(int inFd, int outFd, bool master)
{
    _YMPlexer *plexer = (_YMPlexer *)calloc(1,sizeof(_YMPlexer));
    plexer->_typeID = _YMPlexerTypeID;
    
    plexer->provider = YMSecurityProviderCreate(inFd, outFd);
    
    plexer->localStreamsByID = YMDictionaryCreate();
    plexer->localAccessLock = YMLockCreate();
    plexer->localPlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->localPlexBuffer = malloc(plexer->localPlexBufferSize);
    
    plexer->remoteStreamsByID = YMDictionaryCreate();
    plexer->remoteAccessLock = YMLockCreate();
    plexer->remotePlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->remotePlexBuffer = malloc(plexer->remotePlexBufferSize);
    
    char *threadName = YMStringCreateWithFormat("plex-%s-down",master?"m":"s");
    plexer->localServiceThread = YMThreadCreate(threadName, _YMPlexerServiceDownstreamThread, plexer);
    free(threadName);
    
    threadName = YMStringCreateWithFormat("plex-%s-up",master?"m":"s");
    plexer->remoteServiceThread = YMThreadCreate(threadName, _YMPlexerServiceUpstreamThread, plexer);
    free(threadName);
    
    threadName = YMStringCreateWithFormat("plex-%s-event",master?"m":"s");
    plexer->eventDeliveryThread = YMThreadDispatchCreate(threadName);
    free(threadName);
    
    plexer->interruptionLock = YMLockCreate();
    plexer->localDataAvailableSemaphore = YMSemaphoreCreate("plexer-signal");
    
    plexer->inFd = inFd;
    plexer->outFd = outFd;
    plexer->master = master;
    plexer->active = false;
    return plexer;
}

void _YMPlexerFree(YMPlexerRef plexer)
{
    free(plexer);
}

void YMPlexerSetInterruptedFunc(YMPlexerRef plexer, ym_plexer_interrupted_func func)
{
    plexer->interruptedFunc = func;
}

void YMPlexerSetNewIncomingStreamFunc(YMPlexerRef plexer, ym_plexer_new_upstream_func func)
{
    plexer->newIncomingFunc = func;
}

void YMPlexerSetStreamClosingFunc(YMPlexerRef plexer, ym_plexer_stream_closing_func func)
{
    plexer->closingFunc = func;
}

void YMPlexerSetSecurityProvider(YMPlexerRef plexer, YMTypeRef provider)
{
    YMTypeID type = ((_YMTypeRef *)provider)->_typeID;
    if ( type != _YMSecurityProviderTypeID )
        YMLog("plexer[%s]: warning: %s: provider is type '%c'",plexer->master?"master":"slave",__FUNCTION__,type);
    plexer->provider = (YMSecurityProviderRef)provider;
}

bool YMPlexerStart(YMPlexerRef plexer)
{
    bool okay;
    
    if ( plexer->active )
    {
        YMLog("plexer[%s]: user error: this plexer is already initialized",plexer->master?"master":"slave");
        return false;
    }
    
    if ( plexer->master )
        okay = _YMPlexerInitAsMaster(plexer);
    else
        okay = _YMPlexerInitAsSlave(plexer);
    
    if ( ! okay )
        goto catch_fail;
    
    YMLog("plexer[%s]: initialized m[%u:%u], s[%u:%u]",plexer->master?"master":"slave",
          plexer->master ? plexer->localStreamIDMin : plexer->remoteStreamIDMin,
          plexer->master ? plexer->localStreamIDMax : plexer->remoteStreamIDMax,
          plexer->master ? plexer->remoteStreamIDMin : plexer->localStreamIDMin,
          plexer->master ? plexer->remoteStreamIDMax : plexer->localStreamIDMax);
    
    // this flag is used to let our threads exit, among other things
    plexer->active = true;
    
    okay = YMThreadStart(plexer->localServiceThread);
    if ( ! okay )
    {
        YMLog("plexer[%s]: error: failed to detach down service thread",plexer->master?"master":"slave");
        goto catch_fail;
    }
    
    okay = YMThreadStart(plexer->remoteServiceThread);
    if ( ! okay )
    {
        YMLog("plexer[%s]: error: failed to detach up service thread",plexer->master?"master":"slave");
        goto catch_fail;
    }
    
    okay = YMThreadStart(plexer->eventDeliveryThread);
    if ( ! okay )
    {
        YMLog("plexer[%s]: error: failed to detach event thread",plexer->master?"master":"slave");
        goto catch_fail;
    }
    
catch_fail:
    return okay;
}

const char *YMPlexerMasterHello = "オス、王様でおるべし";
const char *YMPlexerSlaveHello = "よろしくお願いいたします";

bool _YMPlexerInitAsMaster(YMPlexerRef plexer)
{
    char *errorTemplate = "plexer[master]: error: plexer init failed: %s";
    bool okay = YMSecurityProviderWrite(plexer->provider, (void *)YMPlexerMasterHello, strlen(YMPlexerMasterHello));
    if ( ! okay )
    {
        YMLog(errorTemplate,"master hello write");
        return false;
    }
    
    unsigned long inHelloLen = strlen(YMPlexerSlaveHello);
    char inHello[inHelloLen];
    okay = YMSecurityProviderRead(plexer->provider, (void *)inHello, inHelloLen);
    if ( ! okay || memcmp(YMPlexerSlaveHello,inHello,inHelloLen) )
    {
        YMLog(errorTemplate,"slave hello read");
        return false;
    }
    
    plexer->localStreamIDMin = 0;
    plexer->localStreamIDMax = YMStreamIDMax / 2;
    plexer->localStreamIDLast = plexer->localStreamIDMax;
    plexer->remoteStreamIDMin = plexer->localStreamIDMax + 1;
    plexer->remoteStreamIDMax = YMStreamIDMax;
    
    YMPlexerMasterInitializer initializer = { YMPlexerBuiltInVersion, plexer->localStreamIDMin, plexer->localStreamIDMax };
    okay = YMSecurityProviderWrite(plexer->provider, (void *)&initializer, sizeof(initializer));
    if ( ! okay )
    {
        YMLog(errorTemplate,"master init write");
        return false;
    }
    
    YMPlexerSlaveAck ack;
    okay = YMSecurityProviderRead(plexer->provider, (void *)&ack, sizeof(ack));
    if ( ! okay )
    {
        YMLog(errorTemplate,"slave ack read");
        return false;
    }
    if ( ack.protocolVersion > YMPlexerBuiltInVersion )
    {
        YMLog(errorTemplate,"protocol mismatch");
        return false;
    }
    
    return true;
}

bool _YMPlexerInitAsSlave(YMPlexerRef plexer)
{
    char *errorTemplate = "plexer[slave]: error: plexer init failed: %s";
    unsigned long inHelloLen = strlen(YMPlexerMasterHello);
    char inHello[inHelloLen];
    bool okay = YMSecurityProviderRead(plexer->provider, (void *)inHello, inHelloLen);
    
    if ( ! okay || memcmp(YMPlexerMasterHello,inHello,inHelloLen) )
    {
        YMLog(errorTemplate,"master hello read failed");
        return false;
    }
    
    okay = YMSecurityProviderWrite(plexer->provider, (void *)YMPlexerSlaveHello, strlen(YMPlexerSlaveHello));
    if ( ! okay )
    {
        YMLog(errorTemplate,"slave hello write failed");
        return false;
    }
    
    YMPlexerMasterInitializer initializer;
    okay = YMSecurityProviderRead(plexer->provider, (void *)&initializer, sizeof(initializer));
    if ( ! okay )
    {
        YMLog(errorTemplate,"master init read failed");
        return false;
    }
    
    // todo, technically this should handle non-zero-based master min id, but doesn't
    plexer->localStreamIDMin = initializer.masterStreamIDMax + 1;
    plexer->localStreamIDMax = YMStreamIDMax;
    plexer->localStreamIDLast = plexer->localStreamIDMax;
    plexer->remoteStreamIDMin = initializer.masterStreamIDMin;
    plexer->remoteStreamIDMax = initializer.masterStreamIDMax;
    
    bool supported = initializer.protocolVersion <= YMPlexerBuiltInVersion;
    YMPlexerSlaveAck ack = { YMPlexerBuiltInVersion };
    okay = YMSecurityProviderWrite(plexer->provider, (void *)&ack, sizeof(ack));
    if ( ! okay )
    {
        YMLog(errorTemplate,"slave hello read error");
        return false;
    }
    
#pragma message "todo renegotiate"
    if ( ! supported )
    {
        YMLog(errorTemplate,"master requested protocol newer than built-in %lu",YMPlexerBuiltInVersion);
        return false;
    }
    
    return true;
}

void *_YMPlexerServiceDownstreamThread(void *context)
{
    struct __YMPlexer *plexer = (struct __YMPlexer *)context;
    YMLog("plexer[%s]: down service thread entered",plexer->master?"master":"slave");
    
    bool okay = true;
    while(okay && plexer->active)
    {
        YMLog("plexer[%s]: awaiting signal",plexer->master?"master":"slave");
        // there is only one thread consuming this semaphore, so i think it's ok not to actually lock around this loop iteration
        YMSemaphoreWait(plexer->localDataAvailableSemaphore);
        
        int readyStreams = 0;
        YMStreamRef servicingStream = _YMPlexerChooseDownstream(plexer, &readyStreams);
        
        YMLog("plexer[%s]: downstream signaled, %d streams ready",plexer->master?"master":"slave",readyStreams);
        
#pragma message "todo, choose should probably return an ordered list, because this is awkward at this level"
        //while ( --readyStreams )
        {
            if ( ! servicingStream )
            {
                YMLog("plexer[%s]: warning: service downstream signaled but couldn't find a stream",plexer->master?"master":"slave");
                continue;
            }
            
            bool okay = _YMPlexerServiceDownstream(plexer, servicingStream);
            if ( ! okay )
            {
                YMLog("plexer[%s]: fatal: service downstream failed",plexer->master?"master":"slave");
                _YMPlexerInterrupt(plexer);
                okay = false;
            }
        }
    }
    
#pragma message "todo free user info of deallocated streams somewhere"
    YMLog("plexer[%s]: down service thread exiting",plexer->master?"master":"slave");
    return NULL;
}

YMStreamRef _YMPlexerChooseDownstream(YMPlexerRef plexer, int *outReadyStreams)
{
    int readyStreams = 0;
    YMStreamRef servicingStream = NULL;
    struct timeval newestTime;
    YMSetTheEndOfPosixTimeForCurrentPlatform(&newestTime);
    YMLockLock(plexer->localAccessLock);
    {
        YMDictionaryEnumRef localStreamsEnum = YMDictionaryEnumeratorBegin(plexer->localStreamsByID);
        while ( localStreamsEnum )
        {
            YMStreamRef aLocalStream = (YMStreamRef)localStreamsEnum->value;
            YMStreamID aLocalStreamID = _YMStreamGetUserInfo(aLocalStream)->streamID;
            int downRead = _YMStreamGetDownwardRead(aLocalStream);
            
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(downRead,&fdset);
            // a zero'd timeval struct indicates a poll, which is what i think we want here
            // if something goes "wrong" (todo) with one stream, don't starve the others
            struct timeval timeout = { 0, 0 };
#pragma message "todo unsure of this, but select(all) and note how many times this loop should iterate before sleeping again?"
            int nReadyFds = select(downRead + 1, &fdset, NULL, NULL, &timeout);
            
            if ( nReadyFds <= 0 ) // zero is a timeout
            {
                if ( nReadyFds == -1 )
                    YMLog("plexer[%s,s%u]: warning: select failed %d (%s)",plexer->master?"master":"slave",aLocalStreamID,errno,strerror(errno));
                continue;
            }
            
            readyStreams ++;
            
            struct timeval *thisStreamLastService = _YMStreamGetLastServiceTime(aLocalStream);
            if ( YMTimevalCompare(thisStreamLastService, &newestTime ) != GreaterThan )
                servicingStream = aLocalStream;
            
            localStreamsEnum = YMDictionaryEnumeratorGetNext(plexer->localStreamsByID, localStreamsEnum);
        }
    }
    YMLockUnlock(plexer->localAccessLock);
    
    if ( outReadyStreams )
        *outReadyStreams = readyStreams;
    
    return servicingStream;
}

bool _YMPlexerServiceDownstream(YMPlexerRef plexer, YMStreamRef servicingStream)
{
    YMStreamID streamID = _YMStreamGetUserInfo(servicingStream)->streamID;
#pragma message "todo go through all of these logs and print fd before stream id where applicable"
    YMLog("plexer[%s,%u]: chose stream for service",plexer->master?"master":"slave",streamID);
    
    // update last service time on stream
    _YMStreamSetLastServiceTimeNow(servicingStream);
    
    YMStreamChunkHeader streamHeader;
    bool okay = YMStreamReadDown(servicingStream, (void *)&streamHeader, sizeof(streamHeader));
    if ( ! okay )
    {
        YMLog("plexer[%s,%u]: error: failed reading down stream header: %d (%s)",plexer->master?"master":"slave",streamID,errno,strerror(errno));
        return false;
    }
    
    YMLog("plexer[%s,%u]: servicing down stream chunk size %u",plexer->master?"master":"slave",streamID,streamHeader.length);
    
    while ( streamHeader.length > plexer->localPlexBufferSize )
    {
        plexer->localPlexBufferSize *= 2;
        YMLog("plexer[%s,%u] reallocating down plex buffer to %u",plexer->master?"master":"slave",streamID,plexer->localPlexBufferSize);
        plexer->localPlexBuffer = realloc(plexer->localPlexBuffer, plexer->localPlexBufferSize);
    }
    
    okay = YMStreamReadDown(servicingStream, plexer->localPlexBuffer, streamHeader.length);
    if ( ! okay )
    {
        YMLog("plexer[%s,%u]: error: reading down stream chunk size %u: %d (%s)",plexer->master?"master":"slave", streamID,streamHeader.length,errno,strerror(errno));
        return false;
    }
    
    YMLog("plexer[%s,%u]: read stream chunk for stream",plexer->master?"master":"slave",streamID);
    
#pragma message "todo add hton ntoh to stuff across the wire"
    YMPlexerChunkHeader plexHeader = { streamID, streamHeader.length };
    
    okay = YMWriteFull(plexer->inFd, (void *)&plexHeader, sizeof(plexHeader));
    if ( ! okay )
    {
        YMLog("plexer[%s,%u]: error: failed writing down plex header size %u: %d (%s)",plexer->master?"master":"slave",streamID,plexHeader.length,errno,strerror(errno));
        return false;
    }
    
    YMLog("plexer[%s,%u]: wrote down plex header",plexer->master?"master":"slave",streamID);
    
    okay = YMWriteFull(plexer->inFd, plexer->localPlexBuffer, streamHeader.length);
    if ( ! okay )
    {
        YMLog("plexer[%s,%u]: error: failed writing down plex chunk size %u: %d (%s)",plexer->master?"master":"slave",streamID,plexHeader.length,errno,strerror(errno));
        return false;
    }
    
    YMLog("plexer[%s,%u]: wrote plex chunk size %u",plexer->master?"master":"slave",streamID,plexHeader.length);
    return true;
}

void *_YMPlexerServiceUpstreamThread(void *context)
{
    YMPlexerRef plexer = (YMPlexerRef)context;
    YMLog("plexer[%s,^]: remote service thread entered",plexer->master?"master":"slave");
    
    bool okay = true;
    while ( okay && plexer->active )
    {
        YMPlexerChunkHeader header;
        okay = YMReadFull(plexer->outFd, (void *)&header, sizeof(header));
        if ( ! okay )
        {
            YMLog("plexer[%s,^,%d]: fatal: failed reading plex header",plexer->master?"master":"slave",plexer->outFd);
            _YMPlexerInterrupt(plexer);
            break;
        }
        
        YMLog("plexer[%s,^,%d,%d] read plex header with length %u",plexer->master?"master":"slave",plexer->outFd,header.streamID,header.length);
        
        YMStreamRef theStream = _YMPlexerGetOrCreateStreamWithID(plexer, header.streamID);
        if ( ! theStream )
        {
            YMLog("plexer[%s,^,%d,%u] failed to look up stream",plexer->master?"master":"slave",plexer->outFd,header.streamID);
            abort();
        }
        
        while ( header.length > plexer->remotePlexBufferSize )
        {
            plexer->remotePlexBufferSize *= 2;
            YMLog("plexer[%s,^,%d,%u] reallocating plex buffer to %u",plexer->master?"master":"slave",plexer->outFd,header.streamID,plexer->remotePlexBufferSize);
            plexer->remotePlexBuffer = realloc(plexer->remotePlexBuffer, plexer->remotePlexBufferSize);
        }
        
        okay = YMReadFull(plexer->outFd, plexer->remotePlexBuffer, header.length);
        if ( ! okay )
        {
            YMLog("plexer[%s,^,%d,%u]: fatal: failed reading plex buffer of length %u",plexer->master?"master":"slave",plexer->outFd,header.streamID,header.length);
            _YMPlexerInterrupt(plexer);
            goto catch_break;
        }
        YMLog("plexer[%s,^,%d,%u] read plex header length %u",plexer->master?"master":"slave",plexer->outFd,header.streamID,header.length);
        
        okay = YMWriteFull(plexer->inFd, plexer->remotePlexBuffer, header.length);
        if ( ! okay )
        {
            YMLog("plexer[%s,^,%d,%u]: fatal: failed writing plex buffer of length %u",plexer->master?"master":"slave",plexer->outFd,header.streamID,header.length);
            _YMPlexerInterrupt(plexer);
            goto catch_break;
        }
        
    catch_break:
        ;
    }
    
#pragma message "todo free user info of deallocated streams somewhere"
    YMLog("plexer[%s,^]: remote service thread exiting",plexer->master?"master":"slave");
    return NULL;
}

#pragma message "todo clean these smattered things up AND all the structs at the top of YMThread.c"
typedef struct __ym_notify_new_stream_def
{
    YMPlexerRef plexer;
    YMStreamRef stream;
} _ym_notify_new_stream_def;

void *ym_notify_new_stream(void *context)
{
    YMThreadDispatchUserInfoRef userDispatch = context;
    _ym_notify_new_stream_def *notifyDef = (_ym_notify_new_stream_def *)userDispatch->context;
    YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    YMLog("user[%s,%u] ym_notify_new_stream entered", plexer->master?"master":"slave", streamID);
    plexer->newIncomingFunc(plexer,stream);
    YMLog("user[%s,%u] ym_notify_new_stream exiting", plexer->master?"master":"slave", streamID);
    
    return NULL;
}

YMStreamRef _YMPlexerGetOrCreateStreamWithID(YMPlexerRef plexer, YMStreamID streamID)
{
    YMStreamRef theStream = NULL;
    
    YMLockLock(plexer->remoteAccessLock);
    {
        YMDictionaryEnumRef enumerator = YMDictionaryEnumeratorBegin(plexer->remoteStreamsByID);
        while ( enumerator )
        {
            if ( enumerator->key == streamID )
            {
                theStream = (YMStreamRef)enumerator->value;
                YMLog("plexer[%s,%u]: found existing remote stream",plexer->master?"master":"slave",streamID);
                YMDictionaryEnumeratorEnd(plexer->remoteStreamsByID, enumerator);
                break;
            }
        }
        
        // new stream
#pragma message "todo find a way to optimize passing of ownership for these cases?"
        
        YMLog("plexer[%s,%u]: notifying new remote stream",plexer->master?"master":"slave",streamID);
        char *streamName = YMStringCreateWithFormat("upstream-%u", streamID);        
        theStream = YMStreamCreate(streamName, false);
        free(streamName);
        YMStreamUserInfoRef userInfo = (YMStreamUserInfoRef)malloc(sizeof(struct __YMStreamUserInfo));
        userInfo->streamID = streamID;
        _YMStreamSetUserInfo(theStream, userInfo);
        
        _ym_notify_new_stream_def *notifyDef = (_ym_notify_new_stream_def *)malloc(sizeof(_ym_notify_new_stream_def));
        notifyDef->plexer = plexer;
        notifyDef->stream = theStream;
        
        YMThreadDispatchUserInfo userDispatch = { ym_notify_new_stream, notifyDef, true, NULL, YMStringCreateWithFormat("notify-new-stream-%u",streamID) };
        YMThreadDispatchDispatch(plexer->eventDeliveryThread, &userDispatch);
    }
    YMLockUnlock(plexer->remoteAccessLock);
    
    return theStream;
}

void _YMPlexerInterrupt(YMPlexerRef plexer)
{
#pragma message "todo"
    YMLog("plexer[%s]: *** _YMPlexerInterrupt, todo",plexer->master?"master":"slave");
    close(plexer->inFd);
}

void YMPlexerStop(YMPlexerRef plexer)
{
    // deallocate volatile stuff
    
    free(plexer->localPlexBuffer);
    plexer->localPlexBuffer = NULL;
    
    free(plexer->remotePlexBuffer);
    plexer->remotePlexBuffer = NULL;
    
    plexer->active = false;
}

YMStreamRef YMPlexerCreateNewStream(YMPlexerRef plexer, char *name, bool direct)
{
    YMLockLock(plexer->localAccessLock);
    YMStreamID newStreamID = ( plexer->localStreamIDLast == plexer->localStreamIDMax ) ? plexer->localStreamIDMin : ++(plexer->localStreamIDLast);
    YMStreamRef newStream = YMStreamCreate(name, true);
    YMStreamUserInfoRef userInfo = (YMStreamUserInfoRef)malloc(sizeof(struct __YMStreamUserInfo));
    userInfo->streamID = newStreamID;
    _YMStreamSetUserInfo(newStream, userInfo);
    _YMStreamSetDataAvailableSemaphore(newStream, plexer->localDataAvailableSemaphore);
    if ( YMDictionaryContains(plexer->localStreamsByID, newStreamID) )
    {
        YMLog("fatal: YMPlexer has run out of streams");
        abort();
    }
    YMDictionaryAdd(plexer->localStreamsByID, newStreamID, newStream);
    YMLockUnlock(plexer->localAccessLock);
    
#pragma message "todo fcntl direct."
    
    return newStream;
}

bool YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream)
{
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    
    YMStreamRef localStream;
    YMLockLock(plexer->localAccessLock);
    {
        localStream = (YMStreamRef)YMDictionaryRemove(plexer->localStreamsByID, streamID);
    }
    YMLockUnlock(plexer->localAccessLock);
    
    if ( localStream == NULL )
    {
        bool isRemote;
        YMLockLock(plexer->remoteAccessLock);
        {
            isRemote = YMDictionaryContains(plexer->remoteStreamsByID, streamID);
        }
        YMLockUnlock(plexer->remoteAccessLock);
        
        if ( isRemote )
            YMLog("error: YMPlexer user requested closure of remote stream %u",streamID);
        else
            YMLog("error: YMPlexer user requested closure of unknown stream %u",streamID);
        return false;
    }
    
    YMLog("local stream %u marked closed...",streamID);
    YMStreamClose(localStream);
    // local service thread to deallocate after it's able to flush data and pass off the close command to remote
    
    return true;
}
