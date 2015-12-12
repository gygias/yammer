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

YM_EXTERN_C_PUSH

YMPeerRef _YMPeerCreateWithAddress(YMAddressRef address);
YMPeerRef _YMPeerCreate(YMStringRef name, YMArrayRef addresses, YMArrayRef certificates);

void _YMPeerSetName(YMPeerRef peer, YMStringRef name);
void _YMPeerSetAddresses(YMPeerRef peer, YMArrayRef addresses);
void _YMPeerSetPort(YMPeerRef peer, uint16_t port);
void _YMPeerSetCertificates(YMPeerRef peer, YMArrayRef certificates);

YM_EXTERN_C_POP

#endif /* YMPeerPriv_h */
