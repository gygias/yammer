//
//  YMSession.m
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YMSession.h"

#import "YMConnectionPriv.h"
#import "YMPeerPriv.h"

#include <libyammer/YMSession.h>

@interface YMSession ()

@property (nonatomic,copy) NSString *type;

@property (nonatomic,copy) YMSessionPeerDiscoveredHandler discoveredHandler;
@property (nonatomic,copy) YMSessionPeerDisappearedHandler disappearedHandler;
@property (nonatomic,copy) YMSessionPeerResolveHandler resolvedHandler;
@property (nonatomic,copy) YMSessionConnectionInitializingHandler initializingHandler;
@property (nonatomic,copy) YMSessionNewConnectionHandler connectionHandler;
@property (nonatomic,copy) YMSessionConnectionFailedHandler failedHandler;
@property (nonatomic,copy) YMSessionShouldAcceptConnectionHandler shouldAcceptHandler;
@property (nonatomic,copy) YMSessionNewStreamHandler newHandler;
@property (nonatomic,copy) YMSessionStreamClosingHandler closingHandler;
@property (nonatomic,copy) YMSessionInterruptedHandler interruptedHandler;

@property (nonatomic) YMSessionRef ymsession;
@property (nonatomic,retain) NSMutableArray *connections;
@property (nonatomic,retain) YMPeer *resolvingPeer;
@property (nonatomic,retain) YMPeer *connectingPeer;

@end

@implementation YMSession

- (id)initWithType:(NSString *)type
{
    if ( ( self = [super init] ) ) {
        // todo: since we're the 'friendly objc' wrapper, should probably check args rather than crash in the c lib
        YMStringRef ymName = YMSTRC([type UTF8String]);
        self.ymsession = YMSessionCreate(ymName);
        YMRelease(ymName);
        if ( ! self.ymsession )
            return nil;
        
        YMSessionSetCommonCallbacks(self.ymsession, _ym_session_initializing_func, _connected_func, _interrupted_func, _new_stream_func, _closing_func);
        YMSessionSetAdvertisingCallbacks(self.ymsession, _should_accept_func, (__bridge void *)(self));
        YMSessionSetBrowsingCallbacks(self.ymsession, _added_peer_func, _removed_peer_func, _resolve_failed_func, _resolved_func, _connect_failed_func, (__bridge void *)(self));
        
        self.type = type;
        self.connections = [NSMutableArray array];
    }
    return self;
}

- (void)setStandardHandlers:(YMSessionConnectionInitializingHandler)initializing
                           :(YMSessionNewStreamHandler)new
                           :(YMSessionStreamClosingHandler)closing
                           :(YMSessionInterruptedHandler)interrupted
{
    self.initializingHandler = initializing;
    self.newHandler = new;
    self.closingHandler = closing;
    self.interruptedHandler = interrupted;
}

- (void)dealloc
{
    if ( self.ymsession )
        YMRelease(self.ymsession);
    
    //[super dealloc]; // arc
}

- (BOOL)startAdvertisingWithName:(NSString *)name
                   acceptHandler:(YMSessionShouldAcceptConnectionHandler)acceptHandler
               connectionHandler:(YMSessionNewConnectionHandler)connectionHandler
{
    self.shouldAcceptHandler = acceptHandler;
    self.connectionHandler = connectionHandler;
    YMStringRef sessionName = YMSTRC([name UTF8String]);
    BOOL okay = YMSessionStartAdvertising(self.ymsession, sessionName);
    YMRelease(sessionName);
    return okay;
}

- (BOOL)browsePeersWithHandler:(YMSessionPeerDiscoveredHandler)discoveredHandler
          disappearanceHandler:(YMSessionPeerDisappearedHandler)disappearanceHandler
{
    if ( ! self.ymsession )
        return NO;
    
    self.discoveredHandler = discoveredHandler;
    self.disappearedHandler = disappearanceHandler;
    return YMSessionStartBrowsing(self.ymsession);
}

- (BOOL)resolvePeer:(YMPeer *)peer withHandler:(YMSessionPeerResolveHandler)handler
{
    if ( ! self.ymsession )
        return NO;
    
    self.resolvingPeer = peer;
    self.resolvedHandler = handler;
    return YMSessionResolvePeer(self.ymsession, [peer _peerRef]);
}

- (BOOL)connectToPeer:(YMPeer *)peer
    connectionHandler:(YMSessionNewConnectionHandler)connectedHandler
       failureHandler:(YMSessionConnectionFailedHandler)failedHandler
{
    if ( ! self.ymsession )
        return NO;
    if ( ! peer )
        return NO;
    
    self.connectionHandler = connectedHandler;
    self.failedHandler = failedHandler;
    self.connectingPeer = peer;
    return YMSessionConnectToPeer(self.ymsession, [peer _peerRef], false);
}

- (void)stop
{
    NSLog(@"%s: %@",__FUNCTION__,self);
    YMSessionStop(self.ymsession);
}

- (YMConnection *)_connectionForRef:(YMConnectionRef)connectionRef
{
    __block YMConnection *theConnection = nil;
    [self.connections enumerateObjectsUsingBlock:^(id  _Nonnull obj, __unused NSUInteger idx, BOOL * _Nonnull stop) {
        YMConnection *aCon = (YMConnection *)obj;
        if ( [aCon _isEqualToRef:connectionRef] ) {
            theConnection = obj;
            *stop = YES;
        }
    }];
    
    return theConnection;
}

void _added_peer_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    if ( SELF.discoveredHandler ) {
        YMPeer *peer = [[YMPeer alloc] _initWithPeerRef:peerRef];
        SELF.discoveredHandler(SELF, peer);
    }
}

void _removed_peer_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    if ( SELF.disappearedHandler ) {
        YMPeer *peer = [[YMPeer alloc] _initWithPeerRef:peerRef];
        SELF.disappearedHandler(SELF, peer);
    }
}

void _resolve_failed_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    if ( SELF.resolvingPeer && SELF.resolvedHandler ) {
        BOOL same = YMStringEquals(YMPeerGetName(peerRef), YMPeerGetName([SELF.resolvingPeer _peerRef]));
        if ( same )
            SELF.resolvedHandler(SELF, SELF.resolvingPeer, NO);
    }
}

void _resolved_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    if ( SELF.resolvingPeer && SELF.resolvedHandler ) {
        BOOL same = YMStringEquals(YMPeerGetName(peerRef), YMPeerGetName([SELF.resolvingPeer _peerRef]));
        if ( same )
            SELF.resolvedHandler(SELF, SELF.resolvingPeer, YES);
    }
}

void _connect_failed_func(__unused YMSessionRef session, YMPeerRef peerRef, __unused bool moreComing, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    if ( SELF.connectingPeer && SELF.failedHandler ) {
        BOOL same = YMStringEquals(YMPeerGetName(peerRef), YMPeerGetName([SELF.connectingPeer _peerRef]));
        if ( same )
            SELF.failedHandler(SELF, SELF.connectingPeer);
    }
}

bool _should_accept_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    YMPeer *peer = [[YMPeer alloc] _initWithPeerRef:peerRef];
    return SELF.shouldAcceptHandler(SELF, peer);
}

void _ym_session_initializing_func(__unused YMSessionRef session, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    if ( SELF.initializingHandler )
        SELF.initializingHandler(SELF);
}

void _connected_func(__unused YMSessionRef session,YMConnectionRef connectionRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    YMConnection *connection = [[YMConnection alloc] _initWithConnectionRef:connectionRef];
    [SELF.connections addObject:connection];
    if ( SELF.connectionHandler )
        SELF.connectionHandler(SELF, connection);
}

void _interrupted_func(__unused YMSessionRef sessionRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    if ( SELF.interruptedHandler )
        SELF.interruptedHandler(SELF);
}

void _new_stream_func(__unused YMSessionRef sessionRef, __unused YMConnectionRef connectionRef, YMStreamRef streamRef, __unused void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    YMConnection *connectionForRef = [SELF _connectionForRef:connectionRef];
    YMStream *streamForRef = [connectionForRef _streamForRef:streamRef];
    if ( SELF.newHandler )
        SELF.newHandler(SELF, connectionForRef, streamForRef);
}

void _closing_func(__unused YMSessionRef sessionRef, __unused YMConnectionRef connectionRef, __unused YMStreamRef streamRef, __unused void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    YMConnection *connection = [SELF _connectionForRef:connectionRef];
    YMStream *stream = [connection _streamForRef:streamRef];
    if ( SELF.closingHandler )
        SELF.closingHandler(SELF,connection,stream);
}

@end
