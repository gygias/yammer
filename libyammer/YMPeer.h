//
//  YMPeer.h
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPeer_h
#define YMPeer_h

#include <libyammer/YMDictionary.h>
#include <libyammer/YMPeer.h>
#include <libyammer/YMAddress.h>

typedef YMTypeRef YMPeerRef;

YMStringRef YMPeerGetName(YMPeerRef peer);
YMDictionaryRef YMPeerGetAddresses(YMPeerRef peer);
uint16_t YMPeerGetPort(YMPeerRef peer);
YMDictionaryRef YMPeerGetCertificates(YMPeerRef peer);

#endif /* YMPeer_h */
