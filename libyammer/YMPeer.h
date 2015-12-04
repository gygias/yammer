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

YMStringRef YMAPI YMPeerGetName(YMPeerRef peer);
YMDictionaryRef YMAPI YMPeerGetAddresses(YMPeerRef peer);
uint16_t YMAPI YMPeerGetPort(YMPeerRef peer);
YMDictionaryRef YMAPI YMPeerGetCertificates(YMPeerRef peer);

#ifdef __cplusplus
}
#endif

#endif /* YMPeer_h */
