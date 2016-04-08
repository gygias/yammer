//
//  YMPeer.c
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPeer.h"

YM_EXTERN_C_PUSH

typedef struct __ym_peer
{
    _YMType _type;
    
    YMStringRef name;
    YMArrayRef addresses;
    uint16_t port;
    YMArrayRef certificates;
} __ym_peer;
typedef struct __ym_peer __ym_peer_t;

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
    __ym_peer_t *p = (__ym_peer_t *)_YMAlloc(_YMPeerTypeID,sizeof(__ym_peer_t));
    
    p->name = name ? YMRetain(name) : YMSTRC("unnamed-peer");
    p->addresses = addresses ? YMRetain(addresses) : NULL;
    p->certificates = certificates ? YMRetain(certificates) : NULL;
    
    return p;
}

void _YMPeerFree(YMTypeRef o_)
{
    __ym_peer_t *p = (__ym_peer_t *)o_;
    
    if ( p->addresses ) {
        _YMArrayRemoveAll(p->addresses, true, false);
        YMRelease(p->addresses);
    }
    
    if ( p->certificates ) {
        _YMArrayRemoveAll(p->certificates, true, false);
        YMRelease(p->certificates);
    }
    
    YMRelease(p->name);
}

YMStringRef YMPeerGetName(YMPeerRef p)
{
    return p->name;
}

YMArrayRef YMPeerGetAddresses(YMPeerRef p)
{
    return p->addresses;
}

uint16_t YMPeerGetPort(YMPeerRef p)
{
    return p->port;
}

YMArrayRef YMPeerGetCertificates(YMPeerRef p)
{
    return p->certificates;
}

void _YMPeerSetName(YMPeerRef p, YMStringRef name)
{
    ((__ym_peer_t *)p)->name = YMRetain(name);
}

void _YMPeerSetAddresses(YMPeerRef p, YMArrayRef addresses)
{
    ((__ym_peer_t *)p)->addresses = YMRetain(addresses);
}

void _YMPeerSetPort(YMPeerRef p, uint16_t port)
{
    ((__ym_peer_t *)p)->port = port;
}

void _YMPeerSetCertificates(YMPeerRef p, YMArrayRef certificates)
{
    ((__ym_peer_t *)p)->certificates = YMRetain(certificates);
}

YM_EXTERN_C_POP
