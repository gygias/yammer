//
//  YMPeerPriv.h
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPeerPriv_h
#define YMPeerPriv_h

#include "YMPeer.h"

YMPeerRef _YMPeerCreateWithAddress(YMAddressRef address);
YMPeerRef _YMPeerCreate(YMStringRef name, YMDictionaryRef addresses, YMDictionaryRef certificates);

void _YMPeerSetName(YMPeerRef peer, YMStringRef name);
void _YMPeerSetAddresses(YMPeerRef peer, YMDictionaryRef addresses);
void _YMPeerSetPort(YMPeerRef peer, uint16_t port);
void _YMPeerSetCertificates(YMPeerRef peer, YMDictionaryRef certificates);

#endif /* YMPeerPriv_h */
