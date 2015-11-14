//
//  YMPlexer.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPlexer.h"
#include "YMPlexerPriv.h"

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

void __ym_plexer_notify_new_stream(ym_thread_dispatch_ref);
void __ym_plexer_notify_stream_closing(ym_thread_dispatch_ref);
void __ym_plexer_release_remote_stream(ym_thread_dispatch_ref);
void __ym_plexer_notify_interrupted(ym_thread_dispatch_ref);

// linked to 'private' definition in YMPlexerPriv
typedef struct __ym_plexer_stream_user_info_def
{
    YMStringRef name;
    YMPlexerStreamID streamID;
    
    bool isLocallyOriginated;
    struct timeval *lastServiceTime; // free me
    
    YMLockRef retainLock; // used to synchronize between local stream writing 'close' & plexer free'ing, and between remote exiting -newStream & plexer free'ing
    bool isPlexerReleased;
    bool isUserReleased;
    bool isDeallocated;
} ___ym_plexer_stream_user_info_def;
typedef struct __ym_plexer_stream_user_info_def __ym_plexer_stream_user_info;
typedef __ym_plexer_stream_user_info * __ym_plexer_stream_user_info_ref;

#undef YM_STREAM_INFO
#define YM_STREAM_INFO(x) ((__ym_plexer_stream_user_info_ref)_YMStreamGetUserInfo(x))

typedef struct {
    YMPlexerCommand command;
    YMPlexerStreamID streamID;
} YMPlexerMessage;

typedef struct __ym_plexer
{
    _YMType _type;
    
    YMStringRef name;
    YMSecurityProviderRef provider;
    
    int inputFile;
    int outputFile;
    bool active; // intialized and happy
    bool stopped; // stop called before files start closing
    bool master;
    
    // the downstream
    YMDictionaryRef localStreamsByID;
    YMLockRef localAccessLock;
    uint8_t *localPlexBuffer;
    uint32_t localPlexBufferSize;
    YMPlexerStreamID localStreamIDMin;
    YMPlexerStreamID localStreamIDMax;
    YMPlexerStreamID localStreamIDLast;
    
    // the upstream
    YMDictionaryRef remoteStreamsByID;
    YMLockRef remoteAccessLock;
    uint8_t *remotePlexBuffer;
    uint32_t remotePlexBufferSize;
    YMPlexerStreamID remoteStreamIDMin;
    YMPlexerStreamID remoteStreamIDMax;
    
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
} ___ym_plexer;
typedef struct __ym_plexer __YMPlexer;
typedef __YMPlexer *__YMPlexerRef;

// generic context pointer definition for "plexer & stream" entry points
typedef struct __ym_dispatch_plexer_stream_def
{
    __YMPlexerRef plexer;
    YMStreamRef stream;
} _ym_dispatch_plexer_stream_def;
typedef struct __ym_dispatch_plexer_stream_def __ym_dispatch_plexer_and_stream;
typedef __ym_dispatch_plexer_and_stream *__ym_dispatch_plexer_and_stream_ref;

YMStreamRef __YMPlexerChooseReadyStream(__YMPlexerRef plexer, YMTypeRef **list, int *outReadyStreamsByIdx, int *outStreamListIdx, int *outStreamIdx);
void __YMPlexerDispatchFunctionWithName(__YMPlexerRef plexer, YMStreamRef stream, YMThreadRef targetThread, ym_thread_dispatch_func function, YMStringRef name);
bool __YMPlexerStartServiceThreads(__YMPlexerRef plexer);
bool __YMPlexerDoInitialization(__YMPlexerRef plexer, bool master);
bool __YMPlexerInitAsMaster(__YMPlexerRef plexer);
bool __YMPlexerInitAsSlave(__YMPlexerRef plexer);
void __ym_plexer_service_downstream_proc(void *);
void __ym_plexer_service_upstream_proc(void *);
bool __YMPlexerServiceADownstream(__YMPlexerRef plexer, YMStreamRef servicingStream, YMTypeRef *listOfLocksAndLists, int streamListIdx);
YMStreamRef __YMPlexerGetOrCreateRemoteStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID);
bool __YMPlexerInterrupt(__YMPlexerRef plexer);
void __ym_plexer_stream_data_available_proc(void *ctx);

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

YMPlexerRef YMPlexerCreateWithFullDuplexFile(YMStringRef name, int file, bool master)
{
    return YMPlexerCreate(name, file, file, master);
}

YMPlexerRef YMPlexerCreate(YMStringRef name, int inputFile, int outputFile, bool master)
{
    pthread_once(&gYMRegisterSigpipeOnce, _YMRegisterSigpipe);
    
    __YMPlexerRef plexer = (__YMPlexerRef)_YMAlloc(_YMPlexerTypeID,sizeof(__YMPlexer));
    
    plexer->name = YMStringCreateWithFormat("plex-%s(%s)",name?YMSTR(name):"unnamed",master?"m":"s",NULL);
    plexer->provider = YMSecurityProviderCreate(inputFile, outputFile);
    
    plexer->inputFile = inputFile;
    plexer->outputFile = outputFile;
    plexer->active = false;
    plexer->master = master;
    plexer->stopped = false;
    
    plexer->localStreamsByID = YMDictionaryCreate();
    YMStringRef aString = YMStringCreateWithFormat("%s-local",YMSTR(plexer->name), NULL);
    plexer->localAccessLock = YMLockCreate(aString);
    YMRelease(aString);
    plexer->localPlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->localPlexBuffer = YMALLOC(plexer->localPlexBufferSize);
    
    plexer->remoteStreamsByID = YMDictionaryCreate();
    aString = YMStringCreateWithFormat("%s-remote",YMSTR(plexer->name),NULL);
    plexer->remoteAccessLock = YMLockCreate(aString);
    YMRelease(aString);
    plexer->remotePlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->remotePlexBuffer = YMALLOC(plexer->remotePlexBufferSize);
    
    aString = YMStringCreateWithFormat("%s-down",YMSTR(plexer->name),NULL);
    plexer->localServiceThread = YMThreadCreate(aString, __ym_plexer_service_downstream_proc, plexer);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-up",YMSTR(plexer->name),NULL);
    plexer->remoteServiceThread = YMThreadCreate(aString, __ym_plexer_service_upstream_proc, plexer);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-event",YMSTR(plexer->name),NULL);
    plexer->eventDeliveryThread = YMThreadDispatchCreate(aString);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-interrupt",YMSTR(plexer->name),NULL);
    plexer->interruptionLock = YMLockCreate(aString);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-signal",YMSTR(plexer->name),NULL);
    plexer->streamMessageSemaphore = YMSemaphoreCreate(aString,0);
    YMRelease(aString);
    
    plexer->interruptedFunc = NULL;
    plexer->newIncomingFunc = NULL;
    plexer->closingFunc = NULL;
    plexer->callbackContext = NULL;
    
    return plexer;
}

void _YMPlexerFree(YMPlexerRef plexer_)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
    YMRelease(plexer->name);
    YMRelease(plexer->provider);
    
    free(plexer->localPlexBuffer);
    YMRelease(plexer->localStreamsByID);
    YMRelease(plexer->localAccessLock);
    YMRelease(plexer->localServiceThread);
    free(plexer->remotePlexBuffer);
    YMRelease(plexer->remoteStreamsByID);
    YMRelease(plexer->remoteAccessLock);
    YMRelease(plexer->remoteServiceThread);
    
    YMRelease(plexer->eventDeliveryThread);
    YMRelease(plexer->interruptionLock);
    YMRelease(plexer->streamMessageSemaphore);
}

void YMPlexerSetInterruptedFunc(YMPlexerRef plexer_, ym_plexer_interrupted_func func)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    plexer->interruptedFunc = func;
}

void YMPlexerSetNewIncomingStreamFunc(YMPlexerRef plexer_, ym_plexer_new_upstream_func func)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    plexer->newIncomingFunc = func;
}

void YMPlexerSetStreamClosingFunc(YMPlexerRef plexer_, ym_plexer_stream_closing_func func)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    plexer->closingFunc = func;
}

void YMPlexerSetCallbackContext(YMPlexerRef plexer_, void *context)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    plexer->callbackContext = context;
}

void YMPlexerSetSecurityProvider(YMPlexerRef plexer_, YMTypeRef provider)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    if ( plexer->_type.type != _YMSecurityProviderTypeID )
        ymlog(" plexer[%s]: warning: %s: provider is type '%c'",YMSTR(plexer->name),__FUNCTION__,plexer->_type.type);
    plexer->provider = (YMSecurityProviderRef)provider;
}

bool YMPlexerStart(YMPlexerRef plexer_)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
    bool okay;
    
    if ( plexer->active )
    {
        ymerr(" plexer[%s]: user error: this plexer is already initialized",YMSTR(plexer->name));
        return false;
    }
    
    if ( plexer->master )
        okay = __YMPlexerInitAsMaster(plexer);
    else
        okay = __YMPlexerInitAsSlave(plexer);
    
    if ( ! okay )
        goto catch_fail;
    
    ymlog(" plexer[%s]: initialized m[%u:%u], s[%u:%u]",YMSTR(plexer->name),
          plexer->master ? plexer->localStreamIDMin : plexer->remoteStreamIDMin,
          plexer->master ? plexer->localStreamIDMax : plexer->remoteStreamIDMax,
          plexer->master ? plexer->remoteStreamIDMin : plexer->localStreamIDMin,
          plexer->master ? plexer->remoteStreamIDMax : plexer->localStreamIDMax);
    
    // this flag is used to let our threads exit, among other things
    plexer->active = true;
    
    okay = YMThreadStart(plexer->localServiceThread);
    if ( ! okay )
    {
        ymerr(" plexer[%s]: error: failed to detach down service thread",YMSTR(plexer->name));
        goto catch_fail;
    }
    
    okay = YMThreadStart(plexer->remoteServiceThread);
    if ( ! okay )
    {
        ymerr(" plexer[%s]: error: failed to detach up service thread",YMSTR(plexer->name));
        goto catch_fail;
    }
    
    okay = YMThreadStart(plexer->eventDeliveryThread);
    if ( ! okay )
    {
        ymerr(" plexer[%s]: error: failed to detach event thread",YMSTR(plexer->name));
        goto catch_fail;
    }
    
    ymlog(" plexer[%s,i%d-o%d]: started",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile);
    
catch_fail:
    return okay;
}

const char *YMPlexerMasterHello = "オス、王様でおるべし";
const char *YMPlexerSlaveHello = "よろしくお願いいたします";

bool __YMPlexerInitAsMaster(__YMPlexerRef plexer_)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
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
    plexer->localStreamIDMax = YMPlexerStreamIDMax / 2;
    plexer->localStreamIDLast = plexer->localStreamIDMax;
    plexer->remoteStreamIDMin = plexer->localStreamIDMax + 1;
    plexer->remoteStreamIDMax = YMPlexerStreamIDMax;
    
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

bool __YMPlexerInitAsSlave(__YMPlexerRef plexer)
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
    plexer->localStreamIDMax = YMPlexerStreamIDMax;
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
    __YMPlexerRef plexer = (__YMPlexerRef)ctx;
    
    ymlog(" plexer[%s]: downstream service thread entered",YMSTR(plexer->name));
    
    while(plexer->active)
    {
        ymlog(" plexer[%s-V]: awaiting signal",YMSTR(plexer->name));
        // there is only one thread consuming this semaphore, so i think it's ok not to actually lock around this loop iteration
        YMSemaphoreWait(plexer->streamMessageSemaphore);
        
#define __YMOutgoingListIdx 0
#define __YMIncomingListIdx 1
#define __YMListMax 2
#define __YMLockListIdx 0
#define __YMListListIdx 1
        YMTypeRef *listOfLocksAndLists[] = { (YMTypeRef[]) { plexer->localAccessLock, plexer->localStreamsByID },
            (YMTypeRef[]) { plexer->remoteAccessLock, plexer->remoteStreamsByID } };
        int readyStreamsByList[2] = { 0, 0 };
        int listIdx = -1;
        int streamIdx = -1;
        YMStreamRef servicingStream = __YMPlexerChooseReadyStream(plexer, listOfLocksAndLists, readyStreamsByList, &listIdx, &streamIdx);
        
        ymlog(" plexer[%s-V]: signaled, [d%d,u%d] streams ready",YMSTR(plexer->name),readyStreamsByList[__YMOutgoingListIdx],readyStreamsByList[__YMIncomingListIdx]);
        
        // todo about not locking until we've consumed as many semaphore signals as we can
        //while ( --readyStreams )
        {
            if ( ! servicingStream )
            {
                ymerr(" plexer[%s-V]: internal fatal: signaled but nothing available",YMSTR(plexer->name));
                abort();
            }
            
            bool okay = __YMPlexerServiceADownstream(plexer, servicingStream, listOfLocksAndLists[listIdx], streamIdx);
            if ( ! okay )
            {
                if ( __YMPlexerInterrupt(plexer) )
                    ymerr(" plexer[%s-V]: perror: service downstream failed",YMSTR(plexer->name));
                break;
            }
        }
    }
    
    ymlog(" plexer[%s]: downstream service thread exiting",YMSTR(plexer->name));
}

YMStreamRef __YMPlexerChooseReadyStream(__YMPlexerRef plexer, YMTypeRef **list, int *outReadyStreamsByIdx, int *outListIdx, int *outStreamIdx)
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
                YMPlexerStreamID aStreamID = YM_STREAM_INFO(aStream)->streamID;
                int downRead = _YMStreamGetDownwardRead(aStream);
                ymlog(" plexer[%s-choose]: considering %s downstream %u",YMSTR(plexer->name),listIdx==__YMOutgoingListIdx?"local":"remote",aStreamID);
                
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
                        if ( __YMPlexerInterrupt(plexer) )
                        {
                            ymerr(" plexer[%s-choose]: fatal: select failed %d (%s)",YMSTR(plexer->name),errno,strerror(errno));
                            abort();
                        }
                        YMLockUnlock(aLock);
                        return NULL;
                    }
                    goto catch_continue;
                }
                
                ymlog(" plexer[%s-choose]: %s stream %u reports it is ready!",YMSTR(plexer->name),listIdx==__YMOutgoingListIdx?"local":"remote",aStreamID);
                
                if ( outReadyStreamsByIdx )
                    outReadyStreamsByIdx[listIdx]++;
                
                struct timeval *thisStreamLastService = YM_STREAM_INFO(aStream)->lastServiceTime;
                if ( YMTimevalCompare(thisStreamLastService, &newestTime ) != GreaterThan )
                {
                    ymlog(" plexer[%s-choose]: adding ready %s stream %u",YMSTR(plexer->name),listIdx==__YMOutgoingListIdx?"local":"remote",aStreamID);
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
                ymlog(" plexer[%s-choose]: %s list is empty",YMSTR(plexer->name),listIdx==__YMOutgoingListIdx?"down stream":"up stream");
                
        }
        YMLockUnlock(aLock);
    }
    
    return oldestStream;
}

bool __YMPlexerServiceADownstream(__YMPlexerRef plexer, YMStreamRef servicingStream, YMTypeRef *lockAndList, int streamIdx)
{
    YMPlexerStreamID streamID = YM_STREAM_INFO(servicingStream)->streamID;
    ymlog(" plexer[%s-V,V]: chose stream %u for service",YMSTR(plexer->name),streamID);
    
    // update last service time on stream
    __ym_plexer_stream_user_info_ref streamInfo = YM_STREAM_INFO(servicingStream);
    if ( 0 != gettimeofday(streamInfo->lastServiceTime, NULL) )
    {
        ymerr(" plexer[%s-V,V]: warning: error setting initial service time for stream: %d (%s)",YMSTR(plexer->name),errno,strerror(errno));
        YMGetTheBeginningOfPosixTimeForCurrentPlatform(streamInfo->lastServiceTime);
    }
    
    YMStreamCommand streamCommand;

    uint32_t chunkLength = 0;
    bool closing = false;
    _YMStreamReadDown(servicingStream, (void *)&streamCommand, sizeof(streamCommand));
    if ( streamCommand.command <= 0 ) // handle command
    {
        if ( streamCommand.command == YMStreamClose )
        {
            ymlog(" plexer[%s-V,s%u]: stream command close",YMSTR(plexer->name),streamID);
            closing = true;
        }
        else if ( streamCommand.command >= 0 ) // not really sure what '0' would mean or if it should be an error
        {
            ymlog(" plexer[%s-V,s%u]: servicing stream chunk %u",YMSTR(plexer->name),streamID, chunkLength);
        }
        else
        {
            ymerr(" plexer[%s-V,s%u]: fatal: invalid command: %d",YMSTR(plexer->name),streamID,streamCommand.command);
            abort();
        }
    }
    else
    {
        chunkLength = streamCommand.command;
        ymlog(" plexer[%s-V,s%u]: servicing stream chunk %ub",YMSTR(plexer->name),streamID, chunkLength);
    }
    
    while ( chunkLength > plexer->localPlexBufferSize )
    {
        plexer->localPlexBufferSize *= 2;
        ymerr(" plexer[%s-V,s%u] reallocating buffer to %ub",YMSTR(plexer->name),streamID,plexer->localPlexBufferSize);
        plexer->localPlexBuffer = realloc(plexer->localPlexBuffer, plexer->localPlexBufferSize);
    }
    
//okay =
    _YMStreamReadDown(servicingStream, plexer->localPlexBuffer, chunkLength);
//    if ( ! okay )
//    {
//        ymerr(" plexer[%s,V,s%u]: fatal: reading stream chunk size %u: %d (%s)",plexer->name, streamID,streamHeader.length,errno,strerror(errno));
//        abort();
//    }
    
    ymlog(" plexer[%s-V,s%u]: read stream chunk",YMSTR(plexer->name),streamID);
    
    YMPlexerMessage plexMessage = {closing ? YMPlexerCommandCloseStream : chunkLength, streamID };
    size_t plexMessageLen = sizeof(plexMessage);
    YMIOResult ioResult = YMWriteFull(plexer->outputFile, (void *)&plexMessage, plexMessageLen, NULL);
    if ( ioResult != YMIOSuccess )
    {
        ymerr(" plexer[%s-V,o%d!,s%u]: perror: failed writing plex message size %zub: %d (%s)",YMSTR(plexer->name),plexer->outputFile,streamID,plexMessageLen,errno,strerror(errno));
        return false;
    }
    
    if ( closing )
    {
        ymlog(" plexer[%s-V,s%u]: stream closing: %p (%s)",YMSTR(plexer->name),streamID,servicingStream,_YMStreamGetName(servicingStream));
        
        if ( streamIdx < 0 || ! lockAndList )
        {
            ymerr(" plexer[%s-V,s%u]: fatal: closing stream not found in lists",YMSTR(plexer->name),streamID);
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
            ymerr(" plexer[%s-V,i%d<->o%d,s%u]: fatal: internal check failed removing %u",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID,streamID);
            abort();
        }
        
        // see comment in _YMStreamClose, we have to let the client's close call return before actually freeing this object
//        YMLockLock(_YMStreamGetRetainLock(servicingStream));
//        {
//        }
//        YMLockUnlock(_YMStreamGetRetainLock(servicingStream));
//        _YMStreamDesignatedFree(servicingStream);
        ymerr("IS RETAIN RELEASE WORKING YET?");
        
        return true;
    }
    
    ymlog(" plexer[%s-V,o%d!,s%u]: wrote plex header",YMSTR(plexer->name),plexer->outputFile,streamID);
    
    ioResult = YMWriteFull(plexer->outputFile, plexer->localPlexBuffer, chunkLength, NULL);
    if ( ioResult != YMIOSuccess )
    {
        ymerr(" plexer[%s-V,o%d!,s%u]: perror: failed writing plex buffer %ub: %d (%s)",YMSTR(plexer->name),plexer->outputFile,streamID,plexMessage.command,errno,strerror(errno));
        return false;
    }
    
    ymlog(" plexer[%s-V,o%d!,s%u]: wrote plex buffer %ub",YMSTR(plexer->name),plexer->outputFile,streamID,plexMessage.command);
    return true;
}

void __ym_plexer_service_upstream_proc(void * ctx)
{
    __YMPlexerRef plexer = (__YMPlexerRef)ctx;
    ymlog(" plexer[%s-^-i%d->o%d]: upstream service thread entered",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile);
    
    YMIOResult result = YMIOSuccess;
    
    while ( ( result == YMIOSuccess ) && plexer->active )
    {
        bool streamClosing = false;
        size_t chunkLength = 0;
        YMPlexerMessage plexerMessage;
        
        result = YMReadFull(plexer->inputFile, (void *)&plexerMessage, sizeof(plexerMessage), NULL);
        if ( result != YMIOSuccess )
        {
            if ( __YMPlexerInterrupt(plexer) )
                ymerr(" plexer[%s-^,i%d!->o%d]: perror: failed reading plex header: %d (%s)",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,errno,strerror(errno));
            return;
        }
        else if ( plexerMessage.command == YMPlexerCommandCloseStream )
        {
            ymlog(" plexer[%s-^,i%d!->o%d,s%u] stream closing",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,plexerMessage.streamID);
            streamClosing = true;
        }
        else
        {
            chunkLength = plexerMessage.command;
            ymlog(" plexer[%s-^,i%d!->o%d,s%u] read plex header %zub",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,plexerMessage.streamID,chunkLength);
        }
        
        YMPlexerStreamID streamID = plexerMessage.streamID;
        YMStreamRef theStream = __YMPlexerGetOrCreateRemoteStreamWithID(plexer, streamID);
        if ( ! theStream )
        {
            if ( __YMPlexerInterrupt(plexer) )
                ymerr(" plexer[%s-^,i%d->o%d,s%u]: fatal: stream lookup",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID);
            break;
        }
        
        if ( streamClosing )
        {
            YMStringRef memberName = YMStringCreateWithFormat("%s-s%u-%s",YMSTR(plexer->name),streamID,YM_TOKEN_STR(__ym_plexer_notify_stream_closing), NULL);
            __YMPlexerDispatchFunctionWithName(plexer, theStream, plexer->eventDeliveryThread, __ym_plexer_notify_stream_closing, memberName);
            YMRelease(memberName);
            ymlog(" plexer[%s-^,s%u]: dispatched notify-closing", YMSTR(plexer->name), streamID);
            
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
            ymerr(" plexer[%s-^,i%d->o%d,s%u] reallocating plex buffer for %u",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID,plexer->remotePlexBufferSize);
            plexer->remotePlexBuffer = realloc(plexer->remotePlexBuffer, plexer->remotePlexBufferSize);
        }
        
        result = YMReadFull(plexer->inputFile, plexer->remotePlexBuffer, chunkLength, NULL);
        if ( result != YMIOSuccess )
        {
            if ( __YMPlexerInterrupt(plexer) )
                ymerr(" plexer[%s-^,i%d!->o%d,s%u]: perror: failed reading plex buffer of length %zub: %d (%s)",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID,chunkLength,errno,strerror(errno));
            break;
        }
        ymlog(" plexer[%s-^,i%d->o%d!,s%u] read plex buffer %zub",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID,chunkLength);
        
        int streamInFd = _YMStreamGetUpstreamWrite(theStream);
        ymlog(" plexer[%s-^,i%d->o%d,si%d!,s%u] writing plex buffer %zub",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamInFd,streamID,chunkLength);
        result = YMWriteFull(streamInFd, plexer->remotePlexBuffer, chunkLength, NULL);
        if ( result != YMIOSuccess )
        {
            if ( __YMPlexerInterrupt(plexer) )
                ymerr(" plexer[%s-^,i%d->o%d,si%d!,s%u]: internal fatal: failed writing plex buffer of length %zub: %d (%s)",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamInFd,streamID,chunkLength,errno,strerror(errno));
            break;
        }
        ymlog(" plexer[%s-^,i%d->o%d,si%d!,s%u] wrote plex buffer %zub",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamInFd,streamID,chunkLength);
    }
    
    ymlog(" plexer[%s-^,i%d->o%d]: upstream service thread exiting",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile);
}

YMStreamRef __YMPlexerGetOrCreateRemoteStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID)
{
    YMStreamRef theStream = NULL;
    
#pragma message "__YMPlexerChooseReadyStream should handle up (is it local or remote?) as well as down (which is the oldest signaled?)?"
    YMLockLock(plexer->localAccessLock);
    {
        theStream = (YMStreamRef)YMDictionaryGetItem(plexer->localStreamsByID,streamID);
        if ( theStream )
            ymlog(" plexer[%s,s%u]: found existing local stream",YMSTR(plexer->name),streamID);
    }
    YMLockUnlock(plexer->localAccessLock);
    
    if ( ! theStream )
    {
        YMLockLock(plexer->remoteAccessLock);
        {
            theStream = (YMStreamRef)YMDictionaryGetItem(plexer->remoteStreamsByID,streamID);
            if ( theStream )
                ymlog(" plexer[%s,s%u]: found existing remote stream",YMSTR(plexer->name),streamID);
        
            // new stream
            if ( ! theStream )
            {
                if ( streamID < plexer->remoteStreamIDMin || streamID > plexer->remoteStreamIDMax )
                {
                    if ( __YMPlexerInterrupt(plexer) )
                    {
                        ymerr(" plexer[%s-^,i%d->o%d,s%u]: internal fatal: stream id collision",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID);
                        //abort();
                    }
                    
                    YMLockUnlock(plexer->remoteAccessLock);
                    return NULL;
                }
                
                ymlog(" plexer[%s,s%u]: alloc and notify",YMSTR(plexer->name),streamID);
                
                YMStringRef memberName = YMStringCreateWithFormat("%s-^-%u",YMSTR(plexer->name),streamID,NULL);
                __ym_plexer_stream_user_info_ref userInfo = (__ym_plexer_stream_user_info_ref)YMALLOC(sizeof(__ym_plexer_stream_user_info));
                userInfo->streamID = streamID;
                userInfo->isLocallyOriginated = false;
#pragma message "RETAIN/RELEASE - these probably don't make sense anymore, get rid of them!"
                userInfo->retainLock = YMLockCreate(YMLockDefault,memberName);
                userInfo->isDeallocated = false;
                userInfo->isUserReleased = false;
                userInfo->isPlexerReleased = false;
                theStream = _YMStreamCreate(memberName, (ym_stream_user_info_ref)userInfo);
                YMRelease(memberName);
                _YMStreamSetDataAvailableCallback(theStream, __ym_plexer_stream_data_available_proc, plexer);
                
                YMDictionaryAdd(plexer->remoteStreamsByID, streamID, theStream);
                
                memberName = YMStringCreateWithFormat("%s-notify-new-%u",YMSTR(plexer->name),streamID,NULL);
                __YMPlexerDispatchFunctionWithName(plexer, theStream, plexer->eventDeliveryThread, __ym_plexer_notify_new_stream, memberName);
                YMRelease(memberName);
            }
        }    
        YMLockUnlock(plexer->remoteAccessLock);
    }
    
    return theStream;
}

void __ym_plexer_stream_data_available_proc(void *ctx)
{
    __YMPlexerRef plexer = (__YMPlexerRef)ctx;
    YMSemaphoreSignal(plexer->streamMessageSemaphore);
}

bool __YMPlexerInterrupt(__YMPlexerRef plexer)
{
    ymlog(" plexer[%s]: *** _YMPlexerInterrupt, todo",YMSTR(plexer->name));
    
    YMLockLock(plexer->interruptionLock);
    if ( ! plexer->active )
        return false;
    plexer->active = false;
    YMLockUnlock(plexer->interruptionLock);
    
    plexer->active = false;
    
#pragma message "how to maintain ownership when just running plexer test?"
//    int result; // YMConnection owns the socket
//    result = close(plexer->inputFile);
//    if ( result != 0 )
//        abort();
//    if ( plexer->inputFile != plexer->outputFile )
//    {
//        result = close(plexer->outputFile);
//        if ( result != 0 )
//            abort();
//    }
    
    __YMPlexerDispatchFunctionWithName(plexer, NULL, plexer->eventDeliveryThread, __ym_plexer_notify_interrupted, "plexer-interrupted");
    return ! plexer->stopped;
}

void YMPlexerStop(YMPlexerRef plexer_)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
#pragma message "how should this actually behave? join on remaining activity?"
    // deallocate volatile stuff
    
    free(plexer->localPlexBuffer);
    plexer->localPlexBuffer = NULL;
    
    free(plexer->remotePlexBuffer);
    plexer->remotePlexBuffer = NULL;
    
    plexer->active = false;
    plexer->stopped = true;
}

YMStreamRef YMPlexerCreateNewStream(YMPlexerRef plexer_, YMStringRef name, bool direct)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
    YMStreamRef newStream = NULL;
    YMLockLock(plexer->localAccessLock);
    do {
        if ( plexer->localStreamIDLast == plexer->localStreamIDMax )
            plexer->localStreamIDLast = plexer->localStreamIDMin;
        else
            plexer->localStreamIDLast++;
        
        YMPlexerStreamID newStreamID = plexer->localStreamIDLast;
        if ( YMDictionaryContains(plexer->localStreamsByID, newStreamID) )
        {
            ymerr(" plexer[%s]: fatal: plexer ran out of streams",YMSTR(plexer->name));
            __YMPlexerInterrupt(plexer);
            break;
        }
        
        if ( ! name ) name = YMSTRC("*");
        
        __ym_plexer_stream_user_info_ref userInfo = (__ym_plexer_stream_user_info_ref)YMALLOC(sizeof(__ym_plexer_stream_user_info));
        userInfo->name = YMRetain(name);
        userInfo->streamID = newStreamID;
        userInfo->isLocallyOriginated = true;
#pragma message "RETAIN/RELEASE - these probably don't make sense anymore, get rid of them!"
        userInfo->retainLock = YMLockCreate(YMLockDefault,name);
        userInfo->isDeallocated = false;
        userInfo->isUserReleased = false;
        userInfo->isPlexerReleased = false;
        if ( 0 != gettimeofday(userInfo->lastServiceTime, NULL) )
        {
            ymlog(" plexer[%s]: warning: error setting initial service time for stream: %d (%s)",YMSTR(plexer->name),errno,strerror(errno));
            YMGetTheBeginningOfPosixTimeForCurrentPlatform(userInfo->lastServiceTime);
        }
        
        YMStringRef memberName = YMStringCreateWithFormat("%s-V-%u:%s",YMSTR(plexer->name),newStreamID,YMSTR(name),NULL);
        newStream = _YMStreamCreate(memberName, (ym_stream_user_info_ref)userInfo);
        YMRelease(memberName);
        _YMStreamSetDataAvailableCallback(newStream, __ym_plexer_stream_data_available_proc, plexer);
        
        YMDictionaryAdd(plexer->localStreamsByID, newStreamID, newStream);
        
        if ( direct )
        {
            // ???
            // !!! there is no direct!!
        }
    } while (false);
    YMLockUnlock(plexer->localAccessLock);
    
    return newStream;
}

// void: if this fails, it's either a bug or user error
void YMPlexerCloseStream(YMPlexerRef plexer_, YMStreamRef stream)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
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
            ymerr(" plexer[%s]: error: user requested closure of remote stream %u",YMSTR(plexer->name),streamID);
        else
            ymerr(" plexer[%s]: error: user requested closure of unknown stream %u",YMSTR(plexer->name),streamID);
        
        abort();
    }
    
    _YMStreamClose(localStream);
    
    YMLockUnlock(theLock);
}

//void YMPlexerRemoteStreamRelease(__unused YMPlexerRef plexer, YMStreamRef stream)
//{
//    if ( _YMStreamIsLocallyOriginated(stream) )
//    {
//        ymlog(" plexer[%s]: remote releasing locally originated stream %u",YMSTR(plexer->name), _YMStreamGetUserInfo(stream)->streamID);
//        abort();
//    }
//    
//    _YMStreamRemoteSetUserReleased(stream);
//}

#pragma mark dispatch

void __YMPlexerDispatchFunctionWithName(__YMPlexerRef plexer, YMStreamRef stream, YMThreadRef targetThread, ym_thread_dispatch_func function, YMStringRef name)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = (__ym_dispatch_plexer_and_stream_ref)YMALLOC(sizeof(__ym_dispatch_plexer_and_stream));
    notifyDef->plexer = plexer;
    notifyDef->stream = stream;
    ym_thread_dispatch dispatch = { function, NULL, true, notifyDef, name };
    
//#define YMPLEXER_NO_EVENT_QUEUE // for debugging
#ifdef YMPLEXER_NO_EVENT_QUEUE
    __unused void *silence = targetThread;
    function(&dispatch);
#else
    YMThreadDispatchDispatch(targetThread, dispatch);
#endif
}

void __ym_plexer_notify_new_stream(ym_thread_dispatch_ref dispatch)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog(" plexer[%s,s%u] ym_notify_new_stream entered", YMSTR(plexer->name), streamID);
    plexer->newIncomingFunc(plexer,stream,plexer->callbackContext);
    ymlog(" plexer[%s,s%u] ym_notify_new_stream exiting", YMSTR(plexer->name), streamID);
}

void __ym_plexer_notify_stream_closing(ym_thread_dispatch_ref dispatch)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog(" plexer[%s,s%u] ym_notify_stream_closing entered", YMSTR(plexer->name), streamID);
    plexer->closingFunc(plexer,stream,plexer->callbackContext);
    
    YMLockLock(plexer->remoteAccessLock);
    {
        ymlog(" plexer[%s,s%u]: releasing stream %u",YMSTR(plexer->name),streamID,streamID);
        //_YMStreamRemoteSetPlexerReleased(stream);
        ymerr("IS RETAIN/RELEASE WORKING YET?");
        YMDictionaryRemove(plexer->remoteStreamsByID, streamID);
    }
    YMLockUnlock(plexer->remoteAccessLock);
    
    ymlog(" plexer[%s,s%u] ym_notify_stream_closing exiting",YMSTR(plexer->name), streamID);
}

// todo with the 'retain count' thing, i don't think this is necessary anymore
void __ym_plexer_release_remote_stream(ym_thread_dispatch_ref dispatch)
{
    _ym_dispatch_plexer_stream_def *notifyDef = (_ym_dispatch_plexer_stream_def *)dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    __unused YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog(" plexer[%s,s%u]: releasing stream %u",YMSTR(plexer->name),streamID,streamID);
    //_YMStreamRemoteSetPlexerReleased(stream);
    ymerr("IS RETAIN/RELEASE WORKING YET?");
}

void __ym_plexer_notify_interrupted(ym_thread_dispatch_ref dispatch)
{
    _ym_dispatch_plexer_stream_def *notifyDef = (_ym_dispatch_plexer_stream_def *)dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    
    ymlog(" plexer[%s] ym_notify_interrupted entered", YMSTR(plexer->name));
    plexer->interruptedFunc(plexer,plexer->callbackContext);
    ymlog(" plexer[%s] ym_notify_interrupted exiting", YMSTR(plexer->name));
}

