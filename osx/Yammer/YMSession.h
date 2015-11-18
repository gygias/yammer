//
//  YMSession.h
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <Yammer/Yammer.h>

@interface YMSession : NSObject

typedef void (^YMSessionNewPeerHandler)(YMSession *session, NSString *name);
typedef void (^YMSessionNewConnectionHandler)(YMSession *session, YMConnection *connection);
typedef void (^YMSessionNewStreamHandler)(YMSession *session, YMConnection *connection, YMStream *stream);
typedef void (^YMSessionStreamClosingHandler)(YMSession *session, YMConnection *connection, YMStream *stream);
typedef void (^YMSessionInterruptedHandler)(YMSession *session);

- (id)initWithType:(NSString *)type name:(NSString *)name;

- (void)setInterruptionHandler:(YMSessionInterruptedHandler)handler;

- (BOOL)startAdvertisingWithName:(NSString *)name;

- (BOOL)browsePeersWithHandler:(YMSessionNewPeerHandler)handler;
- (BOOL)connectToPeerNamed:(NSString *)name handler:(YMSessionNewConnectionHandler)handler;

@end
