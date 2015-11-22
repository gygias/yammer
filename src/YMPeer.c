//
//  YMPeer.c
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPeer.h"

typedef struct __ym_peer
{
    _YMType _type;
    
    YMStringRef name;
    YMDictionaryRef addresses;
    uint16_t port;
    YMDictionaryRef certificates;
} ___ym_peer;
typedef struct __ym_peer __YMPeer;
typedef __YMPeer *__YMPeerRef;

YMPeerRef __YMPeerCreate(YMStringRef name, YMDictionaryRef addresses, YMDictionaryRef certificates);

YMPeerRef _YMPeerCreateWithAddress(YMAddressRef address)
{
    YMDictionaryRef dictionary = YMDictionaryCreate();
    YMDictionaryAdd(dictionary, (YMDictionaryKey)address, address);
    return __YMPeerCreate(NULL,dictionary,NULL);
}

YMPeerRef _YMPeerCreate(YMStringRef name, YMDictionaryRef addresses, YMDictionaryRef certificates)
{
    return __YMPeerCreate(name, addresses, certificates);
}

YMPeerRef __YMPeerCreate(YMStringRef name, YMDictionaryRef addresses, YMDictionaryRef certificates)
{
    __YMPeerRef peer = (__YMPeerRef)_YMAlloc(_YMPeerTypeID,sizeof(__YMPeer));
    
    peer->name = name ? YMRetain(name) : YMSTRC("unnamed-peer");
    peer->addresses = addresses;
    peer->certificates = certificates;
    
    return peer;
}

void _YMPeerFree(YMTypeRef object)
{
    __YMPeerRef peer = (__YMPeerRef)object;
    
    if ( peer->addresses )
    {
        YMDictionaryEnumRef aEnum = YMDictionaryEnumeratorBegin(peer->addresses);
        while ( aEnum )
        {
            YMRelease(aEnum->value);
            aEnum = YMDictionaryEnumeratorGetNext(aEnum);
        }
        if ( aEnum ) YMDictionaryEnumeratorEnd(aEnum);
        YMRelease(peer->addresses);
    }
    if ( peer->certificates )
    {
        YMDictionaryEnumRef aEnum = YMDictionaryEnumeratorBegin(peer->certificates);
        while ( aEnum )
        {
            YMRelease(aEnum->value);
            aEnum = YMDictionaryEnumeratorGetNext(aEnum);
        }
        if ( aEnum ) YMDictionaryEnumeratorEnd(aEnum);
        YMRelease(peer->certificates);
    }
    
    YMRelease(peer->name);
}

YMStringRef YMPeerGetName(YMPeerRef peer_)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    return peer->name;
}

YMDictionaryRef YMPeerGetAddresses(YMPeerRef peer_)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    return peer->addresses;
}

uint16_t YMPeerGetPort(YMPeerRef peer_)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    return peer->port;
}

YMDictionaryRef YMPeerGetCertificates(YMPeerRef peer_)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    return peer->certificates;
}

void _YMPeerSetName(YMPeerRef peer_, YMStringRef name)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    peer->name = YMRetain(name);
}

void _YMPeerSetAddresses(YMPeerRef peer_, YMDictionaryRef addresses)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    peer->addresses = YMRetain(addresses);
}

void _YMPeerSetPort(YMPeerRef peer_, uint16_t port)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    peer->port = port;
}

void _YMPeerSetCertificates(YMPeerRef peer_, YMDictionaryRef certificates)
{
    __YMPeerRef peer = (__YMPeerRef)peer_;
    peer->certificates = YMRetain(certificates);
}
