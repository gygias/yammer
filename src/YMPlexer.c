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

#include <pthread.h> // explicit for sigpipe

#define YMPlexerBuiltInVersion ((uint32_t)1)

// initialization
typedef struct {
    uint32_t protocolVersion;
    uint32_t masterStreamIDMin;
    uint32_t masterStreamIDMax;
} YMPlexerMasterInitializer;

typedef struct {
    uint32_t protocolVersion;
} YMPlexerSlaveAck;

// multiplexing
typedef int32_t YMPlexerCommandType;
typedef enum YMPlexerCommandType
{
    YMPlexerCommandCloseStream = -1
} YMPlexerCommand;

typedef struct __ym_notify_plexer_def
{
    YMPlexerRef plexer;
} _ym_notify_plexer_def;

typedef struct __ym_notify_plexer_stream_def
{
    YMPlexerRef plexer;
    YMStreamRef stream;
} _ym_notify_plexer_stream_def;

void *ym_notify_new_stream(void *context);
void *ym_notify_stream_closing(void *context);

typedef struct {
    YMPlexerCommand command;
    YMStreamID streamID;
} YMPlexerMessage;

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
    
    char *name;
    YMSecurityProviderRef provider;
    
    int inputFile;
    int outputFile;
    bool active; // intialized and happy
    bool master;
    
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
    YMSemaphoreRef streamMessageSemaphore;
    
    // user
    ym_plexer_interrupted_func interruptedFunc;
    ym_plexer_new_upstream_func newIncomingFunc;
    ym_plexer_stream_closing_func closingFunc;
} _YMPlexer;

#define YMPlexerDefaultBufferSize (1e+6)

pthread_once_t gYMRegisterSigpipeOnce = PTHREAD_ONCE_INIT;
void sigpipe_handler (__unused int signum)
{
    YMLog("sigpipe happened");
    abort();
}

void _YMRegisterSigpipe()
{
    signal(SIGPIPE,sigpipe_handler);
}

YMPlexerRef YMPlexerCreateWithFullDuplexFile(char *name, int file, bool master)
{
    return YMPlexerCreate(name, file, file, master);
}

YMPlexerRef YMPlexerCreate(char *name, int inputFile, int outputFile, bool master)
{
    pthread_once(&gYMRegisterSigpipeOnce, _YMRegisterSigpipe);
    
    _YMPlexer *plexer = (_YMPlexer *)calloc(1,sizeof(_YMPlexer));
    plexer->_typeID = _YMPlexerTypeID;
    
    char *memberName = YMStringCreateWithFormat("%s(%s)",name?name:"unnamed",master?"m":"s");
    plexer->name = strdup(memberName);
    free(memberName);
    
    plexer->provider = YMSecurityProviderCreate(inputFile, outputFile);
    
    plexer->localStreamsByID = YMDictionaryCreate();
    plexer->localAccessLock = YMLockCreate();
    plexer->localPlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->localPlexBuffer = YMMALLOC(plexer->localPlexBufferSize);
    
    plexer->remoteStreamsByID = YMDictionaryCreate();
    plexer->remoteAccessLock = YMLockCreate();
    plexer->remotePlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->remotePlexBuffer = YMMALLOC(plexer->remotePlexBufferSize);
    
    memberName = YMStringCreateWithFormat("plex-%s-down",plexer->name);
    plexer->localServiceThread = YMThreadCreate(memberName, _YMPlexerServiceDownstreamThread, plexer);
    free(memberName);
    
    memberName = YMStringCreateWithFormat("plex-%s-up",plexer->name);
    plexer->remoteServiceThread = YMThreadCreate(memberName, _YMPlexerServiceUpstreamThread, plexer);
    free(memberName);
    
    memberName = YMStringCreateWithFormat("plex-%s-event",plexer->name);
    plexer->eventDeliveryThread = YMThreadDispatchCreate(memberName);
    free(memberName);
    
    plexer->interruptionLock = YMLockCreate();
    
    memberName = YMStringCreateWithFormat("plex-%s-signal",plexer->name);
    plexer->streamMessageSemaphore = YMSemaphoreCreate(memberName,0);
    free(memberName);
    
    plexer->inputFile = inputFile;
    plexer->outputFile = outputFile;
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
        YMLog(" plexer[%s]: warning: %s: provider is type '%c'",plexer->name,__FUNCTION__,type);
    plexer->provider = (YMSecurityProviderRef)provider;
}

bool YMPlexerStart(YMPlexerRef plexer)
{
    bool okay;
    
    if ( plexer->active )
    {
        YMLog(" plexer[%s]: user error: this plexer is already initialized",plexer->name);
        return false;
    }
    
    if ( plexer->master )
        okay = _YMPlexerInitAsMaster(plexer);
    else
        okay = _YMPlexerInitAsSlave(plexer);
    
    if ( ! okay )
        goto catch_fail;
    
    YMLog(" plexer[%s]: initialized m[%u:%u], s[%u:%u]",plexer->name,
          plexer->master ? plexer->localStreamIDMin : plexer->remoteStreamIDMin,
          plexer->master ? plexer->localStreamIDMax : plexer->remoteStreamIDMax,
          plexer->master ? plexer->remoteStreamIDMin : plexer->localStreamIDMin,
          plexer->master ? plexer->remoteStreamIDMax : plexer->localStreamIDMax);
    
    // this flag is used to let our threads exit, among other things
    plexer->active = true;
    
    okay = YMThreadStart(plexer->localServiceThread);
    if ( ! okay )
    {
        YMLog(" plexer[%s]: error: failed to detach down service thread",plexer->name);
        goto catch_fail;
    }
    
    okay = YMThreadStart(plexer->remoteServiceThread);
    if ( ! okay )
    {
        YMLog(" plexer[%s]: error: failed to detach up service thread",plexer->name);
        goto catch_fail;
    }
    
    okay = YMThreadStart(plexer->eventDeliveryThread);
    if ( ! okay )
    {
        YMLog(" plexer[%s]: error: failed to detach event thread",plexer->name);
        goto catch_fail;
    }
    
    YMLog(" plexer[%s,i%d-o%d]: started",plexer->name,plexer->inputFile,plexer->outputFile);
    
catch_fail:
    return okay;
}

const char *YMPlexerMasterHello = "オス、王様でおるべし";
const char *YMPlexerSlaveHello = "よろしくお願いいたします";

bool _YMPlexerInitAsMaster(YMPlexerRef plexer)
{
    char *errorTemplate = " plexer[m]: error: plexer init failed: %s";
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
    char *errorTemplate = " plexer[s]: error: plexer init failed: %s";
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
    YMLog(" plexer[%s]: down service thread entered",plexer->name);
    
    bool okay = true;
    while(okay && plexer->active)
    {
        YMLog(" plexer[%s-V]: awaiting signal",plexer->name);
        // there is only one thread consuming this semaphore, so i think it's ok not to actually lock around this loop iteration
        YMSemaphoreWait(plexer->streamMessageSemaphore);
        
        int readyStreams = 0;
        YMStreamRef servicingStream = _YMPlexerChooseDownstream(plexer, &readyStreams);
        
        YMLog(" plexer[%s-V]: signaled, %d streams ready",plexer->name,readyStreams);
        
        //while ( --readyStreams )
        {
            if ( ! servicingStream )
            {
                YMLog(" plexer[%s-V]: warning: signaled but couldn't find a stream",plexer->name);
                continue;
            }
            
            okay = _YMPlexerServiceDownstream(plexer, servicingStream);
            if ( ! okay )
            {
                YMLog(" plexer[%s-V]: fatal: service downstream failed",plexer->name);
                _YMPlexerInterrupt(plexer);
                okay = false;
            }
        }
    }
    
    YMLog(" plexer[%s]: down service thread exiting",plexer->name);
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
            
            int nReadyFds = select(downRead + 1, &fdset, NULL, NULL, &timeout);
            
            if ( nReadyFds <= 0 ) // zero is a timeout
            {
                if ( nReadyFds == -1 )
                {
                    YMLog(" plexer[%s-V,s%u]: fatal: select failed %d (%s)",plexer->name,aLocalStreamID,errno,strerror(errno));
                    abort();
                }
                goto catch_continue;
            }
            
            readyStreams ++;
            
            struct timeval *thisStreamLastService = _YMStreamGetLastServiceTime(aLocalStream);
            if ( YMTimevalCompare(thisStreamLastService, &newestTime ) != GreaterThan )
                servicingStream = aLocalStream;
        catch_continue:
            localStreamsEnum = YMDictionaryEnumeratorGetNext(localStreamsEnum);
        }
        YMDictionaryEnumeratorEnd(localStreamsEnum);
    }
    YMLockUnlock(plexer->localAccessLock);
    
    if ( outReadyStreams )
        *outReadyStreams = readyStreams;
    
    return servicingStream;
}

bool _YMPlexerServiceDownstream(YMPlexerRef plexer, YMStreamRef servicingStream)
{
    YMStreamID streamID = _YMStreamGetUserInfo(servicingStream)->streamID;
    YMLog(" plexer[%s-V,V]: chose stream %u for service",plexer->name,streamID);
    
    // update last service time on stream
    _YMStreamSetLastServiceTimeNow(servicingStream);
    
    YMStreamCommand streamCommand;

    uint32_t chunkLength = 0;
    bool closing = false;
    _YMStreamReadDown(servicingStream, (void *)&streamCommand, sizeof(streamCommand));
    if ( streamCommand.command <= 0 ) // handle command
    {
        if ( streamCommand.command == YMStreamClose )
        {
            YMLog(" plexer[%s-V,s%u]: servicing stream chunk %u",plexer->name,streamID, chunkLength);
            closing = true;
        }
        else
        {
            YMLog(" plexer[%s-V,s%u]: fatal: invalid command: %d",plexer->name,streamID,streamCommand.command);
            abort();
        }
    }
    else
    {
        chunkLength = streamCommand.command;
        YMLog(" plexer[%s-V,s%u]: servicing stream chunk %u",plexer->name,streamID, chunkLength);
    }
    
    while ( chunkLength > plexer->localPlexBufferSize )
    {
        plexer->localPlexBufferSize *= 2;
        YMLog(" plexer[%s-V,s%u] REALLOCATING plex buffer to %u",plexer->name,streamID,plexer->localPlexBufferSize);
        plexer->localPlexBuffer = realloc(plexer->localPlexBuffer, plexer->localPlexBufferSize);
    }
    
//okay =
    _YMStreamReadDown(servicingStream, plexer->localPlexBuffer, chunkLength);
//    if ( ! okay )
//    {
//        YMLog(" plexer[%s,V,s%u]: fatal: reading stream chunk size %u: %d (%s)",plexer->name, streamID,streamHeader.length,errno,strerror(errno));
//        abort();
//    }
    
    YMLog(" plexer[%s-V,s%u]: read stream chunk",plexer->name,streamID);
    
    YMPlexerMessage plexMessage = {closing ? YMPlexerCommandCloseStream : chunkLength, streamID };
    size_t plexMessageLen = sizeof(plexMessage);
    YMIOResult ioResult = YMWriteFull(plexer->outputFile, (void *)&plexMessage, plexMessageLen);
    if ( ioResult != YMIOSuccess )
    {
        YMLog(" plexer[%s-V,o%d!,s%u]: perror: failed writing plex message size %zu: %d (%s)",plexer->name,plexer->outputFile,streamID,plexMessageLen,errno,strerror(errno));
        return false;
    }
    
    if ( closing )
    {
        YMLog("******** TODO CLEAN UP LOCAL SHIT ******* ");
        return true;
    }
    
    YMLog(" plexer[%s-V,o%d!,s%u]: wrote plex header",plexer->name,plexer->outputFile,streamID);
    
    ioResult = YMWriteFull(plexer->outputFile, plexer->localPlexBuffer, chunkLength);
    if ( ioResult != YMIOSuccess )
    {
        YMLog(" plexer[%s-V,o%d!,s%u]: perror: failed writing plex chunk size %u: %d (%s)",plexer->name,plexer->outputFile,streamID,plexMessage.command,errno,strerror(errno));
        return false;
    }
    
    YMLog(" plexer[%s-V,o%d!,s%u]: wrote plex chunk size %u",plexer->name,plexer->outputFile,streamID,plexMessage.command);
    return true;
}

void *_YMPlexerServiceUpstreamThread(void *context)
{
    YMPlexerRef plexer = (YMPlexerRef)context;
    YMLog(" plexer[%s-^]: remote service thread entered",plexer->name);
    
    YMIOResult result = YMIOSuccess;
    
    while ( ( result == YMIOSuccess ) && plexer->active )
    {
        bool streamClosing = false;
        size_t chunkLength = 0;
        YMPlexerMessage plexerMessage;
        
        result = YMReadFull(plexer->inputFile, (void *)&plexerMessage, sizeof(plexerMessage));
        if ( result != YMIOSuccess )
        {
            YMLog(" plexer[%s-^,i%d!]: perror: failed reading plex header: %d (%s)",plexer->name,plexer->inputFile,errno,strerror(errno));
            _YMPlexerInterrupt(plexer);
            break;
        }
        else if ( plexerMessage.command == YMPlexerCommandCloseStream )
        {
            YMLog(" plexer[%s-^,%d,i%d!] stream closing",plexer->name,plexer->inputFile,plexerMessage.streamID);
            streamClosing = true;
        }
        else
        {
            chunkLength = plexerMessage.command;
            YMLog(" plexer[%s-^,%d,i%d!] read plex header with length %zu",plexer->name,plexer->inputFile,plexerMessage.streamID,chunkLength);
        }
        
        YMStreamID streamID = plexerMessage.streamID;
        YMStreamRef theStream = _YMPlexerGetOrCreateStreamWithID(plexer, streamID);
        if ( ! theStream )
        {
            YMLog(" plexer[%s-^,i%d!,s%u]: fatal: stream lookup",plexer->name,plexer->inputFile,streamID);
            _YMPlexerInterrupt(plexer);
            abort();
            break;
        }
        
        if ( streamClosing )
        {
            _ym_notify_plexer_stream_def *notifyDef = (_ym_notify_plexer_stream_def *)YMMALLOC(sizeof(_ym_notify_plexer_stream_def));
            notifyDef->plexer = plexer;
            notifyDef->stream = theStream;
            char *memberName = YMStringCreateWithFormat("plex-%s-notify-closing-%u",plexer->name,streamID);
            YMThreadDispatchUserInfo userDispatch = { ym_notify_stream_closing, notifyDef, true, NULL, memberName };
            YMThreadDispatchDispatch(plexer->eventDeliveryThread, &userDispatch);
            
            _YMStreamCloseUp(theStream);
            continue;
        }
        
        while ( chunkLength > plexer->remotePlexBufferSize )
        {
            plexer->remotePlexBufferSize *= 2;
            YMLog(" plexer[%s-^,i%d!,s%u] REALLOCATING plex buffer for %u",plexer->name,plexer->inputFile,streamID,plexer->remotePlexBufferSize);
            plexer->remotePlexBuffer = realloc(plexer->remotePlexBuffer, plexer->remotePlexBufferSize);
        }
        
        result = YMReadFull(plexer->inputFile, plexer->remotePlexBuffer, chunkLength);
        if ( result != YMIOSuccess )
        {
            YMLog(" plexer[%s-^,i%d!,s%u]: perror: failed reading plex buffer of length %zu: %d (%s)",plexer->name,plexer->inputFile,streamID,chunkLength,errno,strerror(errno));
            _YMPlexerInterrupt(plexer);
            break;
        }
        YMLog(" plexer[%s-^,i%d!,s%u] read plex header length %zu",plexer->name,plexer->inputFile,streamID,chunkLength);
        
        int streamInFd = _YMStreamGetUpstreamWrite(theStream);
        result = YMWriteFull(streamInFd, plexer->remotePlexBuffer, chunkLength);
        if ( result != YMIOSuccess )
        {
            YMLog(" plexer[%s-^,%d!,s%u]: fatal: failed writing plex buffer of length %zu: %d (%s)",plexer->name,plexer->inputFile,streamID,chunkLength,errno,strerror(errno));
            _YMPlexerInterrupt(plexer);
            abort();
            break;
        }
    }
    
    YMLog(" plexer[%s-^]: remote service thread exiting",plexer->name);
    return NULL;
}

void *ym_notify_new_stream(void *context)
{
    YMThreadDispatchUserInfoRef userDispatch = context;
    _ym_notify_plexer_stream_def *notifyDef = (_ym_notify_plexer_stream_def *)userDispatch->context;
    YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    YMLog("user[%s,s%u] ym_notify_new_stream entered", plexer->name, streamID);
    plexer->newIncomingFunc(plexer,stream);
    YMLog("user[%s,s%u] ym_notify_new_stream exiting", plexer->name, streamID);
    
    return NULL;
}

void *ym_notify_stream_closing(void *context)
{
    YMThreadDispatchUserInfoRef userDispatch = context;
    _ym_notify_plexer_stream_def *notifyDef = (_ym_notify_plexer_stream_def *)userDispatch->context;
    YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    YMLog("user[%s,s%u] ym_notify_stream_closing entered", plexer->name, streamID);
    plexer->closingFunc(plexer,stream);
    YMLog("user[%s,s%u] ym_notify_stream_closing exiting", plexer->name, streamID);
    
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
                YMLog(" plexer[%s,s%u]: found existing remote stream",plexer->name,streamID);
                YMDictionaryEnumeratorEnd(enumerator);
                break;
            }
        }
        
        // new stream
        if ( ! theStream )
        {
            YMLog(" plexer[%s,s%u]: notifying new remote stream",plexer->name,streamID);
            
            YMStreamUserInfoRef userInfo = (YMStreamUserInfoRef)YMMALLOC(sizeof(struct __YMStreamUserInfo));
            userInfo->streamID = streamID;
            char *memberName = YMStringCreateWithFormat("plex-^-%u", streamID);
            theStream = YMStreamCreate(memberName, false, userInfo);
            free(memberName);
            
            YMDictionaryAdd(plexer->remoteStreamsByID, streamID, theStream);
            
            _ym_notify_plexer_stream_def *notifyDef = (_ym_notify_plexer_stream_def *)YMMALLOC(sizeof(_ym_notify_plexer_stream_def));
            notifyDef->plexer = plexer;
            notifyDef->stream = theStream;
            
            memberName = YMStringCreateWithFormat("plex-%s-notify-new-%u",plexer->name,streamID);
            YMThreadDispatchUserInfo userDispatch = { ym_notify_new_stream, notifyDef, true, NULL, memberName };
            YMThreadDispatchDispatch(plexer->eventDeliveryThread, &userDispatch);
        }
    }
    YMLockUnlock(plexer->remoteAccessLock);
    
    return theStream;
}

void _YMPlexerInterrupt(YMPlexerRef plexer)
{
    YMLog(" plexer[%s]: *** _YMPlexerInterrupt, todo",plexer->name);
    
    if ( ! plexer->active )
        abort();
    
    plexer->active = false;
    
    int result;
    result = close(plexer->inputFile);
    if ( result != 0 )
        abort();
    result = close(plexer->outputFile);
    if ( result != 0 )
        abort();
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

YMStreamRef YMPlexerCreateNewStream(YMPlexerRef plexer, const char *name, bool direct)
{
    YMStreamRef newStream = NULL;
    YMLockLock(plexer->localAccessLock);
    {
        plexer->localStreamIDLast = ( ++plexer->localStreamIDLast >= plexer->localStreamIDMax ) ? plexer->localStreamIDMin : plexer->localStreamIDLast;
        YMStreamID newStreamID = plexer->localStreamIDLast;
        
        YMStreamUserInfoRef userInfo = (YMStreamUserInfoRef)YMMALLOC(sizeof(struct __YMStreamUserInfo));
        userInfo->streamID = newStreamID;
        char *memberName = YMStringCreateWithFormat("plex-V-%u:%s",newStreamID,name);
        newStream = YMStreamCreate(memberName, true, userInfo);
        free(memberName);
        _YMStreamSetDataAvailableSemaphore(newStream, plexer->streamMessageSemaphore);
        if ( YMDictionaryContains(plexer->localStreamsByID, newStreamID) )
        {
            YMLog(" plexer[%s]: fatal: YMPlexer has run out of streams",plexer->name);
            abort();
        }
        YMDictionaryAdd(plexer->localStreamsByID, newStreamID, newStream);
        
        if ( direct )
        {
            // ???
        }
    }
    YMLockUnlock(plexer->localAccessLock);
    
    return newStream;
}

// void: if this fails, it's either a bug or user error
void YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream)
{
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    
    YMStreamRef localStream;
    YMLockLock(plexer->localAccessLock);
    {
        localStream = (YMStreamRef)YMDictionaryGetItem(plexer->localStreamsByID, streamID);
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
            YMLog(" plexer[%s]: error: user requested closure of remote stream %u",plexer->name,streamID);
        else
            YMLog(" plexer[%s]: error: user requested closure of unknown stream %u",plexer->name,streamID);
        
        abort();
    }
    
    _YMStreamClose(localStream);
    // local service thread to deallocate after it's able to flush data and pass off the close command to remote
}
