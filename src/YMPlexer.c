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
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMThread.h"

#define ymlog_type YMLogPlexer
#include "YMLog.h"

#ifdef USE_FTIME
#include <sys/timeb.h>
#error todo
#elif !defined(WIN32)
#include <sys/select.h>
#include <sys/time.h>
#include <pthread.h> // explicit for sigpipe
# if defined (RPI)
# include <signal.h>
# endif
#else
#include <winsock2.h> // gettimeofday
#endif

#define YMPlexerBuiltInVersion ((uint32_t)1)

#undef ymlog_type
#define ymlog_type YMLogPlexer

YM_EXTERN_C_PUSH

#define CHECK_REMOVE_RELEASE(local,streamID,remove) {   \
                                                YMLockLock(plexer->interruptionLock); bool _interrupted = ! plexer->active; YMLockUnlock(plexer->interruptionLock); \
                                                if ( ! _interrupted ) { \
                                                    YMLockRef _lock = local ? plexer->localAccessLock : plexer->remoteAccessLock; \
                                                    YMDictionaryRef _list = local ? plexer->localStreamsByID : plexer->remoteStreamsByID; \
                                                        YMLockLock(_lock); \
                                                        YMStreamRef _theStream; \
                                                        bool _okay = ( remove ? \
                                                                        ( ( _theStream = YMDictionaryRemove(_list,streamID) ) != NULL ) : \
                                                                          YMDictionaryContains(_list,streamID) ); \
                                                        if ( ! _okay ) { ymerr("plexer consistenty check failed"); abort(); } \
                                                        if ( remove ) { YMRelease(_theStream); } \
                                                    YMLockUnlock(_lock); } \
                                                }

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
typedef enum
{
    YMPlexerCommandClose = -1, // indicates to up/down threads to terminate
    YMPlexerCommandCloseStream = -2,
    YMPlexerCommandMin = YMPlexerCommandCloseStream
} YMPlexerCommand;

void YM_CALLING_CONVENTION __ym_plexer_notify_new_stream(ym_thread_dispatch_ref);
void YM_CALLING_CONVENTION __ym_plexer_notify_stream_closing(ym_thread_dispatch_ref);
void YM_CALLING_CONVENTION __ym_plexer_notify_interrupted(ym_thread_dispatch_ref);

// linked to 'private' definition in YMPlexerPriv
typedef struct __ym_plexer_stream_user_info_t
{
    YMStringRef name;
    YMPlexerStreamID streamID;
    
    bool isLocallyOriginated;
    struct timeval *lastServiceTime; // free me
    
    // get rid of select loop
    YMLockRef lock;
    uint64_t bytesAvailable;
    bool userClosed;
    
    uint64_t rawWritten;
    uint64_t muxerWritten;
    uint64_t rawRead;
    uint64_t muxerRead;
} __ym_plexer_stream_user_info_t;
typedef struct __ym_plexer_stream_user_info_t * __ym_plexer_stream_user_info_ref;

#undef YM_STREAM_INFO
#define YM_STREAM_INFO(x) ((__ym_plexer_stream_user_info_ref)_YMStreamGetUserInfo(x))

typedef struct {
    YMPlexerCommandType command;
    YMPlexerStreamID streamID;
} YMPlexerMessage;

typedef struct __ym_plexer_t
{
    _YMType _type;
    
    YMStringRef name;
    YMSecurityProviderRef provider;
    
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
} __ym_plexer_t;
typedef struct __ym_plexer_t *__YMPlexerRef;

// generic context pointer definition for "plexer & stream" entry points
typedef struct __ym_dispatch_plexer_stream_def
{
    __YMPlexerRef plexer;
    YMStreamRef stream;
} _ym_dispatch_plexer_stream_def;
typedef struct __ym_dispatch_plexer_stream_def __ym_dispatch_plexer_and_stream;
typedef __ym_dispatch_plexer_and_stream *__ym_dispatch_plexer_and_stream_ref;

YM_ONCE_DEF(__YMRegisterSigpipe);
void __ym_sigpipe_handler (int signum);
YMStreamRef __YMPlexerRetainReadyStream(__YMPlexerRef plexer);
void __YMPlexerDispatchFunctionWithName(__YMPlexerRef plexer, YMStreamRef stream, YMThreadRef targetThread, ym_thread_dispatch_func function, YMStringRef nameToRelease);
bool __YMPlexerStartServiceThreads(__YMPlexerRef plexer);
bool __YMPlexerDoInitialization(__YMPlexerRef plexer, bool master);
bool __YMPlexerInitAsMaster(__YMPlexerRef plexer);
bool __YMPlexerInitAsSlave(__YMPlexerRef plexer);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_downstream_proc(YM_THREAD_PARAM);
YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_upstream_proc(YM_THREAD_PARAM);
bool __YMPlexerServiceADownstream(__YMPlexerRef plexer, YMStreamRef servicingStream);
YMStreamRef __YMPlexerRetainOrCreateRemoteStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID);
YMStreamRef __YMPlexerCreateStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID, bool isLocal, YMStringRef userNameToRelease);
bool __YMPlexerInterrupt(__YMPlexerRef plexer, bool requested);
void __ym_plexer_stream_data_available_proc(YMStreamRef stream, uint32_t bytes, void *ctx);
void __ym_plexer_free_stream_info(YMStreamRef stream);

#define YMPlexerDefaultBufferSize 16384

YMPlexerRef YMPlexerCreate(YMStringRef name, YMSecurityProviderRef provider, bool master)
{
	YM_ONCE_DO_LOCAL(__YMRegisterSigpipe);
    
    __YMPlexerRef plexer = (__YMPlexerRef)_YMAlloc(_YMPlexerTypeID,sizeof(struct __ym_plexer_t));
    
    plexer->name = YMStringCreateWithFormat("plex-%s(%s)",name?YMSTR(name):"*",master?"m":"s",NULL);
    plexer->provider = YMRetain(provider);
    
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
    plexer->localServiceThread = YMThreadCreate(aString, __ym_plexer_service_downstream_proc, YMRetain(plexer));
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-up",YMSTR(plexer->name),NULL);
    plexer->remoteServiceThread = YMThreadCreate(aString, __ym_plexer_service_upstream_proc, YMRetain(plexer));
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-event",YMSTR(plexer->name),NULL);
    plexer->eventDeliveryThread = YMThreadDispatchCreate(aString);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-interrupt",YMSTR(plexer->name),NULL);
    plexer->interruptionLock = YMLockCreateWithOptionsAndName(YMInternalLockType,aString);
    YMRelease(aString);
    
    aString = YMStringCreateWithFormat("%s-down-signal",YMSTR(plexer->name),NULL);
    plexer->downstreamReadySemaphore = YMSemaphoreCreateWithName(aString,0);
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
    bool first = __YMPlexerInterrupt(plexer, false);
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
    
    // // free'd (but not null'd) in __Interrupt
    if ( plexer->eventDeliveryThread )
    YMRelease(plexer->eventDeliveryThread);
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
    
    ymlog(" plexer[%s]: started",YMSTR(plexer->name));
    
catch_fail:
    return okay;
}

YMStreamRef YMPlexerCreateStream(YMPlexerRef plexer_, YMStringRef name)
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
        
        YMStringRef userName = name ? YMRetain(name) : YMSTRC("*");
        newStream = __YMPlexerCreateStreamWithID(plexer, newStreamID, true, userName);
        
        YMDictionaryAdd(plexer->localStreamsByID, newStreamID, (void *)newStream);
        YMRetain(newStream); // retain for user
        
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
    
    ymassert(!userInfo->userClosed,"stream double-close"); // only catches local
    
    if ( plexer->active )
    {
        if ( isLocal )
        {
            userInfo->userClosed = true;
            // when stream commands were a thing
#ifdef USE_STREAM_COMMANDS
            _YMStreamSendClose(stream);
#else
            YMSemaphoreSignal(plexer->downstreamReadySemaphore);
#endif
        }
    }
    
    ymlog(" plexer[%s]: user %s stream %u", YMSTR(plexer->name), isLocal?"closing":"releasing", streamID);
    YMRelease(stream); // release user
}

bool YMPlexerStop(YMPlexerRef plexer_)
{
    __YMPlexerRef plexer = (__YMPlexerRef)plexer_;
    return __YMPlexerInterrupt(plexer,true);
}

#pragma mark internal

YM_ONCE_FUNC(__YMRegisterSigpipe,
{
    signal(SIGPIPE,__ym_sigpipe_handler);
})

void __ym_sigpipe_handler (__unused int signum)
{
    fprintf(stderr,"sigpipe happened\n");
}

const char YMPlexerMasterHello[] = "オス、王様でおるべし";
const char YMPlexerSlaveHello[] = "よろしくお願いいたします";

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
    char inHello[64];
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
    char inHello[64];
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

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_downstream_proc(YM_THREAD_PARAM ctx)
{
    __YMPlexerRef plexer = (__YMPlexerRef)ctx;
    //YMRetain(plexer); // retained on thread creation, matched at the end of this function
    
    ymlog(" plexer[%s]: downstream service thread entered",YMSTR(plexer->name));
    
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
        
        YMStreamRef servicingStream = __YMPlexerRetainReadyStream(plexer);
        
        ymlog(" plexer[%s-V]: signaled,",YMSTR(plexer->name));
        
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
            YMRelease(servicingStream);
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

	YM_THREAD_END
}

YMStreamRef __YMPlexerRetainReadyStream(__YMPlexerRef plexer)
{
    YMStreamRef oldestStream = NULL;
    struct timeval newestTime = {0,0};
    YMGetTheEndOfPosixTimeForCurrentPlatform(&newestTime);
    
#define __YMOutgoingListIdx 0
#define __YMIncomingListIdx 1
#define __YMListMax 2
#define __YMLockListIdx 0
#define __YMListListIdx 1
    YMTypeRef *list[] = { (YMTypeRef[]) { plexer->localAccessLock, plexer->localStreamsByID },
        (YMTypeRef[]) { plexer->remoteAccessLock, plexer->remoteStreamsByID } };
    
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
                
                struct timeval *thisStreamLastService = YM_STREAM_INFO(aStream)->lastServiceTime;
                if ( YMTimevalCompare(thisStreamLastService, &newestTime ) != GreaterThan )
                {
                    if ( oldestStream ) YMRelease(oldestStream);
                    oldestStream = YMRetain(aStream);
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
    
    if ( oldestStream ) YM_DEBUG_ASSERT_MALLOC(oldestStream);
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
    
    ymassert(bytesAvailable>0||closing,"a downstream state invalid");
    
    bytesRemaining = bytesAvailable;
    
    while ( bytesRemaining > 0 )
    {
        uint32_t chunkLength = 0;
        
        // consume any signals we read ahead
        //if ( chunksHandled )
        //    YMSemaphoreWait(plexer->downstreamReadySemaphore);
        
        // never realloc, chunk chunks
        chunkLength = (uint32_t)bytesRemaining;
        if ( chunkLength > 16384 )
            chunkLength = 16384;
        
        _YMStreamReadDown(stream, plexer->localPlexBuffer, chunkLength);
        ymlog(" plexer[%s-V,s%u]: read stream chunk",YMSTR(plexer->name),streamID);
        
        if ( bytesRemaining - chunkLength > bytesRemaining )
            abort();
        
        bytesRemaining -= chunkLength;
        chunksHandled++;
        
        YMPlexerMessage plexMessage = { chunkLength, streamID };
        size_t plexMessageLen = sizeof(plexMessage);
		bool okay = YMSecurityProviderWrite(plexer->provider, (uint8_t *)&plexMessage, plexMessageLen);
        if ( ! okay )
        {
            ymerr(" plexer[%s-V,s%u]: perror: failed writing plex message size %zub: %d (%s)",YMSTR(plexer->name),streamID,plexMessageLen,errno,strerror(errno));
            return false;
        }
        userInfo->muxerWritten += plexMessageLen;
        
        ymlog(" plexer[%s-V,s%u]: wrote plex header",YMSTR(plexer->name),streamID);
        
		okay = YMSecurityProviderWrite(plexer->provider, plexer->localPlexBuffer, chunkLength);
        if ( ! okay )
        {
            ymerr(" plexer[%s-V,s%u]: perror: failed writing plex buffer %ub: %d (%s)",YMSTR(plexer->name),streamID,plexMessage.command,errno,strerror(errno));
            return false;
        }
        userInfo->rawWritten += chunkLength;
        
        ymlog(" plexer[%s-V,s%u]: wrote plex buffer %ub",YMSTR(plexer->name),streamID,plexMessage.command);
    }
    
    ymlog(" plexer[%s-V,s%u]: flushed %llu chunk(s) and %llu bytes",YMSTR(plexer->name),streamID,chunksHandled,bytesAvailable);
    
    if ( closing )
    {
        if ( ! userInfo->isLocallyOriginated )
            abort();
        
        bool okay = true;
        YMPlexerMessage plexMessage = { YMPlexerCommandCloseStream, streamID };
        size_t plexMessageLen = sizeof(plexMessage);
        okay = YMSecurityProviderWrite(plexer->provider, (uint8_t *)&plexMessage, plexMessageLen);
        if ( ! okay )
            ymerr(" plexer[%s-V,s%u]: perror: failed writing plex message size %zub: %d (%s)",YMSTR(plexer->name),streamID,plexMessageLen,errno,strerror(errno));
        userInfo->muxerWritten += plexMessageLen;
        
        ymlog(" plexer[%s-V,s%u]: stream closing (rW%llu,pW%llu,rR%llu,pR%llu): %p (%s)",YMSTR(plexer->name),streamID,userInfo->rawWritten,userInfo->muxerRead,userInfo->rawRead,userInfo->muxerRead,stream,YMSTR(_YMStreamGetName(stream)));
        
        CHECK_REMOVE_RELEASE(true, streamID, true);
        
        return okay;
    }
    
    return true;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_plexer_service_upstream_proc(YM_THREAD_PARAM ctx)
{
    __YMPlexerRef plexer = (__YMPlexerRef)ctx;
    
    ymlog(" plexer[%s-^]: upstream service thread entered",YMSTR(plexer->name));
    
    bool okay = true;
    while ( okay && plexer->active )
    {
        bool streamClosing = false;
        size_t chunkLength = 0;
        YMPlexerMessage plexerMessage;
        
		okay = YMSecurityProviderRead(plexer->provider, (uint8_t *)&plexerMessage, sizeof(plexerMessage));
        if ( ! okay || plexerMessage.command < YMPlexerCommandMin || plexerMessage.command > UINT16_MAX )
        {
            if ( __YMPlexerInterrupt(plexer,false) )
                ymerr(" plexer[%s-^]: perror: failed reading plex header: %d (%s)",YMSTR(plexer->name),errno,strerror(errno));
            break;
        }
        else if ( plexerMessage.command == YMPlexerCommandCloseStream )
        {
            streamClosing = true;
        }
        else
        {
            chunkLength = plexerMessage.command;
            ymlog(" plexer[%s-^,s%u] read plex header %zub",YMSTR(plexer->name),plexerMessage.streamID,chunkLength);
        }
        
        YMPlexerStreamID streamID = plexerMessage.streamID;
        YMStreamRef theStream = __YMPlexerRetainOrCreateRemoteStreamWithID(plexer, streamID); // todo retain this until this iter is done
        if ( ! theStream )
        {
            if ( __YMPlexerInterrupt(plexer,false) )
                ymerr(" plexer[%s-^,s%u]: fatal: stream lookup",YMSTR(plexer->name),streamID);
            break;
        }
        
        __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(theStream);
        userInfo->muxerRead += sizeof(plexerMessage);
        
        if ( streamClosing )
        {
            YMStringRef memberName = YMStringCreateWithFormat("%s-s%u-%s",YMSTR(plexer->name),streamID,YM_TOKEN_STR(__ym_plexer_notify_stream_closing), NULL);
            // close 'read up', so that if client (or forward file) is reading unbounded data it will get signaled
            _YMStreamCloseWriteUp(theStream);
            __YMPlexerDispatchFunctionWithName(plexer, theStream, plexer->eventDeliveryThread, __ym_plexer_notify_stream_closing, memberName);
            ymlog(" plexer[%s-^,s%u] stream closing (rW%llu,pW%llu,rR%llu,pR%llu)",YMSTR(plexer->name),plexerMessage.streamID,userInfo->rawWritten,userInfo->muxerWritten,userInfo->rawRead,userInfo->muxerRead);
            YMRelease(theStream);
            continue;
        }
        
        ymassert(chunkLength<=UINT16_MAX&&chunkLength!=0,"upstream chuck length");
        
        okay = YMSecurityProviderRead(plexer->provider, plexer->remotePlexBuffer, chunkLength);
        if ( ! okay )
        {
            if ( __YMPlexerInterrupt(plexer,false) )
                ymerr(" plexer[%s-^,s%u]: perror: failed reading plex buffer of length %zub: %d (%s)",YMSTR(plexer->name),streamID,chunkLength,errno,strerror(errno));
            YMRelease(theStream);
            break;
        }
        userInfo->rawRead += chunkLength;
        ymlog(" plexer[%s-^,s%u] read plex buffer %zub",YMSTR(plexer->name),streamID,chunkLength);
        
        YMIOResult result = _YMStreamWriteUp(theStream, plexer->remotePlexBuffer, (uint32_t)chunkLength);
        if ( result != YMIOSuccess )
        {
            if ( __YMPlexerInterrupt(plexer,false) )
                ymerr(" plexer[%s-^,s%u]: internal fatal: failed writing plex buffer of length %zub: %d (%s)",YMSTR(plexer->name),streamID,chunkLength,errno,strerror(errno));
            YMRelease(theStream);
            break;
        }
        
        ymlog(" plexer[%s-^,s%u] wrote plex buffer %zub",YMSTR(plexer->name),streamID,chunkLength);
        YMRelease(theStream);
    }
    
    ymerr(" plexer[%s-^]: upstream service thread exiting",YMSTR(plexer->name));
    
    YMRelease(plexer);

	YM_THREAD_END
}

YMStreamRef __YMPlexerRetainOrCreateRemoteStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID)
{
    YMStreamRef theStream = NULL;
    
    YMLockLock(plexer->localAccessLock);
    {
        theStream = (YMStreamRef)YMDictionaryGetItem(plexer->localStreamsByID,streamID);
        if ( theStream )
        {
            ymlog(" plexer[%s,s%u]: found existing local stream",YMSTR(plexer->name),streamID);
            YMRetain(theStream);
        }
    }
    YMLockUnlock(plexer->localAccessLock);
    
    if ( ! theStream )
    {
        YMLockLock(plexer->remoteAccessLock);
        {
            theStream = (YMStreamRef)YMDictionaryGetItem(plexer->remoteStreamsByID,streamID);
            if ( theStream )
            {
                ymlog(" plexer[%s,s%u]: found existing remote stream",YMSTR(plexer->name),streamID);
                YMRetain(theStream);
            }
        
            // new stream
            if ( ! theStream )
            {
                if ( streamID < plexer->remoteStreamIDMin || streamID > plexer->remoteStreamIDMax )
                {
                    YMLockUnlock(plexer->remoteAccessLock);
                    
                    if ( __YMPlexerInterrupt(plexer,false) )
                        ymerr(" plexer[%s-^,s%u]: internal fatal: stream id collision",YMSTR(plexer->name),streamID);

                    
                    return NULL;
                }
                
                ymlog(" plexer[%s-^]: new incoming s%u, notifying",YMSTR(plexer->name),streamID);
                
                YMStringRef name = YMSTRC("remote");
                theStream = __YMPlexerCreateStreamWithID(plexer,streamID,false,name);
                
                YMDictionaryAdd(plexer->remoteStreamsByID, streamID, (void *)theStream);
                YMRetain(theStream); // retain for user (complements close)
                YMRetain(theStream); // retain for function
                
                name = YMStringCreateWithFormat("%s-notify-new-%u",YMSTR(plexer->name),streamID,NULL);
                __YMPlexerDispatchFunctionWithName(plexer, theStream, plexer->eventDeliveryThread, __ym_plexer_notify_new_stream, name);
            }
        }    
        YMLockUnlock(plexer->remoteAccessLock);
    }
    
    return theStream;
}

YMStreamRef __YMPlexerCreateStreamWithID(__YMPlexerRef plexer, YMPlexerStreamID streamID, bool isLocal, YMStringRef userNameToRelease)
{
    __ym_plexer_stream_user_info_ref userInfo = (__ym_plexer_stream_user_info_ref)YMALLOC(sizeof(struct __ym_plexer_stream_user_info_t));
    userInfo->streamID = streamID;
    userInfo->isLocallyOriginated = isLocal;
    userInfo->lastServiceTime = (struct timeval *)YMALLOC(sizeof(struct timeval));
    if ( 0 != gettimeofday(userInfo->lastServiceTime, NULL) )
    {
        ymlog(" plexer[%s]: warning: error setting initial service time for stream: %d (%s)",YMSTR(plexer->name),errno,strerror(errno));
        YMGetTheBeginningOfPosixTimeForCurrentPlatform(userInfo->lastServiceTime);
    }
    
    YMStringRef memberName = YMStringCreateWithFormat("%s-%s-s%u-%s",plexer->master?"m":"s",isLocal?">":"<",streamID,YMSTR(userNameToRelease),NULL);
    userInfo->lock = YMLockCreateWithOptionsAndName(YMInternalLockType, memberName);
    userInfo->bytesAvailable = 0;
    userInfo->userClosed = false;
    userInfo->rawWritten = 0;
    userInfo->muxerWritten = 0;
    userInfo->rawRead = 0;
    userInfo->muxerRead = 0;
    YMStreamRef theStream = _YMStreamCreate(memberName, (ym_stream_user_info_ref)userInfo, __ym_plexer_free_stream_info);
    YMRelease(memberName);
    _YMStreamSetDataAvailableCallback(theStream, __ym_plexer_stream_data_available_proc, plexer);
    
    YMRelease(userNameToRelease);
    
    return theStream;
}

void __ym_plexer_free_stream_info(YMStreamRef stream)
{
    __ym_plexer_stream_user_info_ref userInfo = YM_STREAM_INFO(stream);
    //ymlog("%s: %u",__FUNCTION__,userInfo->streamID);
    YMRelease(userInfo->lock);
    free(userInfo->lastServiceTime);
    free(userInfo);
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
    
    ymerr(" plexer[%s]: interrupted",YMSTR(plexer->name));
    
    YMSecurityProviderClose(plexer->provider);
    
    YMSemaphoreSignal(plexer->downstreamReadySemaphore);

	YMTypeRef *listOfLocksAndLists[] = { (YMTypeRef[]) { plexer->localAccessLock, plexer->localStreamsByID },
		(YMTypeRef[]) { plexer->remoteAccessLock, plexer->remoteStreamsByID } };

    for ( int i = 0; i < __YMListMax; i++ )
    {
        YMLockRef aLock = listOfLocksAndLists[i][__YMLockListIdx];
        YMDictionaryRef aList = listOfLocksAndLists[i][__YMListListIdx];
        YMLockLock(aLock);
        {
            while ( YMDictionaryGetCount(aList) > 0 )
            {
                YMDictionaryKey aRandomKey = YMDictionaryGetRandomKey(aList);
                YMStreamRef aStream = YMDictionaryGetItem(aList, aRandomKey);
                YMPlexerStreamID aStreamID = YM_STREAM_INFO(aStream)->streamID;
                YMDictionaryRemove(aList, aRandomKey);
                ymerr("plexer[%s]: hanging up stream %u",YMSTR(plexer->name),aStreamID);
                _YMStreamCloseWriteUp(aStream);
                YMRelease(aStream);
            }
        }
        YMLockUnlock(aLock);
    }
    
    // if the client stops us, they don't expect a callback
    if ( ! requested )
    {
        __YMPlexerDispatchFunctionWithName(plexer, NULL, plexer->eventDeliveryThread, __ym_plexer_notify_interrupted, YMSTRC("plexer-interrupted"));
    }
    
//    // also created on init, so long as we're locking might be redundant
//    if ( plexer->eventDeliveryThread )
//    {
//        // releasing a dispatch thread should cause YMThreadDispatch to set the stop flag
//        // in the context struct (allocated on creation, free'd by the exiting thread)
//        // allowing it to go away. don't null it, or we'd need another handshake on the
//        // thread referencing itself via plexer and exiting.
//        YMRelease(plexer->eventDeliveryThread);
//    }
    
    return true;
}

#pragma mark dispatch

void __YMPlexerDispatchFunctionWithName(__YMPlexerRef plexer, YMStreamRef stream, YMThreadRef targetThread, ym_thread_dispatch_func function, YMStringRef nameToRelease)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = (__ym_dispatch_plexer_and_stream_ref)YMALLOC(sizeof(__ym_dispatch_plexer_and_stream));
    notifyDef->plexer = (__YMPlexerRef)YMRetain(plexer);
    notifyDef->stream = stream ? YMRetain(stream) : NULL;
    struct ym_thread_dispatch_t dispatch = { function, NULL, true, notifyDef, nameToRelease };
    
#define YMPLEXER_NO_EVENT_QUEUE // for debugging
#ifdef YMPLEXER_NO_EVENT_QUEUE
    __unused const void *silence = targetThread;
    function(&dispatch);
#else
    YMThreadDispatchDispatch(targetThread, dispatch);
#endif
    
    YMRelease(nameToRelease);
}

void YM_CALLING_CONVENTION __ym_plexer_notify_new_stream(ym_thread_dispatch_ref dispatch)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog(" plexer[%s,s%u] ym_notify_new_stream entered", YMSTR(plexer->name), streamID);
    plexer->newIncomingFunc(plexer,stream,plexer->callbackContext);
    ymlog(" plexer[%s,s%u] ym_notify_new_stream exiting", YMSTR(plexer->name), streamID);
    
    YMRelease(plexer);
    YMRelease(stream);
    //YMRelease(dispatch->description); // done by ThreadDispatch
}

void YM_CALLING_CONVENTION __ym_plexer_notify_stream_closing(ym_thread_dispatch_ref dispatch)
{
    __ym_dispatch_plexer_and_stream_ref notifyDef = dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    YMStreamRef stream = notifyDef->stream;
    YMPlexerStreamID streamID = YM_STREAM_INFO(stream)->streamID;
    
    ymlog(" plexer[%s,s%u] ym_notify_stream_closing entered", YMSTR(plexer->name), streamID);
    plexer->closingFunc(plexer,stream,plexer->callbackContext);
    
    CHECK_REMOVE_RELEASE(false, streamID, true);
    
    ymlog(" plexer[%s,s%u]: released and removed outgoing %u",YMSTR(plexer->name),streamID,streamID);
    
    ymlog(" plexer[%s,s%u] ym_notify_stream_closing exiting",YMSTR(plexer->name), streamID);
    
    YMRelease(plexer);
    YMRelease(stream);
    //YMRelease(dispatch->description); // done by ThreadDispatch
}

void YM_CALLING_CONVENTION __ym_plexer_notify_interrupted(ym_thread_dispatch_ref dispatch)
{
    _ym_dispatch_plexer_stream_def *notifyDef = (_ym_dispatch_plexer_stream_def *)dispatch->context;
    __YMPlexerRef plexer = notifyDef->plexer;
    
    ymlog(" plexer[%s] ym_notify_interrupted entered", YMSTR(plexer->name));
    plexer->interruptedFunc(plexer,plexer->callbackContext);
    ymlog(" plexer[%s] ym_notify_interrupted exiting", YMSTR(plexer->name));
    
    YMRelease(plexer);
    //YMRelease(dispatch->description); // done by ThreadDispatch
}

YM_EXTERN_C_POP
