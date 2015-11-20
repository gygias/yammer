//
//  YMPeer.m
//  yammer
//
//  Created by david on 11/19/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YMPeer.h"
#import "YMPeerPriv.h"

@interface YMPeer ()

@property (nonatomic) YMPeerRef peerRef;

@end

@implementation YMPeer

- (NSString *)name
{
    return [NSString stringWithUTF8String:YMSTR(YMPeerGetName(self.peerRef))];
}

- (NSData *)publicKeyData
{
    return nil;
}

- (id)_initWithPeerRef:(YMPeerRef)peerRef
{
    if ( ( self = [super init] ) )
    {
        self.peerRef = peerRef;
    }
    return self;
}

- (YMPeerRef)_peerRef
{
    return self.peerRef;
}

@end
