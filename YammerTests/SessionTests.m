//
//  SessionTests.m
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#import "YammerTests.h"

#import "YMSession.h"

@interface SessionTests : XCTestCase
{
    YMSessionRef serverSession;
    YMSessionRef clientSession;
    const char *testType;
    const char *testName;
    
    YMConnectionRef serverConnection;
    YMConnectionRef clientConnection;
    
    BOOL expectingRemoval;
}
@end

SessionTests *gTheSessionTest = nil;

@implementation SessionTests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testSession {
    
    gTheSessionTest = self;
    
    testType = "_ymtest._local";
    testName = "twitter-cliche";
    serverSession = YMSessionCreateServer(testType, testName);
    XCTAssert(serverSession,@"server alloc");
    YMSessionSetSharedCallbacks(serverSession, _ym_session_connected_func, _ym_session_interrupted_func, _ym_session_new_stream_func, _ym_session_stream_closing_func);
    YMSessionSetServerCallbacks(serverSession, _ym_session_should_accept_func, (__bridge void *)self);
    
    BOOL started = YMSessionServerStartAdvertising(serverSession);
    XCTAssert(started,@"server start");
    
    clientSession = YMSessionCreateClient(testType);
    XCTAssert(clientSession,@"client alloc");
    YMSessionSetSharedCallbacks(clientSession, _ym_session_connected_func, _ym_session_interrupted_func, _ym_session_new_stream_func, _ym_session_stream_closing_func);
    YMSessionSetClientCallbacks(clientSession, _ym_session_added_peer_func, _ym_session_removed_peer_func, _ym_session_resolve_failed_func, _ym_session_resolved_peer_func, _ym_session_connect_failed_func, (__bridge void *)self);
}

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

// client, discover->connect
void _ym_session_added_peer_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    NSLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest added:session :peer :context];
}

- (void)added:(YMSessionRef)session :(YMPeerRef)peer :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"added context");
    XCTAssert(session==clientSession,@"added session");
    XCTAssert(0==strcmp(YMPeerGetName(peer),testName),@"added name");
    
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_global_queue(0, 0), ^{
        // resolve
    });
}

void _ym_session_removed_peer_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    NSLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest removed:session :peer :context];
}

- (void)removed:(YMSessionRef)session :(YMPeerRef)peer :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"removed context");
    XCTAssert(session==clientSession,@"removed session");
    XCTAssert(0==strcmp(YMPeerGetName(peer),testName),@"removed name");
    XCTAssert(expectingRemoval,@"removed");
}

void _ym_session_resolve_failed_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    NSLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest resolveFailed:session :peer :context];
}

- (void)resolveFailed:(YMSessionRef)session :(YMPeerRef)peer :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"resolveFailed context");
    XCTAssert(session==clientSession,@"resolveFailed session");
    XCTAssert(0==strcmp(YMPeerGetName(peer),testName),@"resolveFailed name");
    XCTAssert(NO,@"resolveFailed");
}

void _ym_session_resolved_peer_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    NSLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest resolved:session :peer :context];
}

- (void)resolved:(YMSessionRef)session :(YMPeerRef)peer :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"resolved context");
    XCTAssert(session==clientSession,@"resolved session");
    XCTAssert(0==strcmp(YMPeerGetName(peer),testName),@"resolved name");
    
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_global_queue(0, 0), ^{
        // connect
    });
}

void _ym_session_connect_failed_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    NSLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest connectFailed:session :peer :context];
}

- (void)connectFailed:(YMSessionRef)session :(YMPeerRef)peer :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"connectFailed context");
    XCTAssert(session==clientSession,@"connectFailed session");
    XCTAssert(0==strcmp(YMPeerGetName(peer),testName),@"connectFailed name");
    XCTAssert(NO,@"connectFailed");
}

// server
bool _ym_session_should_accept_func(YMSessionRef session, YMPeerRef peer, void *context)
{
    NSLog(@"%s",__PRETTY_FUNCTION__);
    return [gTheSessionTest shouldAccept:session :peer :context];
}

- (bool)shouldAccept:(YMSessionRef)session :(YMPeerRef)peer :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"shouldAccept context");
    XCTAssert(session==clientSession,@"shouldAccept session");
    XCTAssert(peer,@"shouldAccept peer");
    return true;
}

// connection
void _ym_session_connected_func(YMSessionRef session, YMConnectionRef connection, void *context)
{
    NSLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest connected:session :connection :context];
}

- (void)connected:(YMSessionRef)session :(YMConnectionRef)connection :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"connected context");
    XCTAssert(session==clientSession||session==serverSession,@"connected session");
    XCTAssert(connection,@"connected peer");
    
    if ( session == clientSession )
        clientConnection = connection;
    else
        serverConnection = connection;
    
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        // do stuff
    });
}

void _ym_session_interrupted_func(YMSessionRef session, void *context)
{
    NSLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest interrupted:session :context];
}

- (void)interrupted:(YMSessionRef)session :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"interrupted context");
    XCTAssert(session==clientSession||session==serverSession,@"interrupted session");
    XCTAssert(NO,@"interrupted");
}

// streams
void _ym_session_new_stream_func(YMSessionRef session, YMStreamRef stream, void *context)
{
    NSLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest newStream:session :stream :context];
}

- (void)newStream:(YMSessionRef)session :(YMStreamRef)stream :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"newStream context");
    XCTAssert(session==clientSession||session==serverSession,@"newStream session");
    XCTAssert(stream,@"newStream stream");
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        // ...
    });
}

void _ym_session_stream_closing_func(YMSessionRef session, YMStreamRef stream, void *context)
{
    NSLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest streamClosing:session :stream :context];
}

- (void)streamClosing:(YMSessionRef)session :(YMStreamRef)stream :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"newStream context");
    XCTAssert(session==clientSession||session==serverSession,@"newStream session");
    XCTAssert(stream,@"newStream stream");
}

@end
