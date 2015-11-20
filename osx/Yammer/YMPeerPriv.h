//
//  YMPeerPriv.h
//  yammer
//
//  Created by david on 11/19/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPeerPriv_h
#define YMPeerPriv_h

#import <libyammer/YMPeer.h>

@interface YMPeer (Private)

- (id)_initWithPeerRef:(YMPeerRef)peerRef;
- (YMPeerRef)_peerRef;

@end

#endif /* YMPeerPriv_h */
