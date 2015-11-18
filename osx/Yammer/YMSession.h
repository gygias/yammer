//
//  YMSession.h
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <Yammer/Yammer.h>

@interface YMSession : NSObject

typedef void (^YMSessionInterruptedHandler)(YMSession *session);
typedef void (^YMSessionNewServiceHandler)(YMSession *session, NSString *name);
typedef void (^YMSessionNewStreamHandler)(YMSession *session, YMConnection *connection, YMStream *stream);
typedef void (^YMSessionStreamClosingHandler)(YMSession *session, YMConnection *connection, YMStream *stream);

- (id)initWithType:(NSString *)type name:(NSString *)name;

- (void)setInterruptionHandler:(YMSessionInterruptedHandler)handler;

- (BOOL)startServer;

- (void)browseServicesWithHandler:(YMSessionNewServiceHandler)handler;
- (BOOL)connectToServiceNamed:(NSString *)name;

@end
