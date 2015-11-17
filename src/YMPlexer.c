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
#include "YMSemaphore.h"
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

#define CHECK_REMOVE(local,streamID,remove) {   YMLockLock(plexer->interruptionLock); bool interrupted = ! plexer->active; YMLockUnlock(plexer->interruptionLock); \
                                                if ( ! interrupted ) { \
                                                    YMLockRef lock = local ? plexer->localAccessLock : plexer->remoteAccessLock; \
                                                    YMDictionaryRef list = local ? plexer->localStreamsByID : plexer->remoteStreamsByID; \
                                                        YMLockLock(lock); \
                                                        bool okay = ( remove ? \
                                                                        ( YMDictionaryRemove(list,streamID) != NULL ) : \
                                                                          YMDictionaryContains(list,streamID) ); \
                                                        if ( ! okay ) { ymerr("plexer consistenty check failed"); abort(); } \
                                                    YMLockUnlock(lock); } }

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
    YMPlexerCommandClose = -1, // indicates to up/down threads to terminate
    YMPlexerCommandCloseStream = -2
} YMPlexerCommand;

void __ym_plexer_notify_new_stream(ym_thread_dispatch_ref);
void __ym_plexer_notify_stream_closing(ym_thread_dispatch_ref);
void __ym_plexer_notify_interrupted(ym_thread_dispatch_ref);

// linked to 'private' definition in YMPlexerPriv
typedef struct __ym_plexer_stream_user_info_def
{
    YMStringRef name;
    YMPlexerStreamID streamID;
    
    bool isLocallyOriginated;
    struct timeval *lastServiceTime; // free me
    
    // get rid of select loop
    YMLockRef lock;
    uint64_t bytesAvailable;
    bool userClosed;
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
    YMSemaphoreRef downstreamReadySemaphore;
    
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

static pthread_once_t gYMRegisterSigpipeOnce = PTHREAD_ONCE_INIT;
void __YMRegisterSigpipe();
void __ym_sigpipe_handler (int signum);
YMStreamRef __YMPlexerChooseReadyStream(__YMPlexerRef plexer, YMTypeRef **list, int *outReadyStreamsByIdx, int *outStreamListIdx, int *outStreamIdx);
void __YMPlexerDispatchFunctionWithName(__YMPlexerRef plexer, YMStreamRef stream, YMThreadRef targetThread, ym_thread_dispatch_func function, YMStringRef name);
bool __YMPlexerStartServiceThreads(__YMPlexerRef plexer);
bool __YMPlexerDoInitialization(__YMPlexerRef plexer, bool master);
bool __YMPlexerInitAsMaster(__YMPlexerRef plexer);
bool __YMPlexerInitAsSlave(__YMPlexerRef plexer);
void __ym_plexer_service_downstream_proc(void *);
void __ym_plexer_service_upstream_proc(void *);
bool __YMPlexerServiceADownstream(__YMPlexerRef plexer, YMStreamRef servicingStream);
YMStreamRef __YMPlexerGetOrCreateRemoteStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID);
YMStreamRef __YMPlexerCreateStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID, bool isLocal);
bool __YMPlexerInterrupt(__YMPlexerRef plexer, bool requested);
void __ym_plexer_stream_data_available_proc(YMStreamRef stream, uint32_t bytes, void *ctx);

#define YMPlexerDefaultBufferSize (1e+6)

#define __YMOutgoingListIdx 0
#define __YMIncomingListIdx 1
#define __YMListMax 2
#define __YMLockListIdx 0
#define __YMListListIdx 1
#define GET_STACK_LIST YMTypeRef *listOfLocksAndLists[] = { (YMTypeRef[]) { plexer->localAccessLock, plexer->localStreamsByID }, \
                                                            (YMTypeRef[]) { plexer->remoteAccessLock, plexer->remoteStreamsByID } };

YMPlexerRef YMPlexerCreateWithFullDuplexFile(YMStringRef name, int file, bool master)
{
    return YMPlexerCreate(name, file, file, master);
}

YMPlexerRef YMPlexerCreate(YMStringRef name, int inputFile, int outputFile, bool master)
{
    pthread_once(&gYMRegisterSigpipeOnce, __YMRegisterSigpipe);
    
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
    plexer->localAccessLock = YMLockCreateWithOptionsAndName(YMInternalLockType,aString);
    YMRelease(aString);
    plexer->localPlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->localPlexBuffer = YMALLOC(plexer->localPlexBufferSize);
    
    plexer->remoteStreamsByID = YMDictionaryCreate();
    aString = YMStringCreateWithFormat("%s-remote",YMSTR(plexer->name),NULL);
    plexer->remoteAccessLock = YMLockCreateWithOptionsAndName(YMInternalLockType,aString);
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
    plexer->interruptionLock = YMLockCreateWithOptionsAndName(YMInternalLockType,aString);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-down-signal",YMSTR(plexer->name),NULL);
    plexer->downstreamReadySemaphore = YMSemaphoreCreate(aString,0);
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
    
    // ensure that if we haven't been stopped, or interrupted, we hang up
    bool first = __YMPlexerInterrupt(plexer, true);
    ymerr("plexer[%s]: deallocating (%s)",YMSTR(plexer->name),first?"stopping":"already stopped");
    
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
    
    // free'd (but not null'd) in __Interrupt
    //if ( plexer->eventDeliveryThread )
    //    YMRelease(plexer->eventDeliveryThread);
    YMRelease(plexer->interruptionLock);
    YMRelease(plexer->downstreamReadySemaphore);
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
    if ( ((_YMTypeRef)provider)->__type != _YMSecurityProviderTypeID )
        ymlog(" plexer[%s]: warning: %s: provider is type '%c'",YMSTR(plexer->name),__FUNCTION__,((_YMTypeRef)provider)->__type);
    plexer->provider = (YMSecurityProviderRef)YMRetain(provider);
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

YMStreamRef YMPlexerCreateNewStream(YMPlexerRef plexer_, YMStringRef name)
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
            __YMPlexerInterrupt(plexer,false);
            break;
        }
        
        if ( ! name ) name = YMSTRC("*");
        
        newStream = __YMPlexerCreateStreamWithID(plexer, newStreamID, true);
        YMDictionaryAdd(plexer->localStreamsByID, newStreamID, newStream);
        
    } while (false);
    YMLockUnlock(plexer->localAccessLock);
    
    return newStream;
}

// void: if this fails, it's either a bug or user error
void YMPlexerCloseStream(YMPlexerRef plexer_, YMStreamRef stream)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    
    __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;
    bool isLocal = userInfo->isLocallyOriginated;
    
    if ( plexer->active )
    {
        if ( isLocal )
        {
            CHECK_REMOVE(true, streamID, false);
            
            // when stream commands were a thing
#ifdef USE_STREAM_COMMANDS
            _YMStreamSendClose(stream);
#else
            userInfo->userClosed = true;
            YMSemaphoreSignal(plexer->downstreamReadySemaphore);
#endif
        }
        // else { // can't assert this for local streams, we might race upstream noticing the remote close command. this is only called for remote streams to release 'user' retain
//        YMLockLock(plexer->remoteAccessLock);
//            dictStream = (YMStreamRef)YMDictionaryGetItem(plexer->remoteStreamsByID, streamID);
//        YMLockUnlock(plexer->remoteAccessLock);
    }
    
    ymlog(" plexer[%s]: user %s stream %u",YMSTR(plexer->name),isLocal?"closing":"releasing",streamID);
}

bool YMPlexerStop(YMPlexerRef plexer_)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    return __YMPlexerInterrupt(plexer,true);
}

#pragma mark internal

void __YMRegisterSigpipe()
{
    signal(SIGPIPE,__ym_sigpipe_handler);
}

void __ym_sigpipe_handler (__unused int signum)
{
    ymerr("sigpipe happened");
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
    YMRetain(plexer);
    
    ymlog(" plexer[%s]: downstream service thread entered",YMSTR(plexer->name));
    
    GET_STACK_LIST
    
    while( plexer->active )
    {
        ymlog(" plexer[%s-V]: awaiting signal",YMSTR(plexer->name));
        // there is only one thread consuming this semaphore, so i think it's ok not to actually lock around this loop iteration
        YMSemaphoreWait(plexer->downstreamReadySemaphore);
        
        if ( ! plexer->active )
        {
            ymerr("plexer[%s-V] signaled to exit",YMSTR(plexer->name));
            break;
        }
        
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
                //ymlog(" plexer[%s-V]: coalescing signals",YMSTR(plexer->name));
                continue;
            }
            
            YM_DEBUG_ASSERT_MALLOC(servicingStream);
            bool okay = __YMPlexerServiceADownstream(plexer, servicingStream);
            if ( ! okay )
            {
                if ( __YMPlexerInterrupt(plexer,false) )
                    ymerr(" plexer[%s-V]: perror: service downstream failed",YMSTR(plexer->name));
                break;
            }
        }
    }
    
    ymerr(" plexer[%s]: downstream service thread exiting",YMSTR(plexer->name));
    YMRelease(plexer);
}

YMStreamRef __YMPlexerChooseReadyStream(__YMPlexerRef plexer, YMTypeRef **list, int *outReadyStreamsByIdx, int *outListIdx, int *outStreamIdx)
{
    YMStreamRef oldestStream = NULL;
    struct timeval newestTime = {0,0};
    YMGetTheEndOfPosixTimeForCurrentPlatform(&newestTime);
    
    int streamIdx;
    int listIdx = 0;
    for( ; listIdx < __YMListMax; listIdx++ )
    {
        YMLockRef aLock = (YMLockRef)list[listIdx][__YMLockListIdx];
        YMDictionaryRef aStreamsById = (YMDictionaryRef)list[listIdx][__YMListListIdx];
        
        YMLockLock(aLock);
        {
            streamIdx = 0;
            YMDictionaryEnumRef aStreamsEnum = YMDictionaryEnumeratorBegin(aStreamsById);
            YMDictionaryEnumRef aStreamsEnumPrev = NULL;
            while ( aStreamsEnum )
            {
                YMStreamRef aStream = (YMStreamRef)aStreamsEnum->value;
                //YM_DEBUG_ABORT_IF_BOGUS(oldestStream);
                
                __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(aStream);
                YMPlexerStreamID aStreamID = userInfo->streamID;
                
                ymlog(" plexer[%s-choose]: considering %s downstream %u",YMSTR(plexer->name),listIdx==__YMOutgoingListIdx?"local":"remote",aStreamID);
                
                // we are the only consumer of bytesAvailable, no need to lock here, but we will when we actually 'consume' them later on
                uint64_t aStreamBytesAvailable = 0;
                if ( userInfo->userClosed )
                {
                    ymlog(" plexer[%s-choose]: stream %u is closing",YMSTR(plexer->name),aStreamID);
                    if ( ! userInfo->isLocallyOriginated )
                        abort();
                }
                else
                {
                    aStreamBytesAvailable = YM_STREAM_INFO(aStream)->bytesAvailable;
                    if ( aStreamBytesAvailable == 0 )
                    {
                        ymlog(" plexer[%s-choose]: stream %u is empty",YMSTR(plexer->name),aStreamID);
                        goto catch_continue;
                    }
                }
                
                ymlog(" plexer[%s-choose]: %s stream %u is ready! %s%llub",YMSTR(plexer->name),listIdx==__YMOutgoingListIdx?"local":"remote",aStreamID,userInfo->userClosed?"(closing) ":"",aStreamBytesAvailable);
                
                if ( outReadyStreamsByIdx )
                    outReadyStreamsByIdx[listIdx]++;
                
                struct timeval *thisStreamLastService = YM_STREAM_INFO(aStream)->lastServiceTime;
                if ( YMTimevalCompare(thisStreamLastService, &newestTime ) != GreaterThan )
                {
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
    
    YM_DEBUG_ASSERT_MALLOC(oldestStream);
    return oldestStream;
}

bool __YMPlexerServiceADownstream(__YMPlexerRef plexer, YMStreamRef stream)
{
    YM_DEBUG_ASSERT_MALLOC(stream);
    
    // update last service time on stream
    __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(stream);
    if ( 0 != gettimeofday(userInfo->lastServiceTime, NULL) )
    {
        ymerr(" plexer[%s-V,V]: warning: error setting initial service time for stream: %d (%s)",YMSTR(plexer->name),errno,strerror(errno));
        YMGetTheBeginningOfPosixTimeForCurrentPlatform(userInfo->lastServiceTime);
    }
    
    YMPlexerStreamID streamID = userInfo->streamID;
    
    bool closing = userInfo->userClosed;
    uint64_t bytesAvailable,
            bytesRemaining,
            chunksHandled = 0;
    
    YMLockLock(userInfo->lock);
        bytesAvailable = userInfo->bytesAvailable;
        userInfo->bytesAvailable = 0;
    YMLockUnlock(userInfo->lock);
    
    ymlog(" plexer[%s-V,s%u]: will flush %llu bytes",YMSTR(plexer->name),streamID,bytesAvailable);
    
    if ( bytesAvailable == 0 && ! closing )
        abort();
    
    if ( closing )
    {
        ymlog(" plexer[%s-V,s%u]: stream closing: %p (%s)",YMSTR(plexer->name),streamID,stream,YMSTR(_YMStreamGetName(stream)));
        
        if ( ! userInfo->isLocallyOriginated )
            abort();
        
        YMPlexerMessage plexMessage = { YMPlexerCommandCloseStream, streamID };
        size_t plexMessageLen = sizeof(plexMessage);
        YMIOResult ioResult = YMWriteFull(plexer->outputFile, (void *)&plexMessage, plexMessageLen, NULL);
        if ( ioResult != YMIOSuccess )
        {
            ymerr(" plexer[%s-V,o%d!,s%u]: perror: failed writing plex message size %zub: %d (%s)",YMSTR(plexer->name),plexer->outputFile,streamID,plexMessageLen,errno,strerror(errno));
            return false;
        }
        
        CHECK_REMOVE(true, streamID, true);
        
        ymlog(" plexer[%s-V,o%d!,s%u]: wrote close command and removed from list",YMSTR(plexer->name),plexer->outputFile,streamID);
        
        YMRelease(stream); // we float retains until the end of all dispatched methods on streams, this is the final release and dealloc
        
        return true;
    }
    
    bytesRemaining = bytesAvailable;
    
    while ( bytesRemaining > 0 )
    {
        uint32_t chunkLength = 0;
        
        // consume any signals we read ahead
        //if ( chunksHandled )
        //    YMSemaphoreWait(plexer->downstreamReadySemaphore);
        
#ifdef USE_STREAM_COMMANDS_DOWN
        YMStreamHeader header;
        YMIOResult result = _YMStreamReadDown(stream, (void *)&header, sizeof(header));
        if ( result != YMIOSuccess )
        {
            ymlog(" plexer[%s-V,s%u] failed reading stream command",YMSTR(plexer->name),streamID);
            return false;
        }
        if ( header.command < 0 ) // handle command
        {
            if ( header.command == YMStreamClose )
            {
                ymlog(" plexer[%s-V,s%u]: stream command close",YMSTR(plexer->name),streamID);
                closing = true;
                chunkLength = 0;
            }
        }
        else if ( header.command > 0 )
        {
            chunkLength = header.command;
            ymlog(" plexer[%s-V,s%u]: servicing stream chunk %ub",YMSTR(plexer->name),streamID, chunkLength);
        }
        else
        {
            ymerr(" plexer[%s-V,s%u]: fatal: invalid command: %d",YMSTR(plexer->name),streamID,header.command);
            abort();
        }
#else
        // todo never realloc, chunk chunks
        if ( bytesAvailable > UINT32_MAX )
            abort();
        chunkLength = (uint32_t)bytesAvailable;
#endif
        
        while ( chunkLength > plexer->localPlexBufferSize )
        {
            plexer->localPlexBufferSize *= 2;
            ymerr(" plexer[%s-V,s%u] reallocating buffer to %ub",YMSTR(plexer->name),streamID,plexer->localPlexBufferSize);
            plexer->localPlexBuffer = realloc(plexer->localPlexBuffer, plexer->localPlexBufferSize);
        }
        
        _YMStreamReadDown(stream, plexer->localPlexBuffer, chunkLength);
        ymlog(" plexer[%s-V,s%u]: read stream chunk",YMSTR(plexer->name),streamID);
        
        if ( bytesRemaining - chunkLength > bytesRemaining )
            abort();
        
        bytesRemaining -= chunkLength;
        chunksHandled++;
        
        YMPlexerMessage plexMessage = { chunkLength, streamID };
        size_t plexMessageLen = sizeof(plexMessage);
        YMIOResult ioResult = YMWriteFull(plexer->outputFile, (void *)&plexMessage, plexMessageLen, NULL);
        if ( ioResult != YMIOSuccess )
        {
            ymerr(" plexer[%s-V,o%d!,s%u]: perror: failed writing plex message size %zub: %d (%s)",YMSTR(plexer->name),plexer->outputFile,streamID,plexMessageLen,errno,strerror(errno));
            return false;
        }
        
        ymlog(" plexer[%s-V,o%d!,s%u]: wrote plex header",YMSTR(plexer->name),plexer->outputFile,streamID);
        
        ioResult = YMWriteFull(plexer->outputFile, plexer->localPlexBuffer, chunkLength, NULL);
        if ( ioResult != YMIOSuccess )
        {
            ymerr(" plexer[%s-V,o%d!,s%u]: perror: failed writing plex buffer %ub: %d (%s)",YMSTR(plexer->name),plexer->outputFile,streamID,plexMessage.command,errno,strerror(errno));
            return false;
        }
        
        ymlog(" plexer[%s-V,o%d!,s%u]: wrote plex buffer %ub",YMSTR(plexer->name),plexer->outputFile,streamID,plexMessage.command);
    }
    
    ymlog(" plexer[%s-V,s%u]: flushed %llu chunk(s) and %llu bytes",YMSTR(plexer->name),streamID,chunksHandled,bytesAvailable);
    
    return true;
}

void __ym_plexer_service_upstream_proc(void * ctx)
{
    __YMPlexerRef plexer = (__YMPlexerRef)ctx;
    YMRetain(plexer);
    
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
            if ( __YMPlexerInterrupt(plexer,false) )
                ymerr(" plexer[%s-^,i%d!->o%d]: perror: failed reading plex header: %d (%s)",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,errno,strerror(errno));
            break;
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
            if ( __YMPlexerInterrupt(plexer,false) )
                ymerr(" plexer[%s-^,i%d->o%d,s%u]: fatal: stream lookup",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID);
            break;
        }
        
        if ( streamClosing )
        {
            YMStringRef memberName = YMStringCreateWithFormat("%s-s%u-%s",YMSTR(plexer->name),streamID,YM_TOKEN_STR(__ym_plexer_notify_stream_closing), NULL);
            __YMPlexerDispatchFunctionWithName(plexer, theStream, plexer->eventDeliveryThread, __ym_plexer_notify_stream_closing, memberName);
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
            if ( __YMPlexerInterrupt(plexer,false) )
                ymerr(" plexer[%s-^,i%d!->o%d,s%u]: perror: failed reading plex buffer of length %zub: %d (%s)",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID,chunkLength,errno,strerror(errno));
            break;
        }
        ymlog(" plexer[%s-^,i%d->o%d!,s%u] read plex buffer %zub",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID,chunkLength);
        
        result = _YMStreamWriteUp(theStream, plexer->remotePlexBuffer, (uint32_t)chunkLength);
        if ( result != YMIOSuccess )
        {
            if ( __YMPlexerInterrupt(plexer,false) )
                ymerr(" plexer[%s-^,i%d->o%d,s%u]: internal fatal: failed writing plex buffer of length %zub: %d (%s)",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID,chunkLength,errno,strerror(errno));
            break;
        }
        ymlog(" plexer[%s-^,i%d->o%d,s%u] wrote plex buffer %zub",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID,chunkLength);
    }
    
    ymerr(" plexer[%s-^,i%d->o%d]: upstream service thread exiting",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile);
    
    YMRelease(plexer);
}

YMStreamRef __YMPlexerGetOrCreateRemoteStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID)
{
    YMStreamRef theStream = NULL;
    
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
                    YMLockUnlock(plexer->remoteAccessLock);
                    
                    if ( __YMPlexerInterrupt(plexer,false) )
                        ymerr(" plexer[%s-^,i%d->o%d,s%u]: internal fatal: stream id collision",YMSTR(plexer->name),plexer->inputFile,plexer->outputFile,streamID);

                    
                    return NULL;
                }
                
                ymlog(" plexer[%s-^]: new incoming s%u, notifying",YMSTR(plexer->name),streamID);
                
                theStream = __YMPlexerCreateStreamWithID(plexer,streamID, false);
                YMDictionaryAdd(plexer->remoteStreamsByID, streamID, theStream);
                
                YMStringRef memberName = YMStringCreateWithFormat("%s-notify-new-%u",YMSTR(plexer->name),streamID,NULL);
                __YMPlexerDispatchFunctionWithName(plexer, theStream, plexer->eventDeliveryThread, __ym_plexer_notify_new_stream, memberName);
            }
        }    
        YMLockUnlock(plexer->remoteAccessLock);
    }
    
    return theStream;
}

YMStreamRef __YMPlexerCreateStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID, bool isLocal)
{
    YMStringRef memberName = YMStringCreateWithFormat("%s-^-%u",YMSTR(plexer->name),streamID,NULL);
    __ym_plexer_stream_user_info_ref userInfo = (__ym_plexer_stream_user_info_ref)YMALLOC(sizeof(__ym_plexer_stream_user_info));
    userInfo->streamID = streamID;
    userInfo->isLocallyOriginated = isLocal;
    userInfo->lastServiceTime = (struct timeval *)YMALLOC(sizeof(struct timeval));
    if ( 0 != gettimeofday(userInfo->lastServiceTime, NULL) )
    {
        ymlog(" plexer[%s]: warning: error setting initial service time for stream: %d (%s)",YMSTR(plexer->name),errno,strerror(errno));
        YMGetTheBeginningOfPosixTimeForCurrentPlatform(userInfo->lastServiceTime);
    }
    userInfo->lock = YMLockCreateWithOptionsAndName(YMInternalLockType, memberName);
    userInfo->bytesAvailable = 0;
    userInfo->userClosed = false;
    YMStreamRef theStream = _YMStreamCreate(memberName, (ym_stream_user_info_ref)userInfo);
    YMRelease(memberName);
    _YMStreamSetDataAvailableCallback(theStream, __ym_plexer_stream_data_available_proc, plexer);
    
    return theStream;
}

void __ym_plexer_stream_data_available_proc(YMStreamRef stream, uint32_t bytes, void *ctx)
{
    __YMPlexerRef plexer = (__YMPlexerRef)ctx;
    __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(stream);
    YMPlexerStreamID streamID = userInfo->streamID;
    ymlog("plexer[%s]: stream %u reports it has %u bytes ready !!!!!",YMSTR(plexer->name),streamID,bytes);
    YMLockLock(userInfo->lock);
        userInfo->bytesAvailable += bytes;
    YMLockUnlock(userInfo->lock);
    YMSemaphoreSignal(plexer->downstreamReadySemaphore);
}

// tears down the plexer, returning whether or not this was the 'first call' to interrupted
// (i/o calls should treat their errors as 'real', and subsequent errors on other threads
// can return quietly)
bool __YMPlexerInterrupt(__YMPlexerRef plexer, bool requested)
{
    bool firstInterrupt = false;
    YMLockLock(plexer->interruptionLock);
    {
        firstInterrupt = plexer->active;
        
        // before closing fds to ensure up/down threads wake up and exit, flag them
        plexer->active = false;
    }
    YMLockUnlock(plexer->interruptionLock);
    
    if ( ! firstInterrupt )
        return false;
    
    ymlog(" plexer[%s]: interrupted",YMSTR(plexer->name));
    
    // fds are set on Create, so they should always be 'valid' (save user error)
    // so given that we're in a lock here, this check might not be necessary
    // these closes() will cause the upstream thread to exit
    if ( plexer->inputFile >= 0 )
    {
        bool ioSame = plexer->inputFile == plexer->outputFile;
        int result = close(plexer->inputFile);
        if ( result != 0 )
            ymerr("plexer[%s]: error: close input failed: %d (%s)",YMSTR(plexer->name),errno,strerror(errno));
        else
            ymlog("plexer[%s]: closed input file %d",YMSTR(plexer->name),plexer->inputFile);
        if ( ! ioSame )
        {
            result = close(plexer->outputFile);
            if ( result != 0 )
                ymerr("plexer[%s]: error: close output failed: %d (%s)",YMSTR(plexer->name),errno,strerror(errno));
            else
                ymlog("plexer[%s]: closed output file %d",YMSTR(plexer->name),plexer->outputFile);
        }
        
        // let the downstream thread wake once more to exit
        YMSemaphoreSignal(plexer->downstreamReadySemaphore);
        
        plexer->inputFile = -1;
        plexer->outputFile = -1;
    }
    
    GET_STACK_LIST
    for ( int i = 0; i < __YMListMax; i++ )
    {
        YMLockRef aLock = listOfLocksAndLists[i][__YMLockListIdx];
        YMDictionaryRef aList = listOfLocksAndLists[i][__YMListListIdx];
        YMLockLock(aLock);
        {
            while ( YMDictionaryGetCount(aList) > 0 )
            {
                YMDictionaryKey randomKey = YMDictionaryGetRandomKey(aList);
                __unused YMStreamRef aStream = YMDictionaryRemove(aList, randomKey);
                ymlog("plexer[%s]: hanging up stream %u",YMSTR(plexer->name),YM_STREAM_INFO(aStream)->streamID);
                _YMStreamCloseReadUpFile(aStream); // "readup" :/ todo still a free race here
                //YMRelease(aStream);
            }
        }
        YMLockUnlock(aLock);
    }
    
    // if the client stops us, they don't expect a callback
    if ( ! requested )
    {
        __YMPlexerDispatchFunctionWithName(plexer, NULL, plexer->eventDeliveryThread, __ym_plexer_notify_interrupted, YMSTRC("plexer-interrupted"));
    }
    
    // also created on init, so long as we're locking might be redundant
    if ( plexer->eventDeliveryThread )
    {
        // releasing a dispatch thread should cause YMThreadDispatch to set the stop flag
        // in the context struct (allocated on creation, free'd by the exiting thread)
        // allowing it to go away. don't null it, or we'd need another handshake on the
        // thread referencing itself via plexer and exiting.
        YMRelease(plexer->eventDeliveryThread);
    }
    
    return true;
}

#pragma mark dispatch

void __YMPlexerDispatchFunctionWithName(__YMPlexerRef plexer, YMStreamRef stream, YMThreadRef targetThread, ym_thread_dispatch_func function, YMStringRef name)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = (__ym_dispatch_plexer_and_stream_ref)YMALLOC(sizeof(__ym_dispatch_plexer_and_stream));
    notifyDef->plexer = plexer;
    notifyDef->stream = stream ? YMRetain(stream) : NULL;  // retain stream until dispatch function completes
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
    
    YMRelease(stream);
    YMRelease(dispatch->description);
}

void __ym_plexer_notify_stream_closing(ym_thread_dispatch_ref dispatch)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog(" plexer[%s,s%u] ym_notify_stream_closing entered", YMSTR(plexer->name), streamID);
    plexer->closingFunc(plexer,stream,plexer->callbackContext);
    
    CHECK_REMOVE(false, streamID, true);
    
    YMRelease(stream); // this matches our retain-for-dispatch
    YMRelease(stream); // this is our initial retain and where deallocation happens, assuming client isn't still retaining it
    ymlog(" plexer[%s,s%u]: released and removed outgoing %u",YMSTR(plexer->name),streamID,streamID);
    
    ymlog(" plexer[%s,s%u] ym_notify_stream_closing exiting",YMSTR(plexer->name), streamID);
    
    YMRelease(dispatch->description);
}

void __ym_plexer_notify_interrupted(ym_thread_dispatch_ref dispatch)
{
    _ym_dispatch_plexer_stream_def *notifyDef = (_ym_dispatch_plexer_stream_def *)dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    
    ymlog(" plexer[%s] ym_notify_interrupted entered", YMSTR(plexer->name));
    plexer->interruptedFunc(plexer,plexer->callbackContext);
    ymlog(" plexer[%s] ym_notify_interrupted exiting", YMSTR(plexer->name));
    
    YMRelease(dispatch->description);
}

