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
#define NOT_CONNECTED ( ( connection->socket == NULL_SOCKET ) && ! connection->isConnected )

typedef struct __YMConnection
{
    YMTypeID _type;
    
    int socket;
    bool isIncoming;
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
    bool isConnected;
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

YMConnectionRef __YMConnectionCreate(bool isIncoming, int socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType);
bool __YMConnectionDestroy(YMConnectionRef connection);
bool __YMConnectionInitCommon(YMConnectionRef connection, int newSocket, bool asServer);

YMConnectionRef YMConnectionCreate(YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType)
{
    return __YMConnectionCreate(false, NULL_SOCKET, address, type, securityType);
}

YMConnectionRef YMConnectionCreateIncoming(int socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType)
{
    YMConnectionRef connection = __YMConnectionCreate(true, socket, address, type, securityType);
    bool commonInitOK = __YMConnectionInitCommon(connection, socket, true);
    if ( ! commonInitOK )
    {
        ymlog("connection[%s]: server init failed",YMAddressGetDescription(address));
        YMFree(connection);
        return NULL;
    }
    return connection;
}

YMConnectionRef __YMConnectionCreate(bool isIncoming, int socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType)
{
    if ( type < __YMConnectionTypeMin || type > __YMConnectionTypeMax )
        return NULL;
    if ( securityType < __YMConnectionSecurityTypeMin || securityType > __YMConnectionSecurityTypeMax )
        return NULL;
    
    _YMConnection *connection = (_YMConnection *)calloc(1,sizeof(_YMConnection));
    connection->_type = _YMConnectionTypeID;
    
    connection->isIncoming = isIncoming;
    connection->address = address;
    connection->type = type;
    connection->socket = socket;
    connection->isConnected = false;
    connection->securityType = securityType;
    
    return (YMConnectionRef)connection;
}

void _YMConnectionFree(YMTypeRef object)
{
    YMConnectionRef connection = (YMConnectionRef)object;
    
    __YMConnectionDestroy(connection);
    
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
    if ( connection->socket >= 0 || connection->isIncoming )
    {
        ymerr("connection[%s]: connect called on connected socket",YMAddressGetDescription(connection->address));
        return false;
    }    
    
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
    //struct protoent *ppe = getprotobyname("tcp");
    
    int newSocket = socket(domain, type, protocol); // xxx
    if ( newSocket < 0 )
    {
        ymerr("connection: socket(%s) failed: %d (%s)",YMAddressGetDescription(connection->address),errno,strerror(errno));
        return false;
    }
    
    ymlog("connection[%s]: connecting...",YMAddressGetDescription(connection->address));
    
    struct sockaddr *addr = (struct sockaddr *)YMAddressGetAddressData(connection->address);
    socklen_t addrLen = YMAddressGetLength(connection->address);
    __unused struct sockaddr_in *addrAsIPV4 = (struct sockaddr_in *)addr;
    __unused struct sockaddr_in6 *addrAsIPV6 = (struct sockaddr_in6 *)addr;
    
    int result = connect(newSocket, addr, addrLen);
    if ( result != 0 )
    {
        ymerr("connection: error: connect(%s): %d (%s)",YMAddressGetDescription(connection->address),errno,strerror(errno));
        close(newSocket);
        return false;
    }
    
    ymlog("connection[%s]: connected",YMAddressGetDescription(connection->address));
    
    bool commonInitOK = __YMConnectionInitCommon(connection, newSocket, false);
    if ( ! commonInitOK )
    {
        close(newSocket);
        return false;
    }
    
    connection->socket = newSocket;
    
    return true;
}

bool __YMConnectionInitCommon(YMConnectionRef connection, int newSocket, bool asServer)
{
    YMSecurityProviderRef security = NULL;
    YMPlexerRef plexer = NULL;
    
    switch( connection->securityType )
    {
        case YMInsecure:
            security = YMSecurityProviderCreateWithFullDuplexFile(newSocket);
            break;
        case YMTLS:
            security = (YMSecurityProviderRef)YMTLSProviderCreateWithFullDuplexFile(newSocket, asServer);
            break;
        default:
            ymerr("connection[%s]: unknown security type",YMAddressGetDescription(connection->address));
            goto rewind_fail;
    }
    
    bool securityOK = YMSecurityProviderInit(security);
    if ( ! securityOK )
    {
        ymerr("connection[%s]: security type %d failed to initialize",YMAddressGetDescription(connection->address),connection->securityType);
        goto rewind_fail;
    }
    
    plexer = YMPlexerCreate((char *)YMAddressGetDescription(connection->address), newSocket, newSocket, asServer);
    bool plexerOK = YMPlexerStart(plexer);
    if ( ! plexerOK )
    {
        ymerr("connection[%s]: plexer failed to initialize",YMAddressGetDescription(connection->address));
        goto rewind_fail;
    }
    
    YMPlexerSetSecurityProvider(plexer, security);
    YMPlexerSetNewIncomingStreamFunc(plexer, ym_connection_new_stream_proc);
    YMPlexerSetInterruptedFunc(plexer, ym_connection_interrupted_proc);
    YMPlexerSetStreamClosingFunc(plexer, ym_connection_stream_closing_proc);
    YMPlexerSetCallbackContext(plexer, connection);
    
    connection->plexer = plexer;
    connection->security = security;
    
    return true;
    
rewind_fail:
    if ( security )
        YMFree(security);
    if ( plexer )
        YMFree(plexer);
    return false;
}

bool YMConnectionClose(YMConnectionRef connection)
{
    return __YMConnectionDestroy(connection);
}

bool __YMConnectionDestroy(YMConnectionRef connection)
{
    if ( connection->plexer )
        YMPlexerStop(connection->plexer);
    bool securityOK = YMSecurityProviderClose(connection->security);
    if ( ! securityOK )
        ymerr("connection[%s]: warning: failed to close security",YMAddressGetDescription(connection->address));
    int closeResult = close(connection->socket);
    if ( closeResult != 0 )
        ymerr("connection[%s]: warning: close socket failed: %d (%s)",YMAddressGetDescription(connection->address),errno,strerror(errno));
    
    YMFree(connection->plexer);
    connection->plexer = NULL;
    YMFree(connection->security);
    connection->security = NULL;
    connection->socket = NULL_SOCKET;
    
    return securityOK && ( closeResult == 0 );
}

uint64_t YMConnectionDoSample(YMConnectionRef connection)
{
    return (uint64_t)connection; // todo
}

YMAddressRef YMConnectionGetAddress(YMConnectionRef connection)
{
    return connection->address;
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
