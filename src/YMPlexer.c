//
//  YMPlexer.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMPlexer.h"
#include "YMPrivate.h"

#include "YMSecurityProvider.h"

typedef struct __YMPlexer
{
    YMTypeID _typeID;
    
    int fd;
    char *name;
    uint8_t *downBuffer;
    size_t downBufferSize;
    uint8_t *upBuffer;
    size_t upBufferSize;
    bool running;
    
    YMSecurityProviderRef provider;
    
    ym_plexer_interrupted_func interruptedFunc;
    ym_plexer_new_upstream_func newIncomingFunc;
    ym_plexer_stream_closing_func closingFunc;
} _YMPlexer;

YMPlexerRef YMPlexerCreate(int fd)
{
    _YMPlexer *plexer = (_YMPlexer *)calloc(1,sizeof(_YMPlexer));
    plexer->_typeID = _YMPlexerTypeID;
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
bool YMPlexerStartOnFile(YMPlexerRef plexer, bool master)
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
        okay = YMWrite(plexer->fd, YMPlexerMasterHello, strlen(YMPlexerMasterHello));
        if ( ! okay )
        {
            YMLog(error);
            return false;
        }
        
        unsigned long inHelloLen = strlen(YMPlexerSlaveHello);
        char *inHello = (char *)calloc(sizeof(char),inHelloLen);
        okay = YMRead(plexer->fd, inHello, inHelloLen);
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
        okay = YMRead(plexer->fd, inHello, inHelloLen);
        
        if ( ! okay || strcmp(YMPlexerMasterHello,inHello) )
        {
            YMLog(error);
            return false;
        }
        
        okay = YMWrite(plexer->fd, YMPlexerSlaveHello, strlen(YMPlexerSlaveHello));
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
    
    if ( plexer->downBuffer )
    {
        free(plexer->downBuffer);
        plexer->downBuffer = NULL;
    }
    if ( plexer->upBuffer )
    {
        free(plexer->upBuffer);
        plexer->upBuffer = NULL;
    }
    
    plexer->running = false;
}

YMStreamRef YMPlexerNewStream(YMPlexerRef plexer, char *name, bool direct);
void YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream);