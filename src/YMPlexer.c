//
//  YMPlexer.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMPlexer.h"

#include "YMSecurityProvider.h"

YMTypeID YMPlexerTypeID = 'p';

typedef struct __YMPlexer
{
    YMTypeID type;
    
    int fd;
    char *name;
    
    YMSecurityProviderRef provider;
    
    ym_plexer_interrupted_func interruptedFunc;
    ym_plexer_new_upstream_func newIncomingFunc;
    ym_plexer_stream_closing_func closingFunc;
} _YMPlexer;

YMPlexerRef YMPlexerCreate(char *name)
{
    _YMPlexer *plexer = (_YMPlexer *)calloc(1,sizeof(_YMPlexer));
    plexer->type = YMPlexerTypeID;
    plexer->name = strdup(name);
    return plexer;
}

void YMPlexerFree(YMPlexerRef plexer)
{
    if ( plexer->name )
        free( plexer->name );
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
    plexer->provider = provider;
}

const char* YMPlexerMasterHello = "hola";
const char* YMPlexerSlaveHello = "greetings";
bool YMPlexerStartOnFile(YMPlexerRef plexer, int fd, bool master)
{
    bool okay;
    
    if ( master )
    {
        okay = YMWrite(fd, YMPlexerMasterHello, strlen(YMPlexerMasterHello));
        if ( ! okay )
        {
            return false;
        }
    }
    else
    {
        unsigned long inHelloLen = strlen(YMPlexerMasterHello);
        char *inHello = (char *)calloc(sizeof(char),inHelloLen);
        okay = YMRead(fd, inHello, inHelloLen);
        
        if ( ! okay || strcmp(YMPlexerMasterHello,inHello) )
            return false;
    }
    
    // ...
    
    return true;
}

void YMPlexerStop(YMPlexerRef plexer);

YMStreamRef YMPlexerNewStream(YMPlexerRef plexer, char *name, bool direct);
void YMPlexerCloseStream(YMPlexerRef plexer, YMStreamRef stream);