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
YMPeerRef _YMPeerCreate(const char *name, YMDictionaryRef addresses, YMDictionaryRef certificates);

void _YMPeerSetName(YMPeerRef peer, const char *name);
void _YMPeerSetAddresses(YMDictionaryRef addresses);
void _YMPeerSetCertificates(YMDictionaryRef certificates);

#endif /* YMPeerPriv_h */
