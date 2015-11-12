//
//  YMPeer.h
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPeer_h
#define YMPeer_h

#include "YMDictionary.h"
#include "YMAddress.h"

typedef struct __YMPeer *YMPeerRef;

const char *YMPeerGetName(YMPeerRef);
YMDictionaryRef YMPeerGetAddresses(YMPeerRef);
YMDictionaryRef YMPeerGetCertificates(YMPeerRef);

#endif /* YMPeer_h */
