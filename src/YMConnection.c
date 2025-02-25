//
//  YMConnection.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMConnection.h"
#include "YMConnectionPriv.h"

#include "YMPlexer.h"
#include "YMSecurityProvider.h"
#include "YMTLSProvider.h"
#include "YMUtilities.h"
#include "YMDispatchUtils.h"

#include "YMStreamPriv.h"

#define ymlog_type YMLogConnection
#define ymlog_pre "connection[%s:%s]: "
#define ymlog_args (c->isIncoming ? "s" : "c"),(c->address ? YMSTR(YMAddressGetDescription(c->address)) : "*")
#include "YMLog.h"

#if !defined(YMWIN32)
# include <sys/socket.h>
# include <arpa/inet.h>
#else
# include <winsock2.h>
# include <ws2tcpip.h>
# include <time.h>
#endif

#define NOT_CONNECTED ( ( c->socket == NULL_SOCKET ) && ! c->isConnected )

typedef enum {
    YMIFAP_IPV4 = 1,
    YMIFAP_IPV6 = 2,
    YMIFAP_Unknown = -1
} YMIFAPType;

typedef enum {
    YMConnectionCommandOkay = -1,
    YMConnectionCommandError = -2,
    YMConnectionCommandSecurity = -3,
    YMConnectionCommandIFExchange = -4,
    YMConnectionCommandSample = -5,
    YMConnectionCommandInit = INT32_MIN
} __YMConnectionCommand;

typedef struct __ym_connection_command {
    int32_t command;
    uint32_t userInfo;
} __ym_connection_command;

YM_EXTERN_C_PUSH

typedef struct __ym_connection
{
    _YMType _common;
    
	YMSOCKET socket;
    bool isIncoming;
    YMIFAPType apType;
    YMStringRef localIFName;
    YMInterfaceType localIFType;
    YMStringRef remoteIFName;
    YMInterfaceType remoteIFType;
    const void *remoteAddr;
    YMAddressRef address;
    YMConnectionType type;
    YMConnectionSecurityType securityType;
    bool closeWhenDone;
    
    ym_connection_new_stream_func newFunc;
    void *newFuncContext;
    ym_connection_stream_closing_func closingFunc;
    void *closingFuncContext;
    ym_connection_interrupted_func interruptedFunc;
    void *interruptedFuncContext;
    
    // volatile
    bool isConnected;
    int64_t sample;
    YMSecurityProviderRef security;
    YMPlexerRef plexer;
} __ym_connection;
typedef struct __ym_connection __ym_connection_t;

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

void ym_connection_new_stream_proc(YMPlexerRef plexer, YMStreamRef stream, void *context);
void ym_connection_stream_closing_proc(YMPlexerRef plexer, YMStreamRef stream, void *context);
void ym_connection_interrupted_proc(YMPlexerRef plexer, void *context);

__ym_connection_t *__YMConnectionCreate(bool isIncoming, YMAddressRef peerAddress, YMConnectionType type, bool closeWhenDone);
bool __YMConnectionDestroy(__ym_connection_t *, bool explicit);
YMSecurityProviderRef __YMConnectionInitSecurity(__ym_connection_t *, YMSOCKET socket, int securityType, bool asServer);
int64_t __YMConnectionDoSample(__ym_connection_t *, YMSOCKET socket, uint32_t length, bool asServer);
bool __YMConnectionDoIFExchange(__ym_connection_t *, YMSOCKET socket, bool asServer);
bool __YMConnectionInitCommon(__ym_connection_t *, YMSOCKET newSocket, bool asServer);

bool __YMConnectionInitializeIncomingStream(__ym_connection_t *c, YMStreamRef stream);
bool __YMConnectionInitializeOutgoingStream(__ym_connection_t *c, YMStreamRef stream, YMCompressionType compression);

bool __YMConnectionForward(YMConnectionRef connection, bool toFile, YMStreamRef stream, YMFILE file, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t*);
void _ym_connection_forward_callback_proc(void *context, YMIOResult result, uint64_t bytesForwarded);

YMConnectionRef YMConnectionCreate(YMAddressRef peerAddress, YMConnectionType type, YMConnectionSecurityType securityType, bool closeWhenDone)
{
    if ( securityType < __YMConnectionSecurityTypeMin || securityType > __YMConnectionSecurityTypeMax )
        return NULL;

    __ym_connection_t *c = __YMConnectionCreate(false, peerAddress, type, closeWhenDone);
    c->socket = NULL_SOCKET;
    c->securityType = securityType;
    return c;
}

YMConnectionRef YMConnectionCreateIncoming(YMSOCKET socket, YMAddressRef peerAddress, YMConnectionType type, bool closeWhenDone)
{
    __ym_connection_t *c = __YMConnectionCreate(true, peerAddress, type, closeWhenDone);
    bool commonInitOK = __YMConnectionInitCommon(c, socket, true);
    if ( ! commonInitOK ) {
        ymlog("server init failed");
        YMRelease(c);
        return NULL;
    }
    
    return c;
}

__ym_connection_t *__YMConnectionCreate(bool isIncoming, YMAddressRef address, YMConnectionType type, bool closeWhenDone)
{
    // not sure what i had envisioned here so long ago
    if ( type < __YMConnectionTypeMin || type > __YMConnectionTypeMax )
        return NULL;
    
    __ym_connection_t *c = (__ym_connection_t *)_YMAlloc(_YMConnectionTypeID,sizeof(__ym_connection_t));

	YMNetworkingInit();
    
    c->isIncoming = isIncoming;
    c->address = (YMAddressRef)YMRetain(address);
    c->type = type;
    c->closeWhenDone = closeWhenDone;
    
    c->newFunc = NULL;
    c->newFuncContext = NULL;
    c->closingFunc = NULL;
    c->closingFuncContext = NULL;
    c->interruptedFunc = NULL;
    c->interruptedFuncContext = NULL;
    
    c->isConnected = false;
    c->plexer = NULL;
    
    // remote gets set during 'initialization' later
    c->remoteIFType = YMInterfaceUnknown;
    c->remoteIFName = NULL;
    c->remoteAddr = NULL;
    
    return c;
}

void _YMConnectionFree(YMTypeRef c_)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    
    c->newFunc = NULL;
    c->closingFunc = NULL;
    c->interruptedFunc = NULL;
    
    __YMConnectionDestroy(c, true); // frees security and plexer
    
    YMRelease(c->address);
    if ( c->localIFName )
        YMRelease(c->localIFName);
    if ( c->remoteIFName )
        YMRelease(c->remoteIFName);
    if ( c->remoteAddr )
        YMFREE((void *)c->remoteAddr);
}

void YMConnectionSetCallbacks(YMConnectionRef c_,
                              ym_connection_new_stream_func newFunc, void *newFuncContext,
                              ym_connection_stream_closing_func closingFunc, void *closingFuncContext,
                              ym_connection_interrupted_func interruptedFunc, void *interruptedFuncContext)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    
    c->newFunc = newFunc;
    c->newFuncContext = newFuncContext;
    c->closingFunc = closingFunc;
    c->closingFuncContext = closingFuncContext;
    c->interruptedFunc = interruptedFunc;
    c->interruptedFuncContext = interruptedFuncContext;
}

bool YMConnectionConnect(YMConnectionRef c_)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    
    YM_IO_BOILERPLATE
    
    if ( c->socket != NULL_SOCKET || c->isIncoming ) {
        ymerr("connect called on connected socket");
        return false;
    }
    
    int type;
    switch(c->type) {
        case YMConnectionStream:
            type = SOCK_STREAM;
            break;
        default:
            return false;
    }
    
    int domain = YMAddressGetDomain(c->address);
    int addressFamily = YMAddressGetAddressFamily(c->address);
    int protocol = YMAddressGetDefaultProtocolForAddressFamily(addressFamily);
    //struct protoent *ppe = getprotobyname("tcp");
    
	YMSOCKET newSocket = socket(domain, type, protocol);
#if !defined(YMWIN32)
    ymassert(newSocket>=0,"socket failed: %d (%s)",errno,strerror(errno));
#else
	ymassert(newSocket!=INVALID_SOCKET,"socket failed: %x",GetLastError());
#endif
    
    int yes = 1;
    result = setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes));
    if ( result != 0 ) ymerr("warning: setsockopt(reuse) failed on f%d: %ld: %d (%s)",newSocket,result,errno,strerror(errno));
    //result = setsockopt(newSocket, SOL_SOCKET, SO_DONTROUTE, (const void *)&yes, sizeof(yes));
    //if ( result != 0 ) ymerr("warning: setsockopt(dontroute) failed on f%d: %ld: %d (%s)",newSocket,result,errno,strerror(errno));
    
    ymlog("connecting...");
    
    struct sockaddr *addr = (struct sockaddr *)YMAddressGetAddressData(c->address);
    socklen_t addrLen = YMAddressGetLength(c->address);
    if ( addr->sa_family == AF_INET )
        ((struct sockaddr_in *)addr)->sin_port = htons(((struct sockaddr_in *)addr)->sin_port);
    else if ( addr->sa_family == AF_INET6 )
        ((struct sockaddr_in6 *)addr)->sin6_port = htons(((struct sockaddr_in6 *)addr)->sin6_port);
    else
        ymabort("connect: address family %d unsupported",addr->sa_family);
    
    result = connect(newSocket, addr, addrLen);
    if ( result != 0 ) {
        ymerr("connect(%s): %d (%s)",YMSTR(YMAddressGetDescription(c->address)),errno,strerror(errno));
        YM_CLOSE_SOCKET(newSocket);
        return false;
    }
    
    ymlog("connected");
    c->socket = newSocket;
    
    return true;
}

bool YMAPI YMConnectionInit(YMConnectionRef c_)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    return __YMConnectionInitCommon(c, c->socket, false);
}

#define YMIFExNameMax 64 // 36-char windows 'guid' representation longest known platform 'interface name'
typedef struct YMIFExchangePrefix {
    YMIFAPType apType;
    YMInterfaceType ifType;
    char ifName[YMIFExNameMax];
} YMIFExchangePrefix;

bool __YMConnectionDoIFExchange(__ym_connection_t *c, YMSOCKET socket, bool asServer)
{
    YM_IO_BOILERPLATE
    
    const char *whyFailed = NULL;
    YMIFExchangePrefix theirPrefix = { YMIFAP_Unknown, YMInterfaceUnknown, {0} };
    YMIFExchangePrefix myPrefix = { YMIFAP_Unknown, c->localIFType, {0} };
    strncat((char *)&(myPrefix.ifName),YMSTR(c->localIFName),YMIFExNameMax);
    
    const struct sockaddr *mySockaddr = YMAddressGetAddressData(c->address);
    if ( mySockaddr->sa_family == AF_INET )
        myPrefix.apType = YMIFAP_IPV4;
    else if ( mySockaddr->sa_family == AF_INET6 )
        myPrefix.apType = YMIFAP_IPV6;
    
    bool okay = true;
    
    for( int i = 0; i < 2; i++ ) {
        if ( (( i == 0 ) && asServer) || (( i == 1 ) && ! asServer) ) {
            okay = YMWriteFull(socket, (uint8_t *)&myPrefix, sizeof(myPrefix), NULL);
            if ( ! okay ) { whyFailed = "write prefix"; goto catch_return; }
            
            // todo implemented localhost<->localhost does endianness matter streaming this?
            // todo endianness here
            if ( mySockaddr->sa_family == AF_INET ) {
                okay = YMWriteFull(socket,(uint8_t *)&((struct sockaddr_in *)mySockaddr)->sin_addr.s_addr, sizeof(in_addr_t), NULL);
                if ( ! okay ) { whyFailed = "write ipv4 addr"; goto catch_return; }
                okay = YMWriteFull(socket,(uint8_t *)&((struct sockaddr_in *)mySockaddr)->sin_port, sizeof(in_port_t), NULL);
                if ( ! okay ) { whyFailed = "write ipv6 port"; goto catch_return; }
            } else if ( mySockaddr->sa_family == AF_INET6 ) {
                okay = YMWriteFull(socket,(uint8_t *)&((struct sockaddr_in6 *)mySockaddr)->sin6_addr,sizeof(struct in6_addr), NULL);
                if ( ! okay ) { whyFailed = "write ipv6 addr"; goto catch_return; }
                okay = YMWriteFull(socket,(uint8_t *)&((struct sockaddr_in6 *)mySockaddr)->sin6_port,sizeof(in_port_t), NULL);
                if ( ! okay ) { whyFailed = "write ipv6 port"; goto catch_return; }
            }
        }
        
        if ( (( i == 1 ) && asServer) || (( i == 0 ) && ! asServer) ) {
            okay = YMReadFull(socket, (uint8_t *)&theirPrefix, sizeof(theirPrefix), NULL);
            if ( ! okay )  { whyFailed = "read prefix"; goto catch_return; }
            
            int32_t addrLen = 0;
            switch(theirPrefix.apType) {
                case YMIFAP_IPV4:
                    addrLen = 4 + 2;
                    break;
                case YMIFAP_IPV6:
                    addrLen = 16 + 2;
                    break;
                default:
                    okay = false;
                    whyFailed = "unknown ap type on if exchange";
                    goto catch_return;
            }
            
            c->apType = theirPrefix.apType;
            c->remoteAddr = YMALLOC(addrLen);
            okay = YMReadFull(socket, (void *)c->remoteAddr, addrLen, NULL);
            if ( ! okay ) { whyFailed = "read addr"; goto catch_return; }
        }
        
    }
    
    c->remoteIFName = YMStringCreateWithCString(theirPrefix.ifName);
    c->remoteIFType = theirPrefix.ifType;
    
catch_return:;
    
    if ( ! okay )
        ymerr("if exchange: %s", whyFailed?whyFailed:"?");
        
    return okay;
}

YMSecurityProviderRef __YMConnectionInitSecurity(__ym_connection_t *c, YMSOCKET socket, int securityType, bool asServer)
{
    YMSecurityProviderRef security = NULL;

    switch( securityType ) {
        case YMInsecure:
            security = YMSecurityProviderCreate(socket,socket);
            break;
        case YMTLS:
            security = (YMSecurityProviderRef)YMTLSProviderCreate(socket, socket, asServer);
            break;
        default:
            ymerr("security init: unknown type %d",securityType);
            return NULL;
    }

    if ( ! security ) {
        ymerr("security init: failed to instantiate");
        return NULL;
    }

    bool securityOK = YMSecurityProviderInit(security);
    if ( ! securityOK ) {
        ymerr("security type %d failed to initialize",securityType);
        return NULL;
    }

    return security;
}

int64_t __YMConnectionDoSample(__unused __ym_connection_t *c, YMSOCKET socket, uint32_t length, bool asServer)
{
    YM_IO_BOILERPLATE
    
    uint64_t sample = -1;
    
    #define bufLen 16384
    if ( length % bufLen != 0 ) {
        ymerr("sample must be divisible by 2^14");
        return sample;
    }
    
    struct timeval start;
    gettimeofday(&start, NULL);

    uint8_t *buf = calloc(1,bufLen);
    if ( ! buf ) {
        ymerr("failed to allocate sample buffer: %d %s",errno,strerror(errno));
        return sample;
    }
    
    uint64_t halfLength = length / 2;
    for( int i = 0; i < 2; i++ ) {
        bool writing = ( i == 0 ) ^ !asServer;
        uint64_t sentReceived = 0;
        
        while ( sentReceived < halfLength ) {
            ssize_t toReadWrite = ( bufLen + sentReceived > halfLength ) ? ( halfLength - sentReceived ) : bufLen;
            if ( writing ) {
                YMRandomDataWithLength(buf,toReadWrite);
                YM_WRITE_SOCKET(socket, buf, (size_t)toReadWrite);
                if ( result == -1 ) {
                    ymerr("%zd = YM_WRITE_SOCKET(%d, %p, %ld): %d %s",result,socket,buf,toReadWrite,errno,strerror(errno));
                    goto catch_return;
                }
                sentReceived += result;
            } else {
                YM_READ_SOCKET(socket, buf, (size_t)toReadWrite);
                if ( result == -1 ) {
                    ymerr("%zd = YM_READ_SOCKET(%d, %p, %ld): %d %s",result,socket,buf,toReadWrite,errno,strerror(errno));
                    goto catch_return;
                }
                sentReceived += result;
            }
        }
        ymlog("%s back sample",writing?"reading":"writing");
    }
    
    struct timeval end;
    gettimeofday(&end,NULL);
    uint64_t usecsElapsed = ( end.tv_sec - start.tv_sec ) * 1000000000 + ( end.tv_usec - start.tv_usec );
    sample = length / ((double)usecsElapsed / 1000000000);
    ymlog("approximated sample to %lub/s",sample);
    
catch_return:
    free(buf);
    return sample;
}

bool __YMConnectionInitCommon(__ym_connection_t *c, YMSOCKET newSocket, bool asServer)
{
    YM_IO_BOILERPLATE
    
    YMSecurityProviderRef security = NULL;
    YMPlexerRef plexer = NULL;
    struct __ym_connection_command command;
    bool conCmdOkay = true;
    
    // determine local interface
    struct sockaddr_in6 saddr;
    socklen_t slen = sizeof(saddr);
    bool matched = false;
    result = getsockname(newSocket, (struct sockaddr *)&saddr, &slen);
    if ( result == 0 ) {
        YMAddressRef localAddr = YMAddressCreate(&saddr, 0);
        YMDictionaryRef localIFMap = YMInterfaceMapCreateLocal();
        if ( localIFMap ) {
            YMDictionaryEnumRef denum = YMDictionaryEnumeratorBegin(localIFMap);
            while ( denum ) {
                YMStringRef ifName = (YMStringRef)denum->key;
                YMDictionaryRef ifInfo = (YMDictionaryRef)denum->value;
                YMArrayRef ifAddrs = (YMArrayRef)YMDictionaryGetItem(ifInfo, kYMIFMapAddressesKey);
                for ( int i = 0; i < YMArrayGetCount(ifAddrs); i++ ) {
                    YMAddressRef aLocalAddress = (YMAddressRef)YMArrayGet(ifAddrs, i);
                    ymdbg("%s: %s ?= %s",YMSTR(ifName),YMSTR(YMAddressGetDescription(localAddr)),YMSTR(YMAddressGetDescription(aLocalAddress)));
                    if ( YMAddressIsEqualIncludingPort(localAddr, aLocalAddress, false) ) {
                        c->localIFName = YMRetain(ifName);
                        c->localIFType = YMInterfaceTypeForName(ifName);
                        ymdbg("allocated %s (%s)",YMSTR(ifName),YMInterfaceTypeDescription(c->localIFType));
                        matched = true;
                        break;
                    }
                }
                if ( matched )
                    break;
                denum = YMDictionaryEnumeratorGetNext(denum);
            }
            YMDictionaryEnumeratorEnd(denum);
            
            YMRelease(localIFMap);
        }
        YMRelease(localAddr);
    }
    if ( result == -1 || ! matched ) {
        c->localIFName = YMSTRC("?");
        c->localIFType = YMInterfaceUnknown;
    }
    
    if ( asServer ) {

        YM_READ_SOCKET(newSocket, (char *)&command, sizeof(command));
        if ( result != sizeof(command) ) {
            ymerr("server failed to recv security: %zd %d %s",result,error,errorStr);
            YM_CLOSE_SOCKET(newSocket);
            return false;
        }

        if ( ( command.command != YMConnectionCommandSecurity ) ||
                ( ( command.userInfo != YMTLS ) && ( command.userInfo != YMInsecure ) ) ) {
            ymerr("server unknown security init %d %u",command.command,command.userInfo);
            YM_CLOSE_SOCKET(newSocket);
            return false;
        }

        c->securityType = command.userInfo;

        command.command = YMConnectionCommandOkay;
        command.userInfo = 0;

        YM_WRITE_SOCKET(newSocket, (const char *)&command, sizeof(command));
        if ( result != sizeof(command) ) {
            ymerr("server failed to send security ok %zd %d %s",result,error,errorStr);
            YM_CLOSE_SOCKET(newSocket);
            return false;
        }

        ymlog("server initializing security %d",c->securityType);
        security = __YMConnectionInitSecurity(c, newSocket, c->securityType, true);
        if ( ! security ) {
            ymerr("server failed to init security %d",c->securityType);
            YM_CLOSE_SOCKET(newSocket);
            return false;
        }

        // when/if optional, sampling is done serially upon connection before the "line" is released to the client.
        // can't assume client will continuously send/receive data to the point that an accurate sample is gathered,
        // yet don't want client data borrowing throughput from the sample.
        // if ( clientWantsSamplingFastestEtc )
#define THIRTY_TWO_MEGABYTES 33554432
#define SIXTEEN_MEGABYTES ( THIRTY_TWO_MEGABYTES / 2 )
        for ( int i = 0; i < 3; i++ ) {
            uint32_t sampleSize = SIXTEEN_MEGABYTES;
            if      ( i == 0 )  { command.command = YMConnectionCommandIFExchange; command.userInfo = 0; }
            else if ( i == 1 )  { command.command = YMConnectionCommandSample; command.userInfo = sampleSize; }
            else                { command.command = YMConnectionCommandInit; command.userInfo = 0; }
            
            bool okay = YMSecurityProviderWrite(security, (const uint8_t *)&command, sizeof(command));
            if ( ! okay ) {
                ymerr("connection failed to initialize: %d %d %s",i,error,errorStr);
                YMSecurityProviderClose(security);
                return false;
            }
            
            if ( command.command == YMConnectionCommandIFExchange ) {
                ymlog("performing ifinfo exchange");
                conCmdOkay = __YMConnectionDoIFExchange(c, newSocket, true);
            } else if ( command.command == YMConnectionCommandSample ) {
                ymlog("performing sample of length %ub",sampleSize);
                int64_t sample = __YMConnectionDoSample(c, newSocket, sampleSize, true);
                if ( sample >= 0 ) {
                    conCmdOkay = true;
                    c->sample = sample;
                } else {
                    YMSecurityProviderClose(security);
                    return false;
                }
            }
            
            if ( ! conCmdOkay ) {
                ymerr("connection command failed");
                YMSecurityProviderClose(security);
                return false;
            }
        }
        
        
    } else {
        command.command = YMConnectionCommandSecurity;
        command.userInfo = c->securityType;
        YM_WRITE_SOCKET(newSocket, (const char *)&command, sizeof(command));
        if ( result != sizeof(command) ) {
            ymerr("client failed to send security: %zd %d %s",result,error,errorStr);
            YM_CLOSE_SOCKET(newSocket);
            return false;
        }

        YM_READ_SOCKET(newSocket, (char *)&command, sizeof(command));
        if ( result != sizeof(command) ) {
            ymerr("failed to read security response: %zd %d %s",result,error,errorStr);
            return false;
        }

        if ( command.command != YMConnectionCommandOkay ) {
            ymerr("failed to init security: %zd %d %s",result,error,errorStr);
            return false;
        }

        ymlog("client initializing security %d",c->securityType);
        security = __YMConnectionInitSecurity(c,newSocket,c->securityType,false);
        if ( ! security ) {
            ymerr("client failed to init security %d",c->securityType);
            YM_CLOSE_SOCKET(newSocket);
            return false;
        }

        while(1) {

            bool okay = YMSecurityProviderRead(security, (uint8_t *)&command, sizeof(command));
            if ( ! okay ) {
                ymerr("connection failed to initialize: %d %s",error,errorStr);
                YMSecurityProviderClose(security);
                return false;
            }
            
            if ( command.command == YMConnectionCommandSample ) {
                ymlog("performing sample of length %ub",command.userInfo);
                int64_t sample = __YMConnectionDoSample(c, newSocket, command.userInfo, false);
                if ( sample >= 0 ) {
                    conCmdOkay = true;
                    c->sample = sample;
                } else {
                    YMSecurityProviderClose(security);
                    return false;
                }
            } else if ( command.command == YMConnectionCommandIFExchange ) {
                ymlog("performing ifinfo exchange");
                conCmdOkay = __YMConnectionDoIFExchange(c, newSocket, false);
            } else if ( command.command == YMConnectionCommandInit ) {
                ymlog("init command received, proceeding");
                break;
            } else {
                ymerr("unknown initialization command: %d",command.command);
                YMSecurityProviderClose(security);
                return false;
            }
            
            if ( ! conCmdOkay ) {
                ymerr("connection command failed");
                YM_CLOSE_SOCKET(newSocket);
                return false;
            }
        }
    }
    
    plexer = YMPlexerCreate(YMAddressGetDescription(c->address), security, asServer, newSocket);
	YMPlexerSetNewIncomingStreamFunc(plexer, ym_connection_new_stream_proc);
	YMPlexerSetInterruptedFunc(plexer, ym_connection_interrupted_proc);
	YMPlexerSetStreamClosingFunc(plexer, ym_connection_stream_closing_proc);
	YMPlexerSetCallbackContext(plexer, c);

    bool plexerOK = YMPlexerStart(plexer);
    if ( ! plexerOK ) {
        ymerr("plexer failed to initialize");
        goto rewind_fail;
    }
    
    ymlog("new connection: %s (%s) <-> %s (%s)",YMSTR(c->localIFName),
          YMInterfaceTypeDescription(c->localIFType),
          YMSTR(c->remoteIFName),
          YMInterfaceTypeDescription(c->remoteIFType));
    
    c->plexer = plexer;
    c->socket = newSocket;
    
    YMRelease(security);
    return true;
    
rewind_fail:
    if ( security )
        YMRelease(security);
    if ( plexer )
        YMRelease(plexer);
    return false;
}

bool YMConnectionClose(YMConnectionRef c_)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    return __YMConnectionDestroy(c, true);
}

bool __YMConnectionDestroy(__ym_connection_t *c, bool explicit)
{
    YM_IO_BOILERPLATE
    
    bool okay = true;
    if ( c->plexer ) {
        bool plexerOK = YMPlexerStop(c->plexer);
        if ( ! plexerOK ) {
            ymerr("warning: failed to close plexer");
            okay = plexerOK;
        }
        
        YMRelease(c->plexer);
        c->plexer = NULL;
    }
    
    if ( explicit && c->socket != NULL_SOCKET ) {
        YM_CLOSE_SOCKET(c->socket);
        ymerr("connection explicit media close: %d: %zd %d %s",c->socket,result,error,errorStr);
    }
    
    c->socket = NULL_SOCKET;
    
    return okay;
}

uint64_t YMConnectionDoSample(YMConnectionRef connection)
{
    return (uint64_t)connection; // todo
}

YMStringRef YMAPI YMConnectionGetLocalInterfaceName(YMConnectionRef c_)
{
    __ym_connection_t * c = (__ym_connection_t *)c_;
    return c->localIFName;
}

YMInterfaceType YMAPI YMConnectionGetLocalInterface(YMConnectionRef c_)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    return c->localIFType;
}

YMInterfaceType YMAPI YMConnectionGetRemoteInterface(YMConnectionRef c_)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    return c->remoteIFType;
}

int64_t YMAPI YMConnectionGetSample(YMConnectionRef c_)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    return c->sample;
}

YMAddressRef YMConnectionGetAddress(YMConnectionRef c_)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    return c->address;
}

#define YMConnectionStreamInitBuiltinVersion 1
typedef struct _ymconnection_stream_init
{
    uint16_t version;
    uint16_t compressionType;
} _ymconnection_stream_init;
typedef struct _ymconnection_stream_init _ymconnection_stream_init_t;

typedef enum _yconnection_stream_init_response_messages
{
    ymConnectionStreamError = 0,
    ymConnectionStreamOkay = 1,
    ymConnectionStreamVersionUnsupported = 2,
    ymConnectionStreamCompressionUnsupported = 3
} _yconnection_stream_init_response_messages;

typedef struct _ymconnection_stream_init_response
{
    uint16_t message;
} _ymconnection_stream_init_response;
typedef struct _ymconnection_stream_init_response _ymconnection_stream_init_response_t;

YMStreamRef YMAPI YMConnectionCreateStream(YMConnectionRef c_, YMStringRef name, YMCompressionType compression)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    
    if ( NOT_CONNECTED )
        return NULL;
    
    YMStreamRef stream = YMPlexerCreateStream(c->plexer, name);
    
#warning plexer should be changed to call this back such that we can return bool and not need to CloseStream
    if ( ! __YMConnectionInitializeOutgoingStream(c, stream, compression) ) {
        ymerr("outgoing stream \"%s\" failed to initialize",YMSTR(name));
#warning test this case
        YMPlexerCloseStream(c->plexer,stream);
        return NULL;
    }

    return stream;
}

bool __YMConnectionInitializeOutgoingStream(__ym_connection_t *c, YMStreamRef stream, YMCompressionType compression)
{
    bool okay = false;

    _ymconnection_stream_init_t init = { YMConnectionStreamInitBuiltinVersion, compression }; // endian?
    YMIOResult ymResult = YMStreamWriteDown(stream, (const uint8_t *)&init, sizeof(_ymconnection_stream_init_t));
    if ( ymResult != YMIOSuccess ) {
        ymerr("outgoing stream init send failed: %d",ymResult);
        goto catch_return;
    }
    
    ymdbg("%s sent initialization %hu %hu",__FUNCTION__,init.version,init.compressionType);

    okay = _YMStreamSetCompression(stream,compression);
    if ( ! okay ) {
        ymerr("failed to set compression %d for outgoing stream",compression);
        goto catch_return;
    }

    _ymconnection_stream_init_response_t response;
    ymResult = YMStreamReadUp(stream, (uint8_t *)&response, sizeof(response), NULL);
    if ( ymResult != YMIOSuccess ) {
        ymerr("outgoing stream init response failed: %d",ymResult);
        goto catch_return;
    }

    if ( response.message == ymConnectionStreamVersionUnsupported ) {
        ymerr("outgoing stream init failed: unsupported version %d",YMConnectionStreamInitBuiltinVersion);
        goto catch_return;
    } else if ( response.message == ymConnectionStreamCompressionUnsupported ) {
        ymerr("outgoing stream init failed: unsupported compression %d",compression);
        goto catch_return;
    } else if ( response.message == ymConnectionStreamOkay) {
        ymdbg("outgoing stream initialized: v%d %d",YMConnectionStreamInitBuiltinVersion,compression);
        okay = true;
    } else {
        ymerr("outgoing stream init failed: unknown error %d",response.message);
    }

catch_return:
    return okay;
}

bool __YMConnectionInitializeIncomingStream(__ym_connection_t *c, YMStreamRef stream)
{
    bool okay = false;

    _ymconnection_stream_init_t init; // endian?
    YMIOResult ymResult = YMStreamReadUp(stream, (uint8_t *)&init, sizeof(_ymconnection_stream_init_t), NULL);
    if ( ymResult != YMIOSuccess ) {
        ymerr("incoming stream init recv failed: %d",ymResult);
        goto catch_return;
    }

    ymdbg("%s received initialization %hu %hu",__FUNCTION__,init.version,init.compressionType);

    _ymconnection_stream_init_response_t response = { ymConnectionStreamOkay };

    if ( init.version != YMConnectionStreamInitBuiltinVersion ) {
        ymerr("incoming stream version unsupported");
        response.message = ymConnectionStreamVersionUnsupported;
        goto catch_respond;
    }

    if ( ( init.compressionType != YMCompressionNone ) && ( init.compressionType != YMCompressionLZ4) ) {
        ymerr("incoming stream unsupported compression: %d",init.compressionType);
        response.message = ymConnectionStreamCompressionUnsupported;
        goto catch_respond;
    }

    okay = _YMStreamSetCompression(stream,init.compressionType);
    if ( ! okay ) {
        ymerr("failed to set compression %d for incoming stream",init.compressionType);
        response.message = ymConnectionStreamError;
        goto catch_respond;
    }

catch_respond:
    ymResult = YMStreamWriteDown(stream,(const uint8_t *)&response,sizeof(response));
    if ( ymResult != YMIOSuccess ) {
        ymerr("incoming stream response send failed: %d (%hu)",ymResult,response.message);
    }

    okay = ( response.message == ymConnectionStreamOkay );

    if ( okay ) {
        ymdbg("incoming stream initialized: v%d %d",YMConnectionStreamInitBuiltinVersion,init.compressionType);
    }

catch_return:
    return okay;
}

void YMConnectionCloseStream(YMConnectionRef c_, YMStreamRef stream)
{
    __ym_connection_t *c = (__ym_connection_t *)c_;
    YMPlexerCloseStream(c->plexer, stream);
}

typedef struct __ym_connection_forward_callback_t
{
    YMConnectionRef connection;
    YMStreamRef stream;
    YMFILE file;
    bool fileToStream;
    bool bounded;
    ym_connection_forward_context_t *userContext;
} __ym_connection_forward_callback_t;

bool YMConnectionForwardFile(YMConnectionRef c, YMFILE fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo)
{
    return __YMConnectionForward(c, false, toStream, fromFile, nBytesPtr, sync, callbackInfo);
}

bool YMConnectionForwardStream(YMConnectionRef c, YMStreamRef fromStream, YMFILE toFile, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo)
{
    return __YMConnectionForward(c, true, fromStream, toFile, nBytesPtr, sync, callbackInfo);
}

bool __YMConnectionForward(YMConnectionRef c, bool toFile, YMStreamRef stream, YMFILE file, const uint64_t *nBytesPtr, bool sync, ym_connection_forward_context_t *callbackInfo)
{
    __ym_connection_forward_callback_t *myContext = NULL;
    ym_forward_file_t *threadContext = NULL;
    
    if ( ! sync ) {
        myContext = YMALLOC(sizeof(struct __ym_connection_forward_callback_t));
        myContext->connection = YMRetain(c);
        myContext->stream = YMRetain(stream);
        myContext->file = file;
        myContext->fileToStream = ! toFile;
        myContext->bounded = ( nBytesPtr != NULL );
        myContext->userContext = callbackInfo;
        
        threadContext = YMALLOC(sizeof(ym_forward_file_t));
        threadContext->callback = _ym_connection_forward_callback_proc;
        threadContext->context = myContext;
    }
    
    bool ret;
    if ( toFile )
        ret = YMDispatchForwardStream(YMDispatchGetGlobalQueue(), stream, file, nBytesPtr, sync, threadContext);
    else
        ret = YMDispatchForwardFile(YMDispatchGetGlobalQueue(), file, stream, nBytesPtr, sync, threadContext);
    
    return ret;
}

void _ym_connection_forward_callback_proc(void *context, YMIOResult result, uint64_t bytesForwarded)
{
    __ym_connection_forward_callback_t *myContext = (__ym_connection_forward_callback_t *)context;
    YMConnectionRef c = myContext->connection;
    YMStreamRef stream = myContext->stream;
    
    // allow user to async-forward, if they don't specify callback info it implies "close stream for me when done"
    if ( ! myContext->userContext ) {
        ymerr("automatically closing stream %p after async forward",myContext->stream);
        YMConnectionCloseStream(c, stream);
    }
    else if ( myContext->userContext->callback )
        myContext->userContext->callback(c, stream, result, bytesForwarded, myContext->userContext->context);
    
    YMRelease(c);
    YMRelease(stream);
    YMFREE(myContext->userContext);
    YMFREE(myContext);
}

void ym_connection_new_stream_proc(__unused YMPlexerRef plexer,YMStreamRef stream, void *context)
{
    __ym_connection_t *c = (__ym_connection_t *)context;
    ymdbg("%s %p %p %p",__FUNCTION__,plexer,stream,context);

    bool okay = __YMConnectionInitializeIncomingStream(c,stream);
    if ( ! okay ) {
        #warning plexer should be changed to call this back such that we can return bool and not need to CloseStream
        YMPlexerCloseStream(plexer,stream);
        return;
    }

    if ( c->newFunc )
        c->newFunc(c, stream, c->newFuncContext);
}

void ym_connection_stream_closing_proc(__unused YMPlexerRef plexer, YMStreamRef stream, void *context)
{
    __ym_connection_t *c = (__ym_connection_t *)context;
    if ( c->closingFunc )
        c->closingFunc(c, stream, c->closingFuncContext);
}

void ym_connection_interrupted_proc(__unused YMPlexerRef plexer, void *context)
{
    __ym_connection_t *c = (__ym_connection_t *)context;
    if ( c->interruptedFunc )
        c->interruptedFunc(c, c->interruptedFuncContext);
}

YM_EXTERN_C_POP
