//
//  YMAddress.c
//  yammer
//
//  Created by david on 11/11/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifdef WIN32
#include "YMBase.h"
#include "YMInternal.h"
#endif

#include "YMAddress.h"
#include "YMUtilities.h"

#include "YMLog.h"

#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h> // protocols
# if defined (YMLINUX)
# define __USE_MISC
# endif
#include <arpa/inet.h>
#else
#define _WINSOCK_DEPRECATED_NO_WARNINGS // todo, inet_ntoa
#include <winsock2.h>
#include <ws2tcpip.h>
//typedef unsigned __int32 socklen_t;
#endif

YM_EXTERN_C_PUSH

#define YM_IPV4_LEN sizeof(struct sockaddr_in)
#define YM_IPV6_LEN sizeof(struct sockaddr_in6)

#define YM_IS_IPV4(x) ( x->sa_family == AF_INET )
#define YM_IS_IPV6(x) ( x->sa_family == AF_INET6 )

typedef struct __ym_address
{
    _YMType _type;
    
    YMAddressType type;
    struct sockaddr *address;
    socklen_t length;
    int family;
    
    YMStringRef description;
} ___ym_address;
typedef struct __ym_address __YMAddress;
typedef __YMAddress *__YMAddressRef;

YMAddressRef YMAddressCreate(const void *addressData, uint32_t length)
{
    const struct sockaddr *addr = addressData;
    
    YMAddressType type;
    bool isIP = false;
    bool isIPV4 = false;
    if( YM_IS_IPV4(addr) )
    {
        type = YMAddressIPV4;
        isIP = isIPV4 = true;
    }
#ifndef WIN32
    else if ( YM_IS_IPV6(addr) )
    {
        type = YMAddressIPV6;
        isIP = true;
    }
#endif
    else
    {
        ymlog("address: warning: yammer doesn't support address family %d",addr->sa_family);
        return NULL;
    }
    
    __YMAddressRef address = (__YMAddressRef)_YMAlloc(_YMAddressTypeID,sizeof(__YMAddress));
    
    address->type = type;
    address->address = YMALLOC(length);
    memcpy(address->address, addressData, length);
    address->length = length;
    
    if ( isIP )
    {
        int family = isIPV4 ? AF_INET : AF_INET6;
        socklen_t ipLength = isIPV4 ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN;
        void *in46_addr = isIPV4 ? (void *)&((struct sockaddr_in *)addr)->sin_addr : (void *)&((struct sockaddr_in6 *)addr)->sin6_addr;
        char ipString[INET6_ADDRSTRLEN];
        
        if ( ! inet_ntop(family, in46_addr, ipString, ipLength) )
        {
            ymerr("address: error: inet_ntop failed for address length %d",ipLength);
            goto rewind_fail;
        }
        uint16_t hostPort;
        if ( isIPV4 )
            hostPort = ntohs(((struct sockaddr_in *)addr)->sin_port);
        else
            hostPort = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
        address->description = YMStringCreateWithFormat("%s:%u",ipString,hostPort,NULL);
    }
    
    return (YMAddressRef)address;
    
rewind_fail:
    free(address->address);
    free(address);
    return NULL;
}

YMAddressRef YMAddressCreateLocalHostIPV4(uint16_t port)
{
    uint32_t localhost = INADDR_LOOPBACK;
    
    uint8_t length = sizeof(struct sockaddr_in);
    struct sockaddr_in *newAddr = YMALLOC(length);
    newAddr->sin_family = AF_INET;
    newAddr->sin_addr.s_addr = localhost;
    newAddr->sin_port = htons(port);
#ifdef YMMACOS
    newAddr->sin_len = length;
#endif
    
    YMAddressRef address = YMAddressCreate(newAddr, length); // todo could be optimized
    free(newAddr);
    return address;
}

YMAddressRef YMAddressCreateWithIPStringAndPort(YMStringRef ipString, uint16_t port)
{
    struct in_addr inAddr = {0};
	int result = inet_pton(AF_INET, YMSTR(ipString), &inAddr);
    if ( result != 1 )
    {
        ymlog("address: failed to parse '%s' (%u)",YMSTR(ipString),port);
        return NULL;
    }
    
    struct sockaddr_in sinAddr;
    bzero(&sinAddr, sizeof(sinAddr));
	socklen_t addrLen = sizeof(struct sockaddr_in);
#ifdef YMMACOS
    sinAddr.sin_len = (uint8_t)addrLen;
#endif
    sinAddr.sin_family = AF_INET;
    sinAddr.sin_port = htons(port);
    sinAddr.sin_addr.s_addr = inAddr.s_addr;
    
    return YMAddressCreate(&sinAddr, addrLen);
}

void _YMAddressFree(YMTypeRef object)
{
    __YMAddressRef address = (__YMAddressRef)object;
    free((void *)address->address);
    YMRelease(address->description);
}

const void *YMAddressGetAddressData(YMAddressRef address_)
{
    __YMAddressRef address = (__YMAddressRef)address_;
    return address->address;
}

int YMAddressGetLength(YMAddressRef address_)
{
    __YMAddressRef address = (__YMAddressRef)address_;
    return address->length;
}

YMStringRef YMAddressGetDescription(YMAddressRef address_)
{
    __YMAddressRef address = (__YMAddressRef)address_;
    return address->description;
}

int YMAddressGetDomain(YMAddressRef address_)
{
    __YMAddressRef address = (__YMAddressRef)address_;
    switch(address->type)
    {
        case YMAddressIPV4:
            return PF_INET;
        case YMAddressIPV6:
            return PF_INET6;
        default: ;
    }
    return -1;
}

int YMAddressGetAddressFamily(YMAddressRef address_)
{
    __YMAddressRef address = (__YMAddressRef)address_;
    switch(address->type)
    {
        case YMAddressIPV4:
            return AF_INET;
        case YMAddressIPV6:
            return AF_INET6;
        default: ;
    }
    return -1;
}

int YMAddressGetDefaultProtocolForAddressFamily(int addressFamily)
{
    switch(addressFamily)
    {
        case AF_INET:
            return IPPROTO_TCP;
        default: ;
    }
    
    return -1;
}

YM_EXTERN_C_POP
