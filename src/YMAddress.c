//
//  YMAddress.c
//  yammer
//
//  Created by david on 11/11/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMAddress.h"

#include "YMUtilities.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogDefault
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#include <sys/socket.h>

// protocols
#include <netinet/in.h>
#include <arpa/inet.h>

#define YM_IPV4_LEN sizeof(struct sockaddr_in)
#define YM_IPV6_LEN sizeof(struct sockaddr_in6)

#define YM_IS_IPV4(x) ( ( x->sa_family == AF_INET ) && ( x->sa_len == YM_IPV4_LEN ) )
#define YM_IS_IPV6(x) ( ( x->sa_family == AF_INET6 ) && ( x->sa_len == YM_IPV6_LEN ) )

typedef struct __YMAddress
{
    YMTypeID _type;
    
    YMAddressType type;
    struct sockaddr *address;
    socklen_t length;
    int family;
    
    const char *description;
} _YMAddress;

YMAddressRef YMAddressCreate(void* addressData, socklen_t length)
{
    struct sockaddr *addr = addressData;
    
    YMAddressType type;
    bool isIP = false;
    bool isIPV4 = false;
    if( YM_IS_IPV4(addr) )
    {
        type = YMAddressIPV4;
        isIP = isIPV4 = true;
    }
    else if ( YM_IS_IPV6(addr) )
    {
        type = YMAddressIPV6;
        isIP = true;
    }
    else
    {
        ymlog("address: warning: yammer doesn't support address family %d",addr->sa_family);
        return NULL;
    }
    
    YMAddressRef address = (YMAddressRef)YMALLOC(sizeof(struct __YMAddress));
    address->_type = _YMAddressTypeID;
    
    address->type = type;
    address->address = YMALLOC(length);
    memcpy(address->address, addressData, length);
    address->length = length;
    
    if ( type == YMAddressIPV4 || type == YMAddressIPV6 )
    {
        struct sockaddr_in *inAddr = (struct sockaddr_in *)address->address;
        char *ipString = inet_ntoa( inAddr->sin_addr );
        if ( ! ipString ) // i can't imagine how this would fail short of segfaulting, man isn't specific
        {
            uint8_t *ipPtr = (uint8_t *)&(inAddr->sin_addr.s_addr);
            ymerr("address: error: inet_ntoa failed for %u.%u.%u.%u: %d (%s)",ipPtr[0],ipPtr[1],ipPtr[2],ipPtr[4],errno,strerror(errno));
            goto rewind_fail;
        }
        uint16_t port = inAddr->sin_port;
        address->description = YMStringCreateWithFormat("%s:%u",ipString,port);
    }
    else if ( isIP )
    {
        int family = ( isIPV4 ) ? AF_INET : AF_INET6;
        socklen_t ipLength = isIPV4 ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN;
        char ipString[ipLength];
        if ( ! inet_ntop(family, address->address, ipString, ipLength) )
        {
            ymerr("address: error: inet_ntop failed for address length %d",ipLength);
            goto rewind_fail;
        }
        uint16_t port;
        if ( isIPV4 )
            port = ((struct sockaddr_in *)addr)->sin_port;
        else
            port = ((struct sockaddr_in6 *)addr)->sin6_port;
        address->description = YMStringCreateWithFormat("%s:%u",ipString,port);
    }
    
    return address;
    
rewind_fail:
    free(address->address);
    free(address);
    return NULL;
}

YMAddressRef YMAddressCreateLocalHostIPV4(uint16_t port)
{
    uint8_t ip[4] = { 127, 0, 0, 1 };
    
    uint8_t length = sizeof(struct sockaddr_in);
    struct sockaddr_in *newAddr = YMALLOC(length);
    newAddr->sin_family = AF_INET;
    memcpy(&newAddr->sin_addr.s_addr,ip,sizeof(ip));
    newAddr->sin_port = port;
    newAddr->sin_len = length;
    
    YMAddressRef address = YMAddressCreate(newAddr, length); // todo could be optimized
    free(newAddr);
    return address;
}

void _YMAddressFree(YMTypeRef object)
{
    YMAddressRef address = (YMAddressRef)object;
    free((void *)address->address);
    free ((void *)address->description);
    free(address);
}

const void *YMAddressGetAddressData(YMAddressRef address)
{
    return address->address;
}

int YMAddressGetLength(YMAddressRef address)
{
    return address->length;
}

const char *YMAddressGetDescription(YMAddressRef address)
{
    return address->description;
}

int YMAddressGetDomain(YMAddressRef address)
{
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

int YMAddressGetAddressFamily(YMAddressRef address)
{
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
            return PF_INET;
        case AF_INET6:
            return PF_INET6;
        default: ;
    }
    
    return -1;
}
