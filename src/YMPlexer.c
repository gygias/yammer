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

typedef struct __YMPlexer
{
    YMTypeID _typeID;
    
    int fd;
    char *name;
    bool running;
    YMSecurityProviderRef provider;
    
    // the downstream
    YMDictionaryRef localStreamsByID;
    uint8_t *localPlexBuffer;
    size_t localPlexBufferSize;
    
    // the upstream
    YMDictionaryRef remoteStreamsByID;
    uint8_t *remotePlexBuffer;
    size_t remotePlexBufferSize;
    
    // synchronization
    YMThreadRef localServiceThread;
    YMThreadRef remoteServiceThread;
    YMLockRef listAccessLock;
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
    plexer->localPlexBufferSize = YMPlexerDefaultBufferSize;
    plexer->localPlexBuffer = malloc(plexer->localPlexBufferSize);
    
    plexer->remoteStreamsByID = YMDictionaryCreate();
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
        okay = YMWriteFull(plexer->fd, (void *)YMPlexerMasterHello, strlen(YMPlexerMasterHello));
        if ( ! okay )
        {
            YMLog(error);
            return false;
        }
        
        unsigned long inHelloLen = strlen(YMPlexerSlaveHello);
        char *inHello = (char *)calloc(sizeof(char),inHelloLen);
        okay = YMReadFull(plexer->fd, (void *)inHello, inHelloLen);
        if ( ! okay || strcmp(YMPlexerSlaveHello,inHello) )
        {
            YMLog(error);
            return false;
        }
    }
    else
    {
        unsigned long inHelloLen = strlen(YMPlexerMasterHello);
        char *inHello = (char *)calloc(sizeof(char),inHelloLen);
        okay = YMReadFull(plexer->fd, (void *)inHello, inHelloLen);
        
        if ( ! okay || strcmp(YMPlexerMasterHello,inHello) )
        {
            YMLog(error);
            return false;
        }
        
        okay = YMWriteFull(plexer->fd, (void *)YMPlexerSlaveHello, strlen(YMPlexerSlaveHello));
        if ( ! okay )
        {
            YMLog(error);
            return false;
        }
    }
    
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
    YMStreamRef newStream = YMStreamCreate(name);
    
#warning todo fcntl direct.
    return NULL;
}

void YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream)
{
    
}