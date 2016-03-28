//
//  YMSession.h
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <Yammer/Yammer.h>

@class YMPeer, YMConnection, YMStream;

@interface YMSession : NSObject

typedef void (^YMSessionPeerDiscoveredHandler)(YMSession *session, YMPeer *peer);
typedef void (^YMSessionPeerDisappearedHandler)(YMSession *session, YMPeer *peer);
typedef void (^YMSessionPeerResolveHandler)(YMSession *session, YMPeer *peer, BOOL resolved);

typedef bool (^YMSessionShouldAcceptConnectionHandler)(YMSession *session, YMPeer *peer);
typedef void (^YMSessionConnectionInitializingHandler)(YMSession *session);
typedef void (^YMSessionConnectionFailedHandler)(YMSession *session, YMPeer *peer);
typedef void (^YMSessionNewConnectionHandler)(YMSession *session, YMConnection *connection);

typedef void (^YMSessionNewStreamHandler)(YMSession *session, YMConnection *connection, YMStream *stream);
typedef void (^YMSessionStreamClosingHandler)(YMSession *session, YMConnection *connection, YMStream *stream);
typedef void (^YMSessionInterruptedHandler)(YMSession *session);

- (id)initWithType:(NSString *)type
              name:(NSString *)name;
- (void)setStandardHandlers:(YMSessionConnectionInitializingHandler)initializingHandler
                           :(YMSessionNewStreamHandler)newHandler
                           :(YMSessionStreamClosingHandler)closingHandler
                           :(YMSessionInterruptedHandler)interruptedHandler;

- (BOOL)startAdvertisingWithName:(NSString *)name
                   acceptHandler:(YMSessionShouldAcceptConnectionHandler)acceptHandler
               connectionHandler:(YMSessionNewConnectionHandler)connectionHandler;

- (BOOL)browsePeersWithHandler:(YMSessionPeerDiscoveredHandler)discoveredHandler
          disappearanceHandler:(YMSessionPeerDisappearedHandler)disappearanceHandler;
- (BOOL)resolvePeer:(YMPeer *)peer withHandler:(YMSessionPeerResolveHandler)handler;
- (BOOL)connectToPeer:(YMPeer *)peer
    connectionHandler:(YMSessionNewConnectionHandler)connectedHandler
       failureHandler:(YMSessionConnectionFailedHandler)failedHandler;

- (void)stop;

@end
