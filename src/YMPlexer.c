//
//  YMPlexer.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPlexer.h"
#include "YMPrivate.h"

#include "YMSecurityProvider.h"
#include "YMDictionary.h"
#include "YMLock.h"
#include "YMThreads.h"

#define YMPlexerBuiltInVersion ((uint32_t)1)

typedef struct {
    uint32_t protocolVersion;
    uint32_t masterStreamIDMin;
    uint32_t masterStreamIDMax;
} YMPlexerMasterInitializer;

typedef struct {
    uint32_t protocolVersion;
} YMPlexerSlaveAck;

typedef uint32_t YMPlexerStreamID;

bool _YMPlexerStartServiceThreads(YMPlexerRef plexer);
bool _YMPlexerDoInitialization(YMPlexerRef plexer, bool master);
void *_YMPlexerLocalServiceThread(void *context);
void *_YMPlexerRemoteServiceThread(void *context);

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
    size_t localPlexBufferSize;
    YMPlexerStreamID localStreamIDMin;
    YMPlexerStreamID localStreamIDMax;
    YMPlexerStreamID localStreamIDLast;
    
    // the upstream
    YMDictionaryRef remoteStreamsByID;
    YMLockRef remoteAccessLock;
    uint8_t *remotePlexBuffer;
    size_t remotePlexBufferSize;
    YMPlexerStreamID remoteStreamIDMin;
    YMPlexerStreamID remoteStreamIDMax;
    
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
    
    plexer->localServiceThread = YMThreadCreate(_YMPlexerLocalServiceThread, plexer);
    plexer->remoteServiceThread = YMThreadCreate(_YMPlexerRemoteServiceThread, plexer);
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
        plexer->localStreamIDMax = UINT32_MAX / 2;
        plexer->localStreamIDLast = plexer->localStreamIDMax;
        plexer->remoteStreamIDMin = plexer->localStreamIDMax + 1;
        plexer->remoteStreamIDMax = UINT32_MAX;
        
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
        plexer->localStreamIDMax = UINT32_MAX;
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

void *_YMPlexerLocalServiceThread(void *context)
{
    YMPlexerRef plexer = (YMPlexerRef)context;
    YMLog("YMPlexer (%s) has enteret its local service thread",plexer->master?"master":"slave");
    while(1)
    {
        YMLog("YMPlexer (%s) V data available wait...",plexer->master?"master":"slave");
        YMSemaphoreWait(plexer->localDataAvailableSemaphore);
        YMLog("YMPlexer (%s) has woken up",plexer->master?"master":"slave");
    }
    return NULL;
}

void *_YMPlexerRemoteServiceThread(void *context)
{
    YMPlexerRef plexer = (YMPlexerRef)context;
    YMLog("YMPlexer (%s) has enteret its remote service thread and is going to sleep",plexer->master?"master":"slave");
    sleep(1e+6);//todo receive signal from 'underlying medium'
    return NULL;
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

YMStreamRef YMPlexerNewStream(YMPlexerRef plexer, char *name, bool direct)
{
    YMLockLock(plexer->localAccessLock);
    YMPlexerStreamID newStreamID = ( plexer->localStreamIDLast == plexer->localStreamIDMax ) ? plexer->localStreamIDMin : ++(plexer->localStreamIDLast);
    YMPlexerStreamID *userInfo = (YMPlexerStreamID *)malloc(sizeof(YMPlexerStreamID));
    *userInfo = newStreamID;
    YMStreamRef newStream = YMStreamCreate(name);
    _YMStreamSetUserInfo(newStream, userInfo);
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

void YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream)
{
    YMPlexerStreamID streamID = *( (YMPlexerStreamID *)_YMStreamGetUserInfo(stream) );
    
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
            YMLog("fatal: YMPlexer user requested closure of remote stream %llu",streamID);
        else
            YMLog("fatal: YMPlexer user requested closure of unknown stream %llu",streamID);
        abort();
        return;
    }
    
    YMLog("local stream %llu marked closed...",streamID);
    YMStreamClose(localStream);
    // local service thread to deallocate after it's able to flush data and pass off the close command to remote
}
