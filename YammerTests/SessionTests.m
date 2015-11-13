//
//  SessionTests.m
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#import "YammerTests.h"

#import "YMSession.h"
#import "YMThread.h"
#import "YMStreamPriv.h" // todo need an 'internal' header

@interface SessionTests : XCTestCase
{
    YMSessionRef serverSession;
    YMSessionRef clientSession;
    const char *testType;
    const char *testName;
    
    YMConnectionRef serverConnection;
    YMConnectionRef clientConnection;
    
    dispatch_semaphore_t mainThreadSemaphore;
    dispatch_semaphore_t asyncForwardSemaphore;
    BOOL expectingRemoval;
}
@end

SessionTests *gTheSessionTest = nil;

#define FAKE_DELAY_MAX 3

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
    
    testType = "_ymtest._tcp";
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
    
    started = YMSessionClientStart(clientSession);
    XCTAssert(started,@"client start");
    
    mainThreadSemaphore = dispatch_semaphore_create(0);
    asyncForwardSemaphore = dispatch_semaphore_create(0);
    
    // wait for 2 connects
    dispatch_semaphore_wait(mainThreadSemaphore, DISPATCH_TIME_FOREVER);
    dispatch_semaphore_wait(mainThreadSemaphore, DISPATCH_TIME_FOREVER);
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self _runServer:serverSession];
    });
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self _runClient:clientSession];
    });
    
    // wait for 2 thread exits
    dispatch_semaphore_wait(mainThreadSemaphore, DISPATCH_TIME_FOREVER);
    dispatch_semaphore_wait(mainThreadSemaphore, DISPATCH_TIME_FOREVER);
    
    YMConnectionRef sC = YMSessionGetDefaultConnection(serverSession);
    XCTAssert(sC,@"server connection");
    YMConnectionRef cC = YMSessionGetDefaultConnection(clientSession);
    XCTAssert(cC,@"client connection");
    YMConnectionClose(sC);
}

- (void)_runServer:(YMSessionRef)server
{
    YMConnectionRef connection = YMSessionGetDefaultConnection(server);
    XCTAssert(connection,@"server connection");
    
    NSFileManager *fm = [NSFileManager defaultManager];
    NSString *file = @"/dev/random"; // @"/var/vm/sleepimage"; not user readable
    BOOL fileIsLarge = YES;
    if ( ! [fm fileExistsAtPath:file] )
    {
        file = [[fm contentsOfDirectoryAtPath:@"/cores" error:NULL] firstObject];
        if ( file )
            file = [@"/cores" stringByAppendingPathComponent:file];
        if ( ! [fm fileExistsAtPath:file] )
        {
            file = @"/System/Library/CoreServices/boot.efi";
            fileIsLarge = NO;
        }
    }
    
    NSLog(@"server chose %@",file);
    NSFileHandle *handle = [NSFileHandle fileHandleForReadingAtPath:file];
    XCTAssert(handle,@"server file handle");
    YMStreamRef stream = YMConnectionCreateStream(connection, [[NSString stringWithFormat:@"test-server-write-%@",file] UTF8String]);
    XCTAssert(stream,@"server create stream");
    uint64_t aboutAGigabyte = 1234567890;
    
    bool testAsync = arc4random_uniform(2);
    ym_thread_dispatch_forward_file_context ctx = {NULL,NULL};
    if ( testAsync )
    {
        ctx.callback = _async_forward_callback;
        ctx.context = (__bridge void *)(self);
    }
    BOOL okay = YMThreadDispatchForwardFile([handle fileDescriptor], stream, true, aboutAGigabyte, !testAsync, ctx);
    XCTAssert(okay,@"server forward file");
    
    if ( testAsync )
        dispatch_semaphore_wait(asyncForwardSemaphore, DISPATCH_TIME_FOREVER);
    
    YMConnectionCloseStream(connection,stream);
    
    dispatch_semaphore_signal(mainThreadSemaphore);
}

void _async_forward_callback(void * ctx, uint64_t bytesWritten)
{
    SessionTests *SELF = (__bridge SessionTests *)ctx;
    [SELF _asyncForwardCallback];
    free((void *)ctx);
}

- (void)_asyncForwardCallback
{
    dispatch_semaphore_signal(asyncForwardSemaphore);
}

- (void)_runClient:(YMSessionRef)client
{
    YMConnectionRef connection = YMSessionGetDefaultConnection(client);
    NSString *basePath = @"/usr/share/man/man2";
    
    NSFileManager *fm = [NSFileManager defaultManager];
    for ( NSString *aFile in [fm contentsOfDirectoryAtPath:basePath error:NULL] )
    {
        NSString *fullPath = [basePath stringByAppendingPathComponent:aFile];
        NSDictionary *attributes = [fm attributesOfItemAtPath:fullPath error:NULL];
        if ( ! [NSFileTypeRegular isEqualToString:(NSString *)attributes[NSFileType]] )
        {
            NSLog(@"client skipping %@",fullPath);
            continue;
        }
        NSLog(@"client sending %@",fullPath);
        
        YMStreamRef stream = YMConnectionCreateStream(connection, [[NSString stringWithFormat:@"test-client-write-%@",fullPath] UTF8String]);
        XCTAssert(stream,@"client stream %@",fullPath);
        NSFileHandle *handle = [NSFileHandle fileHandleForReadingAtPath:fullPath];
        XCTAssert(handle,@"client file handle %@",fullPath);
        ym_thread_dispatch_forward_file_context ctx = {NULL,NULL}; // todo cumbersome, take a ref?
        YMThreadDispatchForwardFile([handle fileDescriptor], stream, false, 0, true, ctx);
        
        YMConnectionCloseStream(connection, stream);
    }
    
    dispatch_semaphore_signal(mainThreadSemaphore);
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
    
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(arc4random_uniform(FAKE_DELAY_MAX) * NSEC_PER_SEC)), dispatch_get_global_queue(0, 0), ^{
        NSLog(@"resolving %s",YMPeerGetName(peer));
        YMSessionClientResolvePeer(session, peer);
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
    XCTAssert(YMPeerGetAddresses(peer));
    
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(arc4random_uniform(FAKE_DELAY_MAX) * NSEC_PER_SEC)), dispatch_get_global_queue(0, 0), ^{
        BOOL testSync = arc4random_uniform(2);
        NSLog(@"connecting to %s (%ssync)...",YMPeerGetName(peer),testSync?"":"a");
        bool okay = YMSessionClientConnectToPeer(session,peer,testSync);
        XCTAssert(okay,@"client connect to peer");
        
        if ( testSync )
        {
            YMConnectionRef connection = YMSessionGetDefaultConnection(session);
            [self connected:session :connection :(__bridge void *)(self)];
        }
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
    XCTAssert(session==serverSession,@"shouldAccept session");
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
    
    dispatch_semaphore_signal(mainThreadSemaphore);
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
