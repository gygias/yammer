//
//  YMPeer.c
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPeer.h"

typedef struct __YMPeer
{
    YMTypeID _type;
    
    const char *name;
    YMDictionaryRef addresses;
    uint16_t port;
    YMDictionaryRef certificates;
} _YMPeer;

YMPeerRef __YMPeerCreate(const char *name, YMDictionaryRef addresses, YMDictionaryRef certificates);

YMPeerRef _YMPeerCreateWithAddress(YMAddressRef address)
{
    YMDictionaryRef dictionary = YMDictionaryCreate();
    YMDictionaryAdd(dictionary, (YMDictionaryKey)address, address);
    return __YMPeerCreate(NULL,dictionary,NULL);
}

YMPeerRef _YMPeerCreate(const char *name, YMDictionaryRef addresses, YMDictionaryRef certificates)
{
    return __YMPeerCreate(name, addresses, certificates);
}

YMPeerRef __YMPeerCreate(const char *name, YMDictionaryRef addresses, YMDictionaryRef certificates)
{
    YMPeerRef peer = (YMPeerRef)YMALLOC(sizeof(struct __YMPeer));
    peer->_type = _YMPeerTypeID;
    
    peer->name = name ? strdup(name) : NULL;
    peer->addresses = addresses;
    peer->certificates = certificates;
    
    return peer;
}

void _YMPeerFree(YMTypeRef object)
{
    YMPeerRef peer = (YMPeerRef)object;
    if ( peer->addresses )
        YMFree(peer->addresses);
    if ( peer->certificates )
        YMFree(peer->certificates);
    if ( peer->name )
        free((void *)peer->name);
    free(peer);
}

const char *YMPeerGetName(YMPeerRef peer)
{
    return peer->name;
}

YMDictionaryRef YMPeerGetAddresses(YMPeerRef peer)
{
    return peer->addresses;
}

uint16_t YMPeerGetPort(YMPeerRef peer)
{
    return peer->port;
}

YMDictionaryRef YMPeerGetCertificates(YMPeerRef peer)
{
    return peer->certificates;
}

void _YMPeerSetName(YMPeerRef peer, const char *name)
{
    peer->name = strdup(name);
}

void _YMPeerSetAddresses(YMPeerRef peer, YMDictionaryRef addresses)
{
    peer->addresses = addresses;
}

void _YMPeerSetPort(YMPeerRef peer, uint16_t port)
{
    peer->port = port;
}

void _YMPeerSetCertificates(YMPeerRef peer, YMDictionaryRef certificates)
{
    peer->certificates = certificates;
}
