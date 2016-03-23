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
        self.peerRef = YMRetain(peerRef);
    }
    return self;
}

- (void)dealloc
{
    if ( self.peerRef )
        YMRelease(self.peerRef);
    
    //[super dealloc]; // arc
}

- (YMPeerRef)_peerRef
{
    return self.peerRef;
}

@end
