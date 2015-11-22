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

@interface YMSession ()

@property (nonatomic,copy) NSString *type;
@property (nonatomic,copy) NSString *name;

@property (nonatomic,copy) YMSessionPeerDiscoveredHandler discoveredHandler;
@property (nonatomic,copy) YMSessionPeerDisappearedHandler disappearedHandler;
@property (nonatomic,copy) YMSessionNewConnectionHandler connectionHandler;
@property (nonatomic,copy) YMSessionConnectionFailedHandler failedHandler;
@property (nonatomic,copy) YMSessionShouldAcceptConnectionHandler shouldAcceptHandler;
@property (nonatomic,copy) YMSessionNewStreamHandler streamHandler;
@property (nonatomic,copy) YMSessionStreamClosingHandler streamClosingHandler;
@property (nonatomic,copy) YMSessionInterruptedHandler interruptHandler;

@property (nonatomic) YMSessionRef ymsession;
@property (nonatomic,retain) NSMutableArray *connections;
@property (nonatomic,retain) YMPeer *connectingPeer;

@end

@implementation YMSession

- (id)initWithType:(NSString *)type name:(NSString *)name
{
    if ( ( self = [super init] ) )
    {
        // todo: since we're the 'friendly objc' wrapper, should probably check args rather than crash in the c lib
        YMStringRef ymName = YMSTRC([type UTF8String]);
        self.ymsession = YMSessionCreate(ymName);
        YMRelease(ymName);
        if ( ! self.ymsession )
            return nil;
        
        YMSessionSetCommonCallbacks(self.ymsession, _ym_session_connected_func, _ym_session_interrupted_func, _ym_session_new_stream_func, _ym_session_stream_closing_func);
        YMSessionSetAdvertisingCallbacks(self.ymsession, _ym_session_should_accept_func, (__bridge void *)(self));
        YMSessionSetBrowsingCallbacks(self.ymsession, _ym_session_added_peer_func, _ym_session_removed_peer_func, _ym_session_resolve_failed_func, _ym_session_resolved_peer_func, _ym_session_connect_failed_func, (__bridge void *)(self));
        
        self.type = type;
        self.name = name;
        self.connections = [NSMutableArray array];
    }
    return self;
}

- (void)dealloc
{
    YMRelease(self.ymsession);
}

- (void)setInterruptionHandler:(YMSessionInterruptedHandler)handler
{
    self.interruptHandler = handler;
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

- (YMConnection *)_connectionForRef:(YMConnectionRef)connectionRef
{
    __block YMConnection *theConnection = nil;
    [self.connections enumerateObjectsUsingBlock:^(id  _Nonnull obj, __unused NSUInteger idx, BOOL * _Nonnull stop) {
        YMConnection *aCon = (YMConnection *)obj;
        if ( [aCon _isEqualToRef:connectionRef] )
        {
            theConnection = obj;
            *stop = YES;
        }
    }];
    
    return theConnection;
}

void _ym_session_added_peer_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    if ( SELF.discoveredHandler )
    {
        YMPeer *peer = [[YMPeer alloc] _initWithPeerRef:peerRef];
        SELF.discoveredHandler(SELF, peer);
    }
}

void _ym_session_removed_peer_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    if ( SELF.disappearedHandler )
    {
        YMPeer *peer = [[YMPeer alloc] _initWithPeerRef:peerRef];
        SELF.disappearedHandler(SELF, peer);
    }
}

void _ym_session_resolve_failed_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    if ( SELF.connectingPeer && SELF.failedHandler )
    {
        BOOL same = YMStringEquals(YMPeerGetName(peerRef), YMPeerGetName([SELF.connectingPeer _peerRef]));
        if ( same )
            SELF.failedHandler(SELF, SELF.connectingPeer);
    }
}

void _ym_session_resolved_peer_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    if ( SELF.connectingPeer && SELF.failedHandler )
    {
        BOOL same = YMStringEquals(YMPeerGetName(peerRef), YMPeerGetName([SELF.connectingPeer _peerRef]));
        if ( same )
            SELF.failedHandler(SELF, SELF.connectingPeer);
    }
}

void _ym_session_connect_failed_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    if ( SELF.connectingPeer && SELF.failedHandler )
    {
        BOOL same = YMStringEquals(YMPeerGetName(peerRef), YMPeerGetName([SELF.connectingPeer _peerRef]));
        if ( same )
            SELF.failedHandler(SELF, SELF.connectingPeer);
    }
}

bool _ym_session_should_accept_func(__unused YMSessionRef session, YMPeerRef peerRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    YMPeer *peer = [[YMPeer alloc] _initWithPeerRef:peerRef];
    return SELF.shouldAcceptHandler(SELF, peer);
}

void _ym_session_connected_func(__unused YMSessionRef session,YMConnectionRef connectionRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    YMConnection *connection = [[YMConnection alloc] _initWithConnectionRef:connectionRef];
    [SELF.connections addObject:connection];
    if ( SELF.connectionHandler )
        SELF.connectionHandler(SELF, connection);
}

void _ym_session_interrupted_func(__unused YMSessionRef sessionRef, void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    if ( SELF.interruptHandler )
        SELF.interruptHandler(SELF);
}

void _ym_session_new_stream_func(__unused YMSessionRef sessionRef, __unused YMConnectionRef connectionRef, YMStreamRef streamRef, __unused void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    YMConnection *connectionForRef = [SELF _connectionForRef:connectionRef];
    YMStream *streamForRef = [connectionForRef _streamForRef:streamRef];
    if ( SELF.streamHandler )
        SELF.streamHandler(SELF, connectionForRef, streamForRef);
}

void _ym_session_stream_closing_func(__unused YMSessionRef sessionRef, __unused YMConnectionRef connectionRef, __unused YMStreamRef streamRef, __unused void* context)
{
    YMSession *SELF = (__bridge YMSession *)context;
    NSLog(@"%s: %@",__FUNCTION__,SELF);
    
    YMConnection *connection = [SELF _connectionForRef:connectionRef];
    YMStream *stream = [connection _streamForRef:streamRef];
    if ( SELF.streamClosingHandler )
        SELF.streamClosingHandler(SELF,connection,stream);
}

@end
