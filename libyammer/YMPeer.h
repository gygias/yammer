//
//  YMPeer.h
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPeer_h
#define YMPeer_h

YM_EXTERN_C_PUSH

#include <libyammer/YMArray.h>
#include <libyammer/YMPeer.h>
#include <libyammer/YMAddress.h>

typedef const struct __ym_peer * YMPeerRef;

YMStringRef YMAPI YMPeerGetName(YMPeerRef peer);
YMArrayRef YMAPI YMPeerGetAddresses(YMPeerRef peer);
uint16_t YMAPI YMPeerGetPort(YMPeerRef peer);
YMArrayRef YMAPI YMPeerGetCertificates(YMPeerRef peer);

YM_EXTERN_C_POP

#endif /* YMPeer_h */
