//
//  YMPeer.c
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPeer.h"

YM_EXTERN_C_PUSH

typedef struct __ym_peer_t
{
    _YMType _type;
    
    YMStringRef name;
    YMArrayRef addresses;
    uint16_t port;
    YMArrayRef certificates;
} __ym_peer_t;
typedef struct __ym_peer_t *__YMPeerRef;

YMPeerRef __YMPeerCreate(YMStringRef name, YMArrayRef addresses, YMArrayRef certificates);

YMPeerRef _YMPeerCreateWithAddress(YMAddressRef address)
{
    YMArrayRef array = YMArrayCreate();
    YMArrayAdd(array, address);
    YMRetain(address);
    YMPeerRef peer = __YMPeerCreate(NULL,array,NULL);
    YMRelease(array);
    return peer;
}

YMPeerRef _YMPeerCreate(YMStringRef name, YMArrayRef addresses, YMArrayRef certificates)
{
    return __YMPeerCreate(name, addresses, certificates);
}

YMPeerRef __YMPeerCreate(YMStringRef name, YMArrayRef addresses, YMArrayRef certificates)
{
    __YMPeerRef peer = (__YMPeerRef)_YMAlloc(_YMPeerTypeID,sizeof(struct __ym_peer_t));
    
    peer->name = name ? YMRetain(name) : YMSTRC("unnamed-peer");
    peer->addresses = addresses ? YMRetain(addresses) : NULL;
    peer->certificates = certificates ? YMRetain(certificates) : NULL;
    
    return peer;
}

void _YMPeerFree(YMTypeRef object)
{
    __YMPeerRef peer = (__YMPeerRef)object;
    
    if ( peer->addresses ) {
        _YMArrayRemoveAll(peer->addresses, true, false);
        YMRelease(peer->addresses);
    }
    
    if ( peer->certificates ) {
        _YMArrayRemoveAll(peer->certificates, true, false);
        YMRelease(peer->certificates);
    }
    
    YMRelease(peer->name);
}

YMStringRef YMPeerGetName(YMPeerRef peer_)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    return peer->name;
}

YMArrayRef YMPeerGetAddresses(YMPeerRef peer_)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    return peer->addresses;
}

uint16_t YMPeerGetPort(YMPeerRef peer_)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    return peer->port;
}

YMArrayRef YMPeerGetCertificates(YMPeerRef peer_)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    return peer->certificates;
}

void _YMPeerSetName(YMPeerRef peer_, YMStringRef name)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    peer->name = YMRetain(name);
}

void _YMPeerSetAddresses(YMPeerRef peer_, YMArrayRef addresses)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    peer->addresses = YMRetain(addresses);
}

void _YMPeerSetPort(YMPeerRef peer_, uint16_t port)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    peer->port = port;
}

void _YMPeerSetCertificates(YMPeerRef peer_, YMArrayRef certificates)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    peer->certificates = YMRetain(certificates);
}

YM_EXTERN_C_POP
