//
//  YMConnection.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMConnection.h"

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

typedef struct __ym_connection
{
    _YMType _type;
    
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
} ___ym_connection;
typedef struct __ym_connection __YMConnection;
typedef __YMConnection *__YMConnectionRef;

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

__YMConnectionRef __YMConnectionCreate(bool isIncoming, int socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType);
bool __YMConnectionDestroy(__YMConnectionRef connection);
bool __YMConnectionInitCommon(__YMConnectionRef connection, int newSocket, bool asServer);

YMConnectionRef YMConnectionCreate(YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType)
{
    return __YMConnectionCreate(false, NULL_SOCKET, address, type, securityType);
}

YMConnectionRef YMConnectionCreateIncoming(int socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType)
{
    __YMConnectionRef connection = __YMConnectionCreate(true, socket, address, type, securityType);
    bool commonInitOK = __YMConnectionInitCommon(connection, socket, true);
    if ( ! commonInitOK )
    {
        ymlog("connection[%s]: server init failed",YMSTR(YMAddressGetDescription(address)));
        YMRelease(connection);
        return NULL;
    }
    return connection;
}

__YMConnectionRef __YMConnectionCreate(bool isIncoming, int socket, YMAddressRef address, YMConnectionType type, YMConnectionSecurityType securityType)
{
    if ( type < __YMConnectionTypeMin || type > __YMConnectionTypeMax )
        return NULL;
    if ( securityType < __YMConnectionSecurityTypeMin || securityType > __YMConnectionSecurityTypeMax )
        return NULL;
    
    __YMConnectionRef connection = (__YMConnectionRef)_YMAlloc(_YMConnectionTypeID,sizeof(__YMConnection));
    
    connection->socket = socket;
    connection->isIncoming = isIncoming;
    connection->address = address;
    connection->type = type;
    connection->securityType = securityType;
    
    connection->newFunc = NULL;
    connection->newFuncContext = NULL;
    connection->closingFunc = NULL;
    connection->closingFuncContext = NULL;
    connection->interruptedFunc = NULL;
    connection->interruptedFuncContext = NULL;
    
    connection->isConnected = false;
    connection->security = NULL;
    connection->plexer = NULL;
    
    return connection;
}

void _YMConnectionFree(YMTypeRef object)
{
    __YMConnectionRef connection = (__YMConnectionRef)object;
    __YMConnectionDestroy(connection);
}

void YMConnectionSetCallbacks(YMConnectionRef connection_,
                              ym_connection_new_stream_func newFunc, void *newFuncContext,
                              ym_connection_stream_closing_func closingFunc, void *closingFuncContext,
                              ym_connection_interrupted_func interruptedFunc, void *interruptedFuncContext)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    
    connection->newFunc = newFunc;
    connection->newFuncContext = newFuncContext;
    connection->closingFunc = closingFunc;
    connection->closingFuncContext = closingFuncContext;
    connection->interruptedFunc = interruptedFunc;
    connection->interruptedFuncContext = interruptedFuncContext;
}

bool YMConnectionConnect(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    
    if ( connection->socket >= 0 || connection->isIncoming )
    {
        ymerr("connection[%s]: connect called on connected socket",YMSTR(YMAddressGetDescription(connection->address)));
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
        ymerr("connection: socket(%s) failed: %d (%s)",YMSTR(YMAddressGetDescription(connection->address)),errno,strerror(errno));
        return false;
    }
    
    int yes = 1;
    int aResult = setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if ( aResult != 0 )
        ymerr("connection: warning: setsockopt failed on %d: %d: %d (%s)",newSocket,aResult,errno,strerror(errno));
    
    ymlog("connection[%s]: connecting...",YMSTR(YMAddressGetDescription(connection->address)));
    
    struct sockaddr *addr = (struct sockaddr *)YMAddressGetAddressData(connection->address);
    socklen_t addrLen = YMAddressGetLength(connection->address);
    __unused struct sockaddr_in *addrAsIPV4 = (struct sockaddr_in *)addr;
    __unused struct sockaddr_in6 *addrAsIPV6 = (struct sockaddr_in6 *)addr;
    
    int result = connect(newSocket, addr, addrLen);
    if ( result != 0 )
    {
        ymerr("connection: error: connect(%s): %d (%s)",YMSTR(YMAddressGetDescription(connection->address)),errno,strerror(errno));
        close(newSocket);
        return false;
    }
    
    ymlog("connection[%s]: connected",YMSTR(YMAddressGetDescription(connection->address)));
    
    bool commonInitOK = __YMConnectionInitCommon(connection, newSocket, false);
    if ( ! commonInitOK )
    {
        close(newSocket);
        return false;
    }
    
    connection->socket = newSocket;
    
    return true;
}

bool __YMConnectionInitCommon(__YMConnectionRef connection, int newSocket, bool asServer)
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
            ymerr("connection[%s]: unknown security type",YMSTR(YMAddressGetDescription(connection->address)));
            goto rewind_fail;
    }
    
    bool securityOK = YMSecurityProviderInit(security);
    if ( ! securityOK )
    {
        ymerr("connection[%s]: security type %d failed to initialize",YMSTR(YMAddressGetDescription(connection->address)),connection->securityType);
        goto rewind_fail;
    }
    
    plexer = YMPlexerCreate(YMAddressGetDescription(connection->address), newSocket, newSocket, asServer);
    bool plexerOK = YMPlexerStart(plexer);
    if ( ! plexerOK )
    {
        ymerr("connection[%s]: plexer failed to initialize",YMSTR(YMAddressGetDescription(connection->address)));
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
        YMRelease(security);
    if ( plexer )
        YMRelease(plexer);
    return false;
}

bool YMConnectionClose(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    return __YMConnectionDestroy(connection);
}

bool __YMConnectionDestroy(__YMConnectionRef connection)
{
    if ( connection->plexer )
        YMPlexerStop(connection->plexer);
    bool securityOK = YMSecurityProviderClose(connection->security);
    if ( ! securityOK )
        ymerr("connection[%s]: warning: failed to close security",YMSTR(YMAddressGetDescription(connection->address)));
    int closeResult = close(connection->socket);
    if ( closeResult != 0 )
        ymerr("connection[%s]: warning: close socket failed: %d (%s)",YMSTR(YMAddressGetDescription(connection->address)),errno,strerror(errno));
    
    YMRelease(connection->plexer);
    connection->plexer = NULL;
    YMRelease(connection->security);
    connection->security = NULL;
    connection->socket = NULL_SOCKET;
    
    return securityOK && ( closeResult == 0 );
}

uint64_t YMConnectionDoSample(YMConnectionRef connection)
{
    return (uint64_t)connection; // todo
}

YMAddressRef YMConnectionGetAddress(YMConnectionRef connection_)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    return connection->address;
}

YMStreamRef YMConnectionCreateStream(YMConnectionRef connection_, YMStringRef name)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    
    if ( NOT_CONNECTED )
        return NULL;
    
    return YMPlexerCreateNewStream(connection->plexer, name, false);
}

void YMConnectionCloseStream(YMConnectionRef connection_, YMStreamRef stream)
{
    __YMConnectionRef connection = (__YMConnectionRef)connection_;
    YMPlexerCloseStream(connection->plexer, stream);
}

void ym_connection_new_stream_proc(__unused YMPlexerRef plexer,YMStreamRef stream, void *context)
{
    __YMConnectionRef connection = (__YMConnectionRef)context;
    connection->newFunc(connection, stream, connection->newFuncContext);
}

void ym_connection_stream_closing_proc(__unused YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    __YMConnectionRef connection = (__YMConnectionRef)context;
    connection->closingFunc(connection, stream, connection->closingFuncContext);
}

void ym_connection_interrupted_proc(__unused YMPlexerRef plexer, void *context)
{
    __YMConnectionRef connection = (__YMConnectionRef)context;
    connection->interruptedFunc(connection, connection->interruptedFuncContext);
}
