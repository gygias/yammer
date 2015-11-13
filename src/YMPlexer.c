//
//  YMPlexer.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPlexer.h"
#include "YMPrivate.h"
#include "YMUtilities.h"

#include "YMStreamPriv.h"
#include "YMSecurityProvider.h"
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMThread.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogPlexer
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#include <sys/select.h>

#ifdef USE_FTIME
#include <sys/timeb.h>
#error todo
#else
#include <sys/time.h>
#endif

#include <pthread.h> // explicit for sigpipe

#define YMPlexerBuiltInVersion ((uint32_t)1)

#undef ymlog_type
#define ymlog_type YMLogPlexer

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

typedef struct __ym_dispatch_plexer_stream_def
{
    YMPlexerRef plexer;
    YMStreamRef stream;
} _ym_dispatch_plexer_stream_def;
typedef struct __ym_dispatch_plexer_stream_def __ym_dispatch_plexer_and_stream;
typedef __ym_dispatch_plexer_and_stream *__ym_dispatch_plexer_and_stream_ref;

void *__ym_plexer_notify_new_stream(ym_thread_dispatch_ref);
void *__ym_plexer_notify_stream_closing(ym_thread_dispatch_ref);
void *__ym_plexer_release_remote_stream(ym_thread_dispatch_ref);
void *__ym_plexer_notify_interrupted(ym_thread_dispatch_ref);

typedef struct {
    YMPlexerCommand command;
    YMStreamID streamID;
} YMPlexerMessage;

#define __YMDownstreamListIdx 0
#define __YMUpstreamListIdx 1
#define __YMListMax 2
#define __YMLockListIdx 0
#define __YMListListIdx 1

YMStreamRef __YMPlexerChooseReadyStream(YMPlexerRef plexer, YMTypeRef **list, int *outReadyStreamsByIdx, int *outStreamListIdx, int *outStreamIdx);
void __YMPlexerDispatchFunctionWithName(YMPlexerRef plexer, YMStreamRef stream, YMThreadRef targetThread, ym_thread_dispatch_func function, char *name);
bool __YMPlexerStartServiceThreads(YMPlexerRef plexer);
bool __YMPlexerDoInitialization(YMPlexerRef plexer, bool master);
bool __YMPlexerInitAsMaster(YMPlexerRef plexer);
bool __YMPlexerInitAsSlave(YMPlexerRef plexer);
void __ym_plexer_service_downstream_proc(void *);
void __ym_plexer_service_upstream_proc(void *);
bool __YMPlexerServiceADownstream(YMPlexerRef plexer, YMStreamRef servicingStream, YMTypeRef *listOfLocksAndLists, int streamListIdx);
YMStreamRef __YMPlexerGetOrCreateRemoteStreamWithID(YMPlexerRef plexer, YMStreamID streamID);
void __YMPlexerInterrupt(YMPlexerRef plexer);

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
    void *callbackContext;
} _YMPlexer;

#define YMPlexerDefaultBufferSize (1e+6)

pthread_once_t gYMRegisterSigpipeOnce = PTHREAD_ONCE_INIT;
void sigpipe_handler (__unused int signum)
{
    ymlog("sigpipe happened");
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
    
    char *memberName = YMStringCreateWithFormat("plex-%s(%s)",name?name:"unnamed",master?"m":"s");
    plexer->name = strdup(memberName);
    free(memberName);
    
    plexer->provider = YMSecurityProviderCreate(inputFile, outputFile);
    
    plexer->localStreamsByID = YMDictionaryCreate();
    memberName = YMStringCreateWithFormat("%s-local",plexer->name);
    plexer->localAccessLock = YMLockCreate(memberName);
    free(memberName);
    plexer->localPlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->localPlexBuffer = YMALLOC(plexer->localPlexBufferSize);
    
    plexer->remoteStreamsByID = YMDictionaryCreate();
    memberName = YMStringCreateWithFormat("%s-remote",plexer->name);
    plexer->remoteAccessLock = YMLockCreate(memberName);
    free(memberName);
    plexer->remotePlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->remotePlexBuffer = YMALLOC(plexer->remotePlexBufferSize);
    
    memberName = YMStringCreateWithFormat("%s-down",plexer->name);
    plexer->localServiceThread = YMThreadCreate(memberName, __ym_plexer_service_downstream_proc, plexer);
    free(memberName);
    
    memberName = YMStringCreateWithFormat("%s-up",plexer->name);
    plexer->remoteServiceThread = YMThreadCreate(memberName, __ym_plexer_service_upstream_proc, plexer);
    free(memberName);
    
    memberName = YMStringCreateWithFormat("%s-event",plexer->name);
    plexer->eventDeliveryThread = YMThreadDispatchCreate(memberName);
    free(memberName);
    
    memberName = YMStringCreateWithFormat("%s-interrupt",plexer->name);
    plexer->interruptionLock = YMLockCreate(memberName);
    free(memberName);
    
    memberName = YMStringCreateWithFormat("%s-signal",plexer->name);
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
#pragma message "implement me"
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

void YMPlexerSetCallbackContext(YMPlexerRef plexer, void *context)
{
    plexer->callbackContext = context;
}

void YMPlexerSetSecurityProvider(YMPlexerRef plexer, YMTypeRef provider)
{
    YMTypeID type = ((_YMTypeRef *)provider)->_typeID;
    if ( type != _YMSecurityProviderTypeID )
        ymlog(" plexer[%s]: warning: %s: provider is type '%c'",plexer->name,__FUNCTION__,type);
    plexer->provider = (YMSecurityProviderRef)provider;
}

bool YMPlexerStart(YMPlexerRef plexer)
{
    bool okay;
    
    if ( plexer->active )
    {
        ymerr(" plexer[%s]: user error: this plexer is already initialized",plexer->name);
        return false;
    }
    
    if ( plexer->master )
        okay = __YMPlexerInitAsMaster(plexer);
    else
        okay = __YMPlexerInitAsSlave(plexer);
    
    if ( ! okay )
        goto catch_fail;
    
    ymlog(" plexer[%s]: initialized m[%u:%u], s[%u:%u]",plexer->name,
          plexer->master ? plexer->localStreamIDMin : plexer->remoteStreamIDMin,
          plexer->master ? plexer->localStreamIDMax : plexer->remoteStreamIDMax,
          plexer->master ? plexer->remoteStreamIDMin : plexer->localStreamIDMin,
          plexer->master ? plexer->remoteStreamIDMax : plexer->localStreamIDMax);
    
    // this flag is used to let our threads exit, among other things
    plexer->active = true;
    
    okay = YMThreadStart(plexer->localServiceThread);
    if ( ! okay )
    {
        ymerr(" plexer[%s]: error: failed to detach down service thread",plexer->name);
        goto catch_fail;
    }
    
    okay = YMThreadStart(plexer->remoteServiceThread);
    if ( ! okay )
    {
        ymerr(" plexer[%s]: error: failed to detach up service thread",plexer->name);
        goto catch_fail;
    }
    
    okay = YMThreadStart(plexer->eventDeliveryThread);
    if ( ! okay )
    {
        ymerr(" plexer[%s]: error: failed to detach event thread",plexer->name);
        goto catch_fail;
    }
    
    ymlog(" plexer[%s,i%d-o%d]: started",plexer->name,plexer->inputFile,plexer->outputFile);
    
catch_fail:
    return okay;
}

const char *YMPlexerMasterHello = "オス、王様でおるべし";
const char *YMPlexerSlaveHello = "よろしくお願いいたします";

bool __YMPlexerInitAsMaster(YMPlexerRef plexer)
{
    char *errorTemplate = " plexer[m]: error: plexer init failed: %s";
    bool okay = YMSecurityProviderWrite(plexer->provider, (void *)YMPlexerMasterHello, strlen(YMPlexerMasterHello));
    if ( ! okay )
    {
        ymerr(errorTemplate,"master hello write");
        return false;
    }
    
    unsigned long inHelloLen = strlen(YMPlexerSlaveHello);
    char inHello[inHelloLen];
    okay = YMSecurityProviderRead(plexer->provider, (void *)inHello, inHelloLen);
    if ( ! okay || memcmp(YMPlexerSlaveHello,inHello,inHelloLen) )
    {
        ymerr(errorTemplate,"slave hello read");
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
        ymerr(errorTemplate,"master init write");
        return false;
    }
    
    YMPlexerSlaveAck ack;
    okay = YMSecurityProviderRead(plexer->provider, (void *)&ack, sizeof(ack));
    if ( ! okay )
    {
        ymerr(errorTemplate,"slave ack read");
        return false;
    }
    if ( ack.protocolVersion > YMPlexerBuiltInVersion )
    {
        ymerr(errorTemplate,"protocol mismatch");
        return false;
    }
    
    return true;
}

bool __YMPlexerInitAsSlave(YMPlexerRef plexer)
{
    char *errorTemplate = " plexer[s]: error: plexer init failed: %s";
    unsigned long inHelloLen = strlen(YMPlexerMasterHello);
    char inHello[inHelloLen];
    bool okay = YMSecurityProviderRead(plexer->provider, (void *)inHello, inHelloLen);
    
    if ( ! okay || memcmp(YMPlexerMasterHello,inHello,inHelloLen) )
    {
        ymerr(errorTemplate,"master hello read failed");
        return false;
    }
    
    okay = YMSecurityProviderWrite(plexer->provider, (void *)YMPlexerSlaveHello, strlen(YMPlexerSlaveHello));
    if ( ! okay )
    {
        ymerr(errorTemplate,"slave hello write failed");
        return false;
    }
    
    YMPlexerMasterInitializer initializer;
    okay = YMSecurityProviderRead(plexer->provider, (void *)&initializer, sizeof(initializer));
    if ( ! okay )
    {
        ymerr(errorTemplate,"master init read failed");
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
        ymerr(errorTemplate,"slave hello read error");
        return false;
    }
    
    if ( ! supported )
    {
        ymerr(errorTemplate,"master requested protocol newer than built-in %lu",YMPlexerBuiltInVersion);
        return false;
    }
    
    return true;
}

void __ym_plexer_service_downstream_proc(void * ctx)
{
    struct __YMPlexer *plexer = (struct __YMPlexer *)ctx;
    ymlog(" plexer[%s]: downstream service thread entered",plexer->name);
    
    bool okay = true;
    while(okay && plexer->active)
    {
        ymlog(" plexer[%s-V]: awaiting signal",plexer->name);
        // there is only one thread consuming this semaphore, so i think it's ok not to actually lock around this loop iteration
        YMSemaphoreWait(plexer->streamMessageSemaphore);
        
        YMTypeRef *listOfLocksAndLists[] = { (YMTypeRef[]) { plexer->localAccessLock, plexer->localStreamsByID },
            (YMTypeRef[]) { plexer->remoteAccessLock, plexer->remoteStreamsByID } };
        int readyStreamsByList[2] = { 0, 0 };
        int listIdx = -1;
        int streamIdx = -1;
        YMStreamRef servicingStream = __YMPlexerChooseReadyStream(plexer, listOfLocksAndLists, readyStreamsByList, &listIdx, &streamIdx);
        
        ymlog(" plexer[%s-V]: signaled, [d%d,u%d] streams ready",plexer->name,readyStreamsByList[__YMDownstreamListIdx],readyStreamsByList[__YMUpstreamListIdx]);
        
        // todo about not locking until we've consumed as many semaphore signals as we can
        //while ( --readyStreams )
        {
            if ( ! servicingStream )
            {
                ymerr(" plexer[%s-V]: fatal: signaled but nothing available",plexer->name);
                abort();
            }
            
            okay = __YMPlexerServiceADownstream(plexer, servicingStream, listOfLocksAndLists[listIdx], streamIdx);
            if ( ! okay )
            {
                ymerr(" plexer[%s-V]: perror: service downstream failed",plexer->name);
                __YMPlexerInterrupt(plexer);
                okay = false;
            }
        }
    }
    
    ymlog(" plexer[%s]: downstream service thread exiting",plexer->name);
}

YMStreamRef __YMPlexerChooseReadyStream(YMPlexerRef plexer, YMTypeRef **list, int *outReadyStreamsByIdx, int *outListIdx, int *outStreamIdx)
{
    YMStreamRef oldestStream = NULL;
    struct timeval newestTime;
    YMGetTheEndOfPosixTimeForCurrentPlatform(&newestTime);
    
    int listIdx = 0;
    for( ; listIdx < __YMListMax; listIdx++ )
    {
        YMLockRef aLock = (YMLockRef)list[listIdx][__YMLockListIdx];
        YMDictionaryRef aStreamsById = (YMDictionaryRef)list[listIdx][__YMListListIdx];
        
        YMLockLock(aLock);
        {
            int streamIdx = 0;
            YMDictionaryEnumRef aStreamsEnum = YMDictionaryEnumeratorBegin(aStreamsById);
            YMDictionaryEnumRef aStreamsEnumPrev = NULL;
            while ( aStreamsEnum )
            {
                YMStreamRef aStream = (YMStreamRef)aStreamsEnum->value;
                __unused YMStreamID aStreamID = _YMStreamGetUserInfo(aStream)->streamID;
                int downRead = _YMStreamGetDownwardRead(aStream);
                ymlog(" plexer[%s-choose]: considering %s downstream %u",plexer->name,listIdx==__YMDownstreamListIdx?"local":"remote",_YMStreamGetUserInfo(aStream)->streamID);
                
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
                        ymerr(" plexer[%s-choose]: fatal: select failed %d (%s)",plexer->name,errno,strerror(errno));
                        abort();
                    }
                    goto catch_continue;
                }
                
                ymlog(" plexer[%s-choose]: %s stream %u reports it is ready!",plexer->name,listIdx==__YMDownstreamListIdx?"local":"remote",aStreamID);
                
                if ( outReadyStreamsByIdx )
                    outReadyStreamsByIdx[listIdx]++;
                
                struct timeval *thisStreamLastService = _YMStreamGetLastServiceTime(aStream);
                if ( YMTimevalCompare(thisStreamLastService, &newestTime ) != GreaterThan )
                {
                    ymlog(" plexer[%s-choose]: adding ready %s stream %u",plexer->name,listIdx==__YMDownstreamListIdx?"local":"remote",aStreamID);
                    if ( outStreamIdx )
                        *outStreamIdx = streamIdx;
                    if ( outListIdx )
                        *outListIdx = listIdx;
                    oldestStream = aStream;
                }
                
            catch_continue:
                aStreamsEnumPrev = aStreamsEnum;
                aStreamsEnum = YMDictionaryEnumeratorGetNext(aStreamsEnum);
            }
            if ( aStreamsEnumPrev )
                YMDictionaryEnumeratorEnd(aStreamsEnum);
            else
                ymlog(" plexer[%s-choose]: %s list is empty",plexer->name,listIdx==__YMDownstreamListIdx?"down stream":"up stream");
                
        }
        YMLockUnlock(aLock);
    }
    
    return oldestStream;
}

bool __YMPlexerServiceADownstream(YMPlexerRef plexer, YMStreamRef servicingStream, YMTypeRef *lockAndList, int streamIdx)
{
    YMStreamID streamID = _YMStreamGetUserInfo(servicingStream)->streamID;
    ymlog(" plexer[%s-V,V]: chose stream %u for service",plexer->name,streamID);
    
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
            ymlog(" plexer[%s-V,s%u]: servicing stream chunk %u",plexer->name,streamID, chunkLength);
            closing = true;
        }
        else
        {
            ymerr(" plexer[%s-V,s%u]: fatal: invalid command: %d",plexer->name,streamID,streamCommand.command);
            abort();
        }
    }
    else
    {
        chunkLength = streamCommand.command;
        ymlog(" plexer[%s-V,s%u]: servicing stream chunk %ub",plexer->name,streamID, chunkLength);
    }
    
    while ( chunkLength > plexer->localPlexBufferSize )
    {
        plexer->localPlexBufferSize *= 2;
        ymerr(" plexer[%s-V,s%u] reallocating buffer to %ub",plexer->name,streamID,plexer->localPlexBufferSize);
        plexer->localPlexBuffer = realloc(plexer->localPlexBuffer, plexer->localPlexBufferSize);
    }
    
//okay =
    _YMStreamReadDown(servicingStream, plexer->localPlexBuffer, chunkLength);
//    if ( ! okay )
//    {
//        ymerr(" plexer[%s,V,s%u]: fatal: reading stream chunk size %u: %d (%s)",plexer->name, streamID,streamHeader.length,errno,strerror(errno));
//        abort();
//    }
    
    ymlog(" plexer[%s-V,s%u]: read stream chunk",plexer->name,streamID);
    
    YMPlexerMessage plexMessage = {closing ? YMPlexerCommandCloseStream : chunkLength, streamID };
    size_t plexMessageLen = sizeof(plexMessage);
    YMIOResult ioResult = YMWriteFull(plexer->outputFile, (void *)&plexMessage, plexMessageLen, NULL);
    if ( ioResult != YMIOSuccess )
    {
        ymerr(" plexer[%s-V,o%d!,s%u]: perror: failed writing plex message size %zub: %d (%s)",plexer->name,plexer->outputFile,streamID,plexMessageLen,errno,strerror(errno));
        return false;
    }
    
    if ( closing )
    {
        ymlog(" plexer[%s-V,s%u]: stream closing: %p (%s)",plexer->name,streamID,servicingStream,_YMStreamGetName(servicingStream));
        
        if ( streamIdx < 0 || ! lockAndList )
        {
            ymerr(" plexer[%s-V,s%u]: fatal: closing stream not found in lists",plexer->name,streamID);
            abort();
        }
        
        YMDictionaryRef theList = (YMDictionaryRef)lockAndList[__YMListListIdx];
        YMLockRef theLock = (YMLockRef)lockAndList[__YMLockListIdx];
        YMStreamRef testRemove = NULL;
        YMLockLock(theLock);
        {
            testRemove = (YMStreamRef)YMDictionaryRemove(theList, streamID);
        }
        YMLockUnlock(theLock);
        if ( ! testRemove || testRemove != servicingStream )
        {
            ymerr(" plexer[%s-V,i%d<->o%d,s%u]: fatal: internal check failed removing %u",plexer->name,plexer->inputFile,plexer->outputFile,streamID,streamID);
            abort();
        }
        
        // see comment in _YMStreamClose, we have to let the client's close call return before actually freeing this object
        YMLockLock(_YMStreamGetRetainLock(servicingStream));
        {
        }
        YMLockUnlock(_YMStreamGetRetainLock(servicingStream));
        _YMStreamDesignatedFree(servicingStream);
        
        return true;
    }
    
    ymlog(" plexer[%s-V,o%d!,s%u]: wrote plex header",plexer->name,plexer->outputFile,streamID);
    
    ioResult = YMWriteFull(plexer->outputFile, plexer->localPlexBuffer, chunkLength, NULL);
    if ( ioResult != YMIOSuccess )
    {
        ymerr(" plexer[%s-V,o%d!,s%u]: perror: failed writing plex buffer %ub: %d (%s)",plexer->name,plexer->outputFile,streamID,plexMessage.command,errno,strerror(errno));
        return false;
    }
    
    ymlog(" plexer[%s-V,o%d!,s%u]: wrote plex buffer %ub",plexer->name,plexer->outputFile,streamID,plexMessage.command);
    return true;
}

void __ym_plexer_service_upstream_proc(void * ctx)
{
    YMPlexerRef plexer = (YMPlexerRef)ctx;
    ymlog(" plexer[%s-^-i%d->o%d]: upstream service thread entered",plexer->name,plexer->inputFile,plexer->outputFile);
    
    YMIOResult result = YMIOSuccess;
    
    while ( ( result == YMIOSuccess ) && plexer->active )
    {
        bool streamClosing = false;
        size_t chunkLength = 0;
        YMPlexerMessage plexerMessage;
        
        result = YMReadFull(plexer->inputFile, (void *)&plexerMessage, sizeof(plexerMessage), NULL);
        if ( result != YMIOSuccess )
        {
            ymerr(" plexer[%s-^,i%d!->o%d]: perror: failed reading plex header: %d (%s)",plexer->name,plexer->inputFile,plexer->outputFile,errno,strerror(errno));
            __YMPlexerInterrupt(plexer);
            break;
        }
        else if ( plexerMessage.command == YMPlexerCommandCloseStream )
        {
            ymlog(" plexer[%s-^,i%d!->o%d,s%u] stream closing",plexer->name,plexer->inputFile,plexer->outputFile,plexerMessage.streamID);
            streamClosing = true;
        }
        else
        {
            chunkLength = plexerMessage.command;
            ymlog(" plexer[%s-^,i%d!->o%d,s%u] read plex header %zub",plexer->name,plexer->inputFile,plexer->outputFile,plexerMessage.streamID,chunkLength);
        }
        
        YMStreamID streamID = plexerMessage.streamID;
        YMStreamRef theStream = __YMPlexerGetOrCreateRemoteStreamWithID(plexer, streamID);
        if ( ! theStream )
        {
            ymerr(" plexer[%s-^,i%d->o%d,s%u]: fatal: stream lookup",plexer->name,plexer->inputFile,plexer->outputFile,streamID);
            __YMPlexerInterrupt(plexer);
            abort();
            break;
        }
        
        if ( streamClosing )
        {
            char *memberName = YMStringCreateWithFormat("%s-s%u-%s",plexer->name,streamID,YM_TOKEN_STR(__ym_plexer_notify_stream_closing));
            __YMPlexerDispatchFunctionWithName(plexer, theStream, plexer->eventDeliveryThread, __ym_plexer_notify_stream_closing, memberName);
            ymlog(" plexer[%s-^,s%u]: dispatched notify-closing", plexer->name, streamID);
            
            // cases:
            //  1. plexer receives remote command after user has RemoteReleased()
            //  2. plexer receives remote command before user has RemoteReleased()
            //		(e.g. last user message sent by remote, not yet consumed by local (this plexer)
            //
            
            // we manage upward closures by notification (and agreement that remote-originated must be RemoteReleased), so don't close fds here
            //_YMStreamCloseUp(theStream);
            continue;
        }
        
        while ( chunkLength > plexer->remotePlexBufferSize )
        {
            plexer->remotePlexBufferSize *= 2;
            ymerr(" plexer[%s-^,i%d->o%d,s%u] reallocating plex buffer for %u",plexer->name,plexer->inputFile,plexer->outputFile,streamID,plexer->remotePlexBufferSize);
            plexer->remotePlexBuffer = realloc(plexer->remotePlexBuffer, plexer->remotePlexBufferSize);
        }
        
        result = YMReadFull(plexer->inputFile, plexer->remotePlexBuffer, chunkLength, NULL);
        if ( result != YMIOSuccess )
        {
            ymerr(" plexer[%s-^,i%d!->o%d,s%u]: perror: failed reading plex buffer of length %zub: %d (%s)",plexer->name,plexer->inputFile,plexer->outputFile,streamID,chunkLength,errno,strerror(errno));
            __YMPlexerInterrupt(plexer);
            break;
        }
        ymlog(" plexer[%s-^,i%d->o%d!,s%u] read plex buffer %zub",plexer->name,plexer->inputFile,plexer->outputFile,streamID,chunkLength);
        
        int streamInFd = _YMStreamGetUpstreamWrite(theStream);
        ymlog(" plexer[%s-^,i%d->o%d,si%d!,s%u] writing plex buffer %zub",plexer->name,plexer->inputFile,plexer->outputFile,streamInFd,streamID,chunkLength);
        result = YMWriteFull(streamInFd, plexer->remotePlexBuffer, chunkLength, NULL);
        if ( result != YMIOSuccess )
        {
            ymerr(" plexer[%s-^,i%d->o%d,si%d!,s%u]: fatal: failed writing plex buffer of length %zub: %d (%s)",plexer->name,plexer->inputFile,plexer->outputFile,streamInFd,streamID,chunkLength,errno,strerror(errno));
            __YMPlexerInterrupt(plexer);
            abort();
            break;
        }
        ymlog(" plexer[%s-^,i%d->o%d,si%d!,s%u] wrote plex buffer %zub",plexer->name,plexer->inputFile,plexer->outputFile,streamInFd,streamID,chunkLength);
    }
    
    ymlog(" plexer[%s-^,i%d->o%d]: upstream service thread exiting",plexer->name,plexer->inputFile,plexer->outputFile);
}

YMStreamRef __YMPlexerGetOrCreateRemoteStreamWithID(YMPlexerRef plexer, YMStreamID streamID)
{
    YMStreamRef theStream = NULL;
    
#pragma message "__YMPlexerChooseReadyStream should handle up (is it local or remote?) as well as down (which is the oldest signaled?)?"
    YMLockLock(plexer->localAccessLock);
    {
        theStream = (YMStreamRef)YMDictionaryGetItem(plexer->localStreamsByID,streamID);
        if ( theStream )
            ymlog(" plexer[%s,s%u]: found existing local stream",plexer->name,streamID);
    }
    YMLockUnlock(plexer->localAccessLock);
    
    if ( ! theStream )
    {
        YMLockLock(plexer->remoteAccessLock);
        {
            theStream = (YMStreamRef)YMDictionaryGetItem(plexer->remoteStreamsByID,streamID);
            if ( theStream )
                ymlog(" plexer[%s,s%u]: found existing remote stream",plexer->name,streamID);
        
            // new stream
            if ( ! theStream )
            {
                ymlog(" plexer[%s,s%u]: alloc and notify",plexer->name,streamID);
                
                YMStreamUserInfoRef userInfo = (YMStreamUserInfoRef)YMALLOC(sizeof(struct __YMStreamUserInfo));
                userInfo->streamID = streamID;
                char *memberName = YMStringCreateWithFormat("%s-^-%u",plexer->name,streamID);
                theStream = YMStreamCreate(memberName, false, userInfo);
                free(memberName);
                _YMStreamSetDataAvailableSemaphore(theStream, plexer->streamMessageSemaphore);
                
                YMDictionaryAdd(plexer->remoteStreamsByID, streamID, theStream);
                
                memberName = YMStringCreateWithFormat("%s-notify-new-%u",plexer->name,streamID);
                __YMPlexerDispatchFunctionWithName(plexer, theStream, plexer->eventDeliveryThread, __ym_plexer_notify_new_stream, memberName);
            }
        }    
        YMLockUnlock(plexer->remoteAccessLock);
    }
    
    return theStream;
}

void __YMPlexerInterrupt(YMPlexerRef plexer)
{
    ymlog(" plexer[%s]: *** _YMPlexerInterrupt, todo",plexer->name);
    
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
#pragma message "how should this actually behave? join on remaining activity?"
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
        if ( YMDictionaryContains(plexer->localStreamsByID, newStreamID) )
        {
            ymerr(" plexer[%s]: fatal: YMPlexer has run out of streams",plexer->name);
            abort();
        }
        
        YMStreamUserInfoRef userInfo = (YMStreamUserInfoRef)YMALLOC(sizeof(struct __YMStreamUserInfo));
        userInfo->streamID = newStreamID;
        if ( ! name ) name = "*";
        char *memberName = YMStringCreateWithFormat("%s-V-%u:%s",plexer->name,newStreamID,name);
        newStream = YMStreamCreate(memberName, true, userInfo);
        free(memberName);
        _YMStreamSetDataAvailableSemaphore(newStream, plexer->streamMessageSemaphore);
        
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
    YMLockRef theLock = NULL;
    YMLockLock(plexer->localAccessLock);
    {
        localStream = (YMStreamRef)YMDictionaryGetItem(plexer->localStreamsByID, streamID);
        theLock = plexer->localAccessLock;
    } // float
    
    if ( localStream == NULL )
    {
        bool isRemote;
        YMLockLock(plexer->remoteAccessLock);
        {
            isRemote = YMDictionaryContains(plexer->remoteStreamsByID, streamID);
            theLock = plexer->remoteAccessLock;
        } // float
        
        if ( isRemote )
            ymerr(" plexer[%s]: error: user requested closure of remote stream %u",plexer->name,streamID);
        else
            ymerr(" plexer[%s]: error: user requested closure of unknown stream %u",plexer->name,streamID);
        
        abort();
    }
    
    _YMStreamClose(localStream);
    
    YMLockUnlock(theLock);
}

void YMPlexerRemoteStreamRelease(__unused YMPlexerRef plexer, YMStreamRef stream)
{
    if ( _YMStreamIsLocallyOriginated(stream) )
    {
        ymlog(" plexer[%s]: remote releasing locally originated stream %u",plexer->name, _YMStreamGetUserInfo(stream)->streamID);
        abort();
    }
    
    _YMStreamRemoteSetUserReleased(stream);
}

#pragma mark dispatch

void __YMPlexerDispatchFunctionWithName(YMPlexerRef plexer, YMStreamRef stream, YMThreadRef targetThread, ym_thread_dispatch_func function, char *name)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = (__ym_dispatch_plexer_and_stream_ref)YMALLOC(sizeof(__ym_dispatch_plexer_and_stream));
    notifyDef->plexer = plexer;
    notifyDef->stream = stream;
    ym_thread_dispatch dispatch = { function, NULL, true, notifyDef, name };
    YMThreadDispatchDispatch(targetThread, dispatch);
    free(name);
}

void *__ym_plexer_notify_new_stream(ym_thread_dispatch_ref dispatch)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = (__ym_dispatch_plexer_and_stream_ref)dispatch->context;
    YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    __unused YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    
    ymlog(" plexer[%s,s%u] ym_notify_new_stream entered", plexer->name, streamID);
    plexer->newIncomingFunc(plexer,stream,plexer->callbackContext);
    ymlog(" plexer[%s,s%u] ym_notify_new_stream exiting", plexer->name, streamID);
    
    return NULL;
}

void *__ym_plexer_notify_stream_closing(ym_thread_dispatch_ref dispatch)
{
    _ym_dispatch_plexer_stream_def *notifyDef = (_ym_dispatch_plexer_stream_def *)dispatch->context;
    YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    
    ymlog(" plexer[%s,s%u] ym_notify_stream_closing entered", plexer->name, streamID);
    plexer->closingFunc(plexer,stream,plexer->callbackContext);
    
    YMLockLock(plexer->remoteAccessLock);
    {
        ymlog(" plexer[%s,s%u]: PLEXER RELEASING stream %u",plexer->name,streamID,streamID);
        _YMStreamRemoteSetPlexerReleased(stream);
        YMDictionaryRemove(plexer->remoteStreamsByID, streamID);
    }
    YMLockUnlock(plexer->remoteAccessLock);
    
    ymlog(" plexer[%s,s%u] ym_notify_stream_closing exiting", plexer->name, streamID);
    
    return NULL;
}

// todo with the 'retain count' thing, i don't think this is necessary anymore
void *__ym_plexer_release_remote_stream(ym_thread_dispatch_ref dispatch)
{
    _ym_dispatch_plexer_stream_def *notifyDef = (_ym_dispatch_plexer_stream_def *)dispatch->context;
    __unused YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    __unused YMStreamID streamID = _YMStreamGetUserInfo(stream)->streamID;
    
    ymlog(" plexer[%s,s%u]: PLEXER RELEASING stream %u",plexer->name,streamID,streamID);
    _YMStreamRemoteSetPlexerReleased(stream);
    
    return NULL;
}

void *__ym_plexer_notify_interrupted(ym_thread_dispatch_ref dispatch)
{
    _ym_dispatch_plexer_stream_def *notifyDef = (_ym_dispatch_plexer_stream_def *)dispatch->context;
    YMPlexerRef plexer = notifyDef->plexer;
    
    ymlog(" plexer[%s] ym_notify_interrupted entered", plexer->name);
    plexer->interruptedFunc(plexer,plexer->callbackContext);
    ymlog(" plexer[%s] ym_notify_interrupted exiting", plexer->name);
    
    return NULL;
}

