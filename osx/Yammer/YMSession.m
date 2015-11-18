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

@property (nonatomic,copy) YMSessionInterruptedHandler interruptHandler;
@property (nonatomic,copy) YMSessionNewServiceHandler serviceHandler;
@property (nonatomic,copy) YMSessionNewStreamHandler streamHandler;
@property (nonatomic,copy) YMSessionStreamClosingHandler streamClosingHandler;

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

- (BOOL)startServer
{
    return YES;
}

- (void)browseServicesWithHandler:(YMSessionNewServiceHandler)handler
{
    
}

- (BOOL)connectToServiceNamed:(NSString *)name
{
    return YES;
}

@end
