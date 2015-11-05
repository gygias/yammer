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
#include "YMThreads.h"

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
void *_YMPlexerServiceDownstreamThread(void *context);
void *_YMPlexerServiceUpstreamThread(void *context);
YMStreamRef _YMPlexerChooseDownstream(YMPlexerRef plexer);
void _YMPlexerInterrupt();

typedef struct __YMPlexer
{
    YMTypeID _typeID;
    
    int inFd;
    int outFd;
    char *name;
    bool initialized;
    bool master;
    bool running;
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
    YMLockRef interruptionLock;
    YMSemaphoreRef localDataAvailableSemaphore;
    
    // user
    ym_plexer_interrupted_func interruptedFunc;
    ym_plexer_new_upstream_func newIncomingFunc;
    ym_plexer_stream_closing_func closingFunc;
} _YMPlexer;

#define YMPlexerDefaultBufferSize (1e+6)

YMPlexerRef YMPlexerCreateWithFullDuplexFile(int fd)
{
    return YMPlexerCreate(fd, fd);
}

YMPlexerRef YMPlexerCreate(int inFd, int outFd)
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
    
    plexer->localServiceThread = YMThreadCreate(_YMPlexerServiceDownstreamThread, plexer);
    plexer->remoteServiceThread = YMThreadCreate(_YMPlexerServiceUpstreamThread, plexer);
    plexer->interruptionLock = YMLockCreate();
    plexer->localDataAvailableSemaphore = YMSemaphoreCreate();
    
    plexer->inFd = inFd;
    plexer->outFd = outFd;
    plexer->initialized = false;
    plexer->running = false;
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
        YMLog("warning: %s: provider is type '%c'",__FUNCTION__,type);
    plexer->provider = (YMSecurityProviderRef)provider;
}

const char* YMPlexerMasterHello = "hola";
const char* YMPlexerSlaveHello = "greetings";
bool YMPlexerStart(YMPlexerRef plexer, bool master)
{
    bool okay = _YMPlexerDoInitialization(plexer,master);
    
    if ( ! okay )
        goto catch_fail;
    
    okay = YMThreadStart(plexer->localServiceThread);
    
    if ( ! okay )
        goto catch_fail;
    
    okay = YMThreadStart(plexer->remoteServiceThread);
    
    if ( ! okay )
        goto catch_fail;
    
catch_fail:
    return okay;
}

bool _YMPlexerDoInitialization(YMPlexerRef plexer, bool master)
{
    bool okay = false;
    
    if ( plexer->initialized )
    {
        YMLog("error: this plexer is already initialized");
        return false;
    }
    
    char *error = "error: plexer initialization failed";
    if ( master )
    {
        okay = YMSecurityProviderWrite(plexer->provider, (void *)YMPlexerMasterHello, strlen(YMPlexerMasterHello));
        if ( ! okay )
        {
            YMLog(error);
            return false;
        }
        
        unsigned long inHelloLen = strlen(YMPlexerSlaveHello);
        char inHello[inHelloLen];
        okay = YMSecurityProviderRead(plexer->provider, (void *)inHello, inHelloLen);
        if ( ! okay || memcmp(YMPlexerSlaveHello,inHello,inHelloLen) )
        {
            YMLog(error);
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
            YMLog(error);
            return false;
        }
        
        YMPlexerSlaveAck ack;
        okay = YMSecurityProviderRead(plexer->provider, (void *)&ack, sizeof(ack));
        if ( ! okay )
        {
            YMLog(error);
            return false;
        }
        if ( ack.protocolVersion > YMPlexerBuiltInVersion )
        {
            YMLog("error: slave requested unknown protocol");
            return false;
        }
    }
    else
    {
        unsigned long inHelloLen = strlen(YMPlexerMasterHello);
        char inHello[inHelloLen];
        okay = YMSecurityProviderRead(plexer->provider, (void *)inHello, inHelloLen);
        
        if ( ! okay || memcmp(YMPlexerMasterHello,inHello,inHelloLen) )
        {
            YMLog(error);
            return false;
        }
        
        okay = YMSecurityProviderWrite(plexer->provider, (void *)YMPlexerSlaveHello, strlen(YMPlexerSlaveHello));
        if ( ! okay )
        {
            YMLog(error);
            return false;
        }
        
        YMPlexerMasterInitializer initializer;
        okay = YMSecurityProviderRead(plexer->provider, (void *)&initializer, sizeof(initializer));
        if ( ! okay )
        {
            YMLog(error);
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
            YMLog(error);
            return false;
        }
        
#warning todo renegotiate
        if ( ! supported )
        {
            YMLog("error: master requested protocol newer than built-in %lu",YMPlexerBuiltInVersion);
            return false;
        }
    }
    
    YMLog("YMPlexer initialized as %s, m[%llu:%llu], s[%llu:%llu]", master?"master":"slave",
          master ? plexer->localStreamIDMin : plexer->remoteStreamIDMin,
          master ? plexer->localStreamIDMax : plexer->remoteStreamIDMax,
          master ? plexer->remoteStreamIDMin : plexer->localStreamIDMin,
          master ? plexer->remoteStreamIDMax : plexer->localStreamIDMax);
    
    plexer->initialized = true; // todo maybe redundant
    plexer->master = master;
    plexer->running = true;
    
    return true;
}

void *_YMPlexerServiceDownstreamThread(void *context)
{
    struct __YMPlexer *plexer = (struct __YMPlexer *)context;
    YMLog("plexer (%s): entered its local service thread",plexer->master?"master":"slave");
    while(1)
    {
        YMLog("plexer (%s): awaiting signal",plexer->master?"master":"slave");
        // there is only one thread consuming this semaphore, so i think it's ok not to actually lock around this loop iteration
        YMSemaphoreWait(plexer->localDataAvailableSemaphore);
        YMLog("plexer (%s): downstream signalled",plexer->master?"master":"slave");
        
        YMStreamRef servicingStream = _YMPlexerChooseDownstream(plexer);
        if ( ! servicingStream )
        {
            YMLog("warning: service downstream signaled but couldn't find a stream");
            continue;
        }
        
        YMStreamID streamID = _YMStreamGetUserInfo(servicingStream)->streamID;
        YMLog("plexer (%s,%lu): chose stream for service",plexer->master?"master":"slave",streamID);
        
        // update last service time on stream
        _YMStreamSetLastServiceTimeNow(servicingStream);
        
        YMStreamChunkHeader streamHeader;
        bool okay = YMStreamReadDown(servicingStream, (void *)&streamHeader, sizeof(streamHeader));
        if ( ! okay )
        {
            YMLog("plexer (%s,%lu): fatal: failed reading down stream header",plexer->master?"master":"slave",streamID);
            _YMPlexerInterrupt();
            return NULL;
        }
        
        YMLog("plexer (%s,%lu): servicing down stream chunk size %lu",plexer->master?"master":"slave",streamID,streamHeader.length);
        
        while ( streamHeader.length > plexer->localPlexBufferSize )
        {
            plexer->localPlexBufferSize *= 2;
            YMLog("plexer (%s,%lu) reallocating local plex buffer to %llu",plexer->master?"master":"slave",streamID,plexer->localPlexBufferSize);
            plexer->localPlexBuffer = realloc(plexer->localPlexBuffer, plexer->localPlexBufferSize);
        }
        
        okay = YMStreamReadDown(servicingStream, plexer->localPlexBuffer, plexer->localPlexBufferSize);
        if ( ! okay )
        {
            YMLog("plexer (%s,%lu): fatal: reading down stream chunk size %lu",plexer->master?"master":"slave", streamID,streamHeader.length);
            _YMPlexerInterrupt();
            return NULL;
        }
        
        YMLog("plexer (%s,%lu): read stream chunk for stream",plexer->master?"master":"slave",streamID);
        
#warning add hton ntoh to stuff across the wire
        YMPlexerChunkHeader plexHeader = { streamID, streamHeader.length };
        
        okay = YMWriteFull(plexer->outFd, (void *)&plexHeader, sizeof(plexHeader));
        if ( ! okay )
        {
            YMLog("plexer (%s,%lu): fatal: failed writing down plex header size %lu",plexer->master?"master":"slave",streamID,plexHeader.length);
            _YMPlexerInterrupt();
            return NULL;
        }
        
        YMLog("plexer (%s,%lu): wrote down plex header",plexer->master?"master":"slave",streamID);
        
        okay = YMWriteFull(plexer->outFd, plexer->localPlexBuffer, plexer->localPlexBufferSize);
        if ( ! okay )
        {
            YMLog("plexer (%s,%lu): fatal: failed writing down plex chunk size %lu",plexer->master?"master":"slave",streamID,plexHeader.length);
            _YMPlexerInterrupt();
            return NULL;
        }
        
        YMLog("plexer (%s,%lu): wrote plex chunk size %lu",plexer->master?"master":"slave",streamID,plexHeader.length);
    }
    
#warning todo free user info of deallocated streams somewhere
    return NULL;
}

YMStreamRef _YMPlexerChooseDownstream(YMPlexerRef plexer)
{
    YMStreamRef servicingStream = NULL;
    struct timeval newestTime;
    YMSetTheEndOfPosixTimeForCurrentPlatform(&newestTime);
    YMLockLock(plexer->localAccessLock);
    {
        YMDictionaryEnumRef localStreamsEnum = YMDictionaryEnumeratorBegin(plexer->localStreamsByID);
        while ( localStreamsEnum )
        {
            YMStreamRef aLocalStream = (YMStreamRef)localStreamsEnum->value;
            int downRead = _YMStreamGetDownwardRead(aLocalStream);
            
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(downRead,&fdset);
            // a zero'd timeval struct indicates a poll, which is what i think we want here
            // if something goes "wrong" (todo) with one stream, don't starve the others
            struct timeval timeout = { 0, 0 };
            int nReadyFds = select(downRead + 1, &fdset, NULL, NULL, &timeout);
            
            if ( nReadyFds <= 0 ) // zero is a timeout
            {
                if ( nReadyFds == -1 )
                    YMLog("warning: select(stream %lu) failed %d (%s)",errno,strerror(errno));
                continue;
            }
            
            struct timeval *thisStreamLastService = _YMStreamGetLastServiceTime(aLocalStream);
            if ( YMTimevalCompare(thisStreamLastService, &newestTime ) != GreaterThan )
                servicingStream = aLocalStream;
            
            localStreamsEnum = YMDictionaryEnumeratorGetNext(plexer->localStreamsByID, localStreamsEnum);
        }
    }
    YMLockUnlock(plexer->localAccessLock);
    
    return servicingStream;
}

void *_YMPlexerServiceUpstreamThread(void *context)
{
    YMPlexerRef plexer = (YMPlexerRef)context;
    YMLog("plexer (%s) has entered its remote service thread and is going to sleep",plexer->master?"master":"slave");
    sleep(1e+6);//todo receive signal from 'underlying medium'
#warning todo free user info of deallocated streams somewhere
    return NULL;
}

void _YMPlexerInterrupt()
{
#warning todo
}

void YMPlexerStop(YMPlexerRef plexer)
{
    // deallocate volatile stuff
    
    free(plexer->localPlexBuffer);
    plexer->localPlexBuffer = NULL;
    
    free(plexer->remotePlexBuffer);
    plexer->remotePlexBuffer = NULL;
    
    plexer->running = false;
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
    
#warning todo fcntl direct.
    
    return newStream;
}

bool YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream)
{
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    
    YMStreamRef localStream;
    YMLockLock(plexer->localAccessLock);
    {
        localStream = YMDictionaryRemove(plexer->localStreamsByID, streamID);
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
            YMLog("error: YMPlexer user requested closure of remote stream %llu",streamID);
        else
            YMLog("error: YMPlexer user requested closure of unknown stream %llu",streamID);
        return false;
    }
    
    YMLog("local stream %llu marked closed...",streamID);
    YMStreamClose(localStream);
    // local service thread to deallocate after it's able to flush data and pass off the close command to remote
    
    return true;
}
