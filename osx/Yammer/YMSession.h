//
//  YMSession.h
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <Yammer/Yammer.h>

@interface YMSession : NSObject

typedef void (^YMSessionPeerDiscoveredHandler)(YMSession *session, YMPeer *peer);
typedef void (^YMSessionPeerDisappearedHandler)(YMSession *session, YMPeer *peer);

typedef bool (^YMSessionShouldAcceptConnectionHandler)(YMSession *session, YMPeer *connection);

typedef void (^YMSessionConnectionFailedHandler)(YMSession *session, YMPeer *peer);
typedef void (^YMSessionNewConnectionHandler)(YMSession *session, YMConnection *connection);
typedef void (^YMSessionNewStreamHandler)(YMSession *session, YMConnection *connection, YMStream *stream);
typedef void (^YMSessionStreamClosingHandler)(YMSession *session, YMConnection *connection, YMStream *stream);
typedef void (^YMSessionInterruptedHandler)(YMSession *session);

- (id)initWithType:(NSString *)type name:(NSString *)name;

- (void)setInterruptionHandler:(YMSessionInterruptedHandler)handler;

- (BOOL)startAdvertisingWithName:(NSString *)name
                   acceptHandler:(YMSessionShouldAcceptConnectionHandler)acceptHandler
               connectionHandler:(YMSessionNewConnectionHandler)connectionHandler;

- (BOOL)browsePeersWithHandler:(YMSessionPeerDiscoveredHandler)discoveredHandler
          disappearanceHandler:(YMSessionPeerDisappearedHandler)disappearanceHandler;
- (BOOL)connectToPeer:(YMPeer *)peer
    connectionHandler:(YMSessionNewConnectionHandler)connectedHandler
       failureHandler:(YMSessionConnectionFailedHandler)failedHandler;

@end
