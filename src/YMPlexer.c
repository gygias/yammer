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

typedef struct __YMPlexer
{
    YMTypeID _typeID;
    
    int fd;
    char *name;
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
    
    // user
    ym_plexer_interrupted_func interruptedFunc;
    ym_plexer_new_upstream_func newIncomingFunc;
    ym_plexer_stream_closing_func closingFunc;
} _YMPlexer;

#define YMPlexerDefaultBufferSize (1e+6)

YMPlexerRef YMPlexerCreate(int fd)
{
    _YMPlexer *plexer = (_YMPlexer *)calloc(1,sizeof(_YMPlexer));
    plexer->_typeID = _YMPlexerTypeID;
    
    plexer->provider = YMSecurityProviderCreate(fd);
    
    plexer->localStreamsByID = YMDictionaryCreate();
    plexer->localAccessLock = YMLockCreate();
    plexer->localPlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->localPlexBuffer = malloc(plexer->localPlexBufferSize);
    
    plexer->remoteStreamsByID = YMDictionaryCreate();
    plexer->remoteAccessLock = YMLockCreate();
    plexer->remotePlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->remotePlexBuffer = malloc(plexer->remotePlexBufferSize);
    
    plexer->fd = fd;
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
    bool okay;
    
    if ( plexer->fd != -1 )
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
        char *inHello = (char *)calloc(sizeof(char),inHelloLen);
        okay = YMSecurityProviderRead(plexer->provider, (void *)inHello, inHelloLen);
        if ( ! okay || strcmp(YMPlexerSlaveHello,inHello) )
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
        char *inHello = (char *)calloc(sizeof(char),inHelloLen);
        okay = YMSecurityProviderRead(plexer->provider, (void *)inHello, inHelloLen);
        
        if ( ! okay || strcmp(YMPlexerMasterHello,inHello) )
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
    
    plexer->running = true;
    
    return true;
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
    YMStreamRef newStream = YMStreamCreate(name,userInfo);
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
