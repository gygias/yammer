//
//  YMConnection.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMConnection.h"
#include "YMPrivate.h"

#include "YMPlexer.h"
#include "YMSecurityProvider.h"
#include "YMTLSProvider.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogConnection
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#include <sys/socket.h>

#define NULL_SOCKET (-1)
#define NOT_CONNECTED ( connection->socket == NULL_SOCKET )

typedef struct __YMConnection
{
    YMTypeID _type;
    
    YMAddressRef address;
    YMConnectionType type;
    YMConnectionSecurityType securityType;
    
    ym_connection_new_stream_func newFunc;
    void *newFuncContext;
    ym_connection_stream_closing_func closingFunc;
    void *closingFuncContext;
    ym_connection_interrupted_func interruptedFunc;
    void *interruptedFuncContext;
    
    // volatile
    int socket;
    YMSecurityProviderRef security;
    YMPlexerRef plexer;
} _YMConnection;

enum
{
    __YMConnectionTypeMin = YMConnectionStream,
    __YMConnectionTypeMax = YMConnectionStream
};

enum
{
    __YMConnectionSecurityTypeMin = YMInsecure,
    __YMConnectionSecurityTypeMax = YMTLS
};

void ym_connection_new_stream_proc(YMPlexerRef plexer,YMStreamRef stream, void *context);
void ym_connection_stream_closing_proc(YMPlexerRef plexer, YMStreamRef stream, void *context);
void ym_connection_interrupted_proc(YMPlexerRef plexer, void *context);

YMConnectionRef YMConnectionCreate(YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType)
{
    if ( type < __YMConnectionTypeMin || type > __YMConnectionTypeMax )
        return NULL;
    if ( securityType < __YMConnectionSecurityTypeMin || securityType > __YMConnectionSecurityTypeMax )
        return NULL;
    
    _YMConnection *connection = (_YMConnection *)calloc(1,sizeof(_YMConnection));
    connection->_type = _YMConnectionTypeID;
    
    connection->address = address;
    connection->type = type;
    connection->socket = NULL_SOCKET;
    connection->securityType = securityType;
    
    return (YMConnectionRef)connection;
}

void _YMConnectionFree(YMTypeRef object)
{
    YMConnectionRef connection = (YMConnectionRef)object;
    free(connection);
}

void YMConnectionSetCallbacks(YMConnectionRef connection,
                              ym_connection_new_stream_func newFunc, void *newFuncContext,
                              ym_connection_stream_closing_func closingFunc, void *closingFuncContext,
                              ym_connection_interrupted_func interruptedFunc, void *interruptedFuncContext)
{
    connection->newFunc = newFunc;
    connection->newFuncContext = newFuncContext;
    connection->closingFunc = closingFunc;
    connection->closingFuncContext = closingFuncContext;
    connection->interruptedFunc = interruptedFunc;
    connection->interruptedFuncContext = interruptedFuncContext;
}

bool YMConnectionConnect(YMConnectionRef connection)
{
    YMSecurityProviderRef security = NULL;
    YMPlexerRef plexer = NULL;
    
    int type;
    switch(connection->type)
    {
        case YMConnectionStream:
            type = SOCK_STREAM;
            break;
        default:
            return false;
    }
    
    int domain = YMAddressGetDomain(connection->address);
    int addressFamily = YMAddressGetAddressFamily(connection->address);
    int protocol = YMAddressGetDefaultProtocolForAddressFamily(addressFamily);
    
    int newSocket = socket(domain, type, protocol);
    if ( newSocket < 0 )
    {
        ymerr("connection: socket(%s) failed: %d (%s)",YMAddressGetDescription(connection->address),errno,strerror(errno));
        return false;
    }
    
    ymlog("connection[%s]: connecting...",YMAddressGetDescription(connection->address));
    
    int result = connect(newSocket, YMAddressGetAddressData(connection->address), YMAddressGetLength(connection->address));
    if ( result != 0 )
    {
        ymerr("connection: connect(%s)",YMAddressGetDescription(connection->address));
        return false;
    }
    
    ymlog("connection[%s]: connected",YMAddressGetDescription(connection->address));
    
    switch( connection->securityType )
    {
        case YMInsecure:
            security = YMSecurityProviderCreateWithFullDuplexFile(newSocket);
            break;
        case YMTLS:
            security = (YMSecurityProviderRef)YMTLSProviderCreateWithFullDuplexFile(newSocket, false);
            break;
        default:
            ymerr("connection: unknown security type");
            goto rewind_fail;
    }
    
    bool securityOK = YMSecurityProviderInit(security);
    if ( ! securityOK )
    {
        ymerr("connection[%s]: security type %d failed to initialize",YMAddressGetDescription(connection->address),connection->securityType);
        goto rewind_fail;
    }
    
    plexer = YMPlexerCreate((char *)YMAddressGetDescription(connection->address), newSocket, newSocket, false);
    bool plexerOK = YMPlexerStart(plexer);
    if ( ! plexerOK )
    {
        ymerr("connection[%s]: plexer failed to initialize",YMAddressGetDescription(connection->address));
        goto rewind_fail;
    }
    
    YMPlexerSetSecurityProvider(plexer, security);
    YMPlexerSetNewIncomingStreamFunc(plexer, ym_connection_new_stream_proc);
    
    connection->plexer = plexer;
    connection->security = security;
    connection->socket = newSocket;
    
    return true;
    
rewind_fail:
    close(newSocket);
    if ( security )
        YMFree(security);
    return false;
}

uint64_t YMConnectionDoSample(YMConnectionRef connection)
{
    return (uint64_t)connection;
}

YMStreamRef YMConnectionCreateStream(YMConnectionRef connection, const char *name)
{
    if ( NOT_CONNECTED )
        return NULL;
    
    return YMPlexerCreateNewStream(connection->plexer, name, false);
}

void YMConnectionCloseStream(YMConnectionRef connection, YMStreamRef stream)
{
    YMPlexerCloseStream(connection->plexer, stream);
}

void ym_connection_new_stream_proc(__unused YMPlexerRef plexer,YMStreamRef stream, void *context)
{
    YMConnectionRef connection = (YMConnectionRef)context;
    connection->newFunc(connection, stream, connection->newFuncContext);
}

void ym_connection_stream_closing_proc(__unused YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    YMConnectionRef connection = (YMConnectionRef)context;
    connection->closingFunc(connection, stream, connection->closingFuncContext);
}

void ym_connection_interrupted_proc(__unused YMPlexerRef plexer, void *context)
{
    YMConnectionRef connection = (YMConnectionRef)context;
    connection->interruptedFunc(connection, connection->interruptedFuncContext);
}
