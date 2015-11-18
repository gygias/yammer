//
//  YMSession.m
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YMSession.h"

@interface YMSession ()

@property (nonatomic,copy) NSString *type;
@property (nonatomic,copy) NSString *name;

@property (nonatomic,copy) YMSessionNewConnectionHandler connectionHandler;
@property (nonatomic,copy) YMSessionNewPeerHandler serviceHandler;
@property (nonatomic,copy) YMSessionNewStreamHandler streamHandler;
@property (nonatomic,copy) YMSessionStreamClosingHandler streamClosingHandler;
@property (nonatomic,copy) YMSessionInterruptedHandler interruptHandler;

@property (nonatomic) YMSessionRef ymsession;

@end

@implementation YMSession

- (id)initWithType:(NSString *)type name:(NSString *)name
{
    if ( ( self = [super init] ) )
    {
        self.type = type;
        self.name = name;
        
        // todo: since we're the 'friendly objc' wrapper, should probably check args rather than crash in the c lib
    }
    return self;
}

- (void)setInterruptionHandler:(YMSessionInterruptedHandler)handler
{
    self.interruptHandler = handler;
}

- (BOOL)startAdvertisingWithName:(NSString *)name
{
    if ( ! self.ymsession )
        return NO;
    return YMSessionStartAdvertising(self.ymsession, YMSTRC([name UTF8String]));
}

- (BOOL)browsePeersWithHandler:(YMSessionNewPeerHandler)handler
{
    if ( ! self.ymsession )
        return NO;
    
    self.serviceHandler = handler;
    return YMSessionStartBrowsing(self.ymsession);
}

- (BOOL)connectToPeerNamed:(NSString *)name handler:(YMSessionNewConnectionHandler)handler
{
    if ( ! self.ymsession )
        return NO;
    
    YMPeerRef peer = YMSessionGetPeerNamed(self.ymsession, YMSTRC([name UTF8String]));
    if ( ! peer )
        return NO;
    
    self.connectionHandler = handler;
    return YMSessionConnectToPeer(self.ymsession, peer, false);
}

@end
