//
//  YMPeer.h
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPeer_h
#define YMPeer_h

#ifdef __cplusplus
extern "C" {
#endif

#include <libyammer/YMDictionary.h>
#include <libyammer/YMPeer.h>
#include <libyammer/YMAddress.h>

typedef const struct __ym_peer_t *YMPeerRef;

YMAPI YMStringRef YMPeerGetName(YMPeerRef peer);
YMAPI YMDictionaryRef YMPeerGetAddresses(YMPeerRef peer);
YMAPI uint16_t YMPeerGetPort(YMPeerRef peer);
YMAPI YMDictionaryRef YMPeerGetCertificates(YMPeerRef peer);

#ifdef __cplusplus
}
#endif

#endif /* YMPeer_h */
