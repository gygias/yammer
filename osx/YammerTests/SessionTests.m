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
    uint64_t lastClientFileSize;
    
    NSMutableArray *nonRegularFileNames;
    NSString *tempFile;
    NSString *tempDir;
    
    // keeping all of these separate to have as tight a test as possible
    dispatch_queue_t serialQueue;
    dispatch_semaphore_t connectSemaphore;
    dispatch_semaphore_t threadExitSemaphore;
    dispatch_semaphore_t serverAsyncForwardSemaphore;
    dispatch_semaphore_t clientAsyncForwardSemaphore;
    BOOL stopping;
}
@end

const uint64_t gSomeLength = 5678900;
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

- (void)testSessionWritingDevRandomAndReadingManPages {
    
    gTheSessionTest = self;
    
    testType = "_ymtest._tcp";
    testName = "twitter-cliche";
    serverSession = YMSessionCreateServer(YMSTRC(testType), YMSTRC(testName));
    XCTAssert(serverSession,@"server alloc");
    YMSessionSetSharedCallbacks(serverSession, _ym_session_connected_func, _ym_session_interrupted_func, _ym_session_new_stream_func, _ym_session_stream_closing_func);
    YMSessionSetServerCallbacks(serverSession, _ym_session_should_accept_func, (__bridge void *)self);
    
    BOOL started = YMSessionServerStart(serverSession);
    XCTAssert(started,@"server start");
    
    clientSession = YMSessionCreateClient(YMSTRC(testType));
    XCTAssert(clientSession,@"client alloc");
    YMSessionSetSharedCallbacks(clientSession, _ym_session_connected_func, _ym_session_interrupted_func, _ym_session_new_stream_func, _ym_session_stream_closing_func);
    YMSessionSetClientCallbacks(clientSession, _ym_session_added_peer_func, _ym_session_removed_peer_func, _ym_session_resolve_failed_func, _ym_session_resolved_peer_func, _ym_session_connect_failed_func, (__bridge void *)self);
    
    started = YMSessionClientStart(clientSession);
    XCTAssert(started,@"client start");
    
    nonRegularFileNames = [NSMutableArray new];
    serialQueue = dispatch_queue_create("ymsessiontests", DISPATCH_QUEUE_SERIAL);
    connectSemaphore = dispatch_semaphore_create(0);
    serverAsyncForwardSemaphore = dispatch_semaphore_create(0);
    clientAsyncForwardSemaphore = dispatch_semaphore_create(0);
    threadExitSemaphore = dispatch_semaphore_create(0);
    
    // wait for 2 connects
    dispatch_semaphore_wait(connectSemaphore, DISPATCH_TIME_FOREVER);
    dispatch_semaphore_wait(connectSemaphore, DISPATCH_TIME_FOREVER);
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self _runServer:serverSession];
    });
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self _runClient:clientSession];
    });
    
    // wait for 2 thread exits
    dispatch_semaphore_wait(threadExitSemaphore, DISPATCH_TIME_FOREVER);
    dispatch_semaphore_wait(threadExitSemaphore, DISPATCH_TIME_FOREVER);
    
    YMConnectionRef sC = YMSessionGetDefaultConnection(serverSession);
    XCTAssert(sC,@"server connection");
    YMConnectionRef cC = YMSessionGetDefaultConnection(clientSession);
    XCTAssert(cC,@"client connection");
    
    stopping = YES;
    bool okay = true;
    bool stopServerFirst = arc4random_uniform(2);
    okay = stopServerFirst ? YMSessionServerStop(serverSession) : YMSessionClientStop(clientSession);
    XCTAssert(okay,@"first (%@) session close",stopServerFirst?@"server":@"client");
    okay = stopServerFirst? YMSessionClientStop(clientSession) : YMSessionServerStop(serverSession);
    // i don't think we can expect this to always succeed in-process.
    // we're racing the i/o threads as soon as we stop the server
    // but we can randomize which we close first to find real bugs.
    //XCTAssert(okay,@"client session close");
    
    NSLog(@"diffing %@",tempDir);
    NSPipe *outputPipe = [NSPipe pipe];
    NSTask *diff = [NSTask new];
    [diff setLaunchPath:@"/usr/bin/diff"];
    [diff setArguments:@[@"-r",@"/usr/share/man/man2",tempDir]];
    [diff setStandardOutput:outputPipe];
    __block BOOL checked = NO;
    __block BOOL checkOK = YES;
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        NSData *output = [[outputPipe fileHandleForReading] readDataToEndOfFile];
        NSString *outputStr = [[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding];
        NSArray *lines = [outputStr componentsSeparatedByString:@"\n"];
        [lines enumerateObjectsUsingBlock:^(id  _Nonnull line, __unused NSUInteger idx, BOOL * _Nonnull stop) {
            if ( [(NSString *)line length] == 0 )
                return;
            __block BOOL lineOK = NO;
            [nonRegularFileNames enumerateObjectsUsingBlock:^(id  _Nonnull name, __unused NSUInteger idx2, BOOL * _Nonnull stop2) {
                if ( [(NSString *)line containsString:(NSString *)name] )
                {
                    NSLog(@"making exception for %@ based on '%@'",name,line);
                    lineOK = YES;
                    *stop2 = YES;
                }
            }];
            if ( ! lineOK )
            {
                NSLog(@"no match for '%@'",line);
                checkOK = NO;
                checked = YES;
                *stop = YES;
            }
        }];
        dispatch_semaphore_signal(threadExitSemaphore);
    });
    [diff launch];
    [diff waitUntilExit];
    dispatch_semaphore_wait(threadExitSemaphore, DISPATCH_TIME_FOREVER);
    XCTAssert([diff terminationStatus]==0||checkOK,@"diff");
    NSLog(@"cleaning up");
    [[NSTask launchedTaskWithLaunchPath:@"/bin/rm" arguments:@[@"-rf",@"/tmp/ymsessiontest*"]] waitUntilExit];
    
    NSLog(@"session test finished");
    
#define CHECK_THREADS
#ifdef CHECK_THREADS
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, false);
    [[NSTask launchedTaskWithLaunchPath:@"/usr/bin/sample" arguments:@[@"-file",@"/dev/stdout",@"xctest",@"1",@"1000"]] waitUntilExit];
#endif
}

typedef struct asyncCallbackInfo
{
    void *theTest;
    void *semaphore;
} _asyncCallbackInfo;

typedef struct ManPageHeader
{
    uint64_t len;
    char     name[NAME_MAX+1];
}_ManPageHeader;

#define THXFORMANTEMPLATE "thx 4 man, man!%s"
typedef struct ManPageThanks
{
    char thx4Man[NAME_MAX+1+15];
}_ManPageThanks;

- (void)_runServer:(YMSessionRef)server
{
    YMConnectionRef connection = YMSessionGetDefaultConnection(server);
    // todo sometimes this is inexplicably null, yet not in the session by the time the test runs
    XCTAssert(connection,@"server connection");
    
    NSString *file = @"/dev/random"; // @"/var/vm/sleepimage"; not user readable
    
    NSLog(@"server chose %@",file);
    NSFileHandle *handle = [NSFileHandle fileHandleForReadingAtPath:file];
    XCTAssert(handle,@"server file handle");
    YMStreamRef stream = YMConnectionCreateStream(connection, YMStringCreateWithFormat("test-server-write-%s",[file UTF8String], NULL));
    XCTAssert(stream,@"server create stream");
    
    bool testAsync = arc4random_uniform(2);
    ym_thread_dispatch_forward_file_context ctx = {NULL,NULL};
    if ( testAsync )
    {
        ctx.callback = _server_async_forward_callback;
        struct asyncCallbackInfo *info = malloc(sizeof(struct asyncCallbackInfo));
        info->theTest = (__bridge void *)(self);
        info->semaphore = (__bridge void *)(serverAsyncForwardSemaphore);
        ctx.context = info;
    }
    BOOL okay = YMThreadDispatchForwardFile([handle fileDescriptor], stream, true, gSomeLength, !testAsync, ctx);
    XCTAssert(okay,@"server forward file");
    
    if ( testAsync )
        dispatch_semaphore_wait(serverAsyncForwardSemaphore, DISPATCH_TIME_FOREVER);
    
    YMConnectionCloseStream(connection,stream);
    
    NSLog(@"server thread exiting");
    dispatch_semaphore_signal(threadExitSemaphore);
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
            [nonRegularFileNames addObject:aFile];
            continue;
        }
        NoisyTestLog(@"client sending %@",fullPath);
        
        YMStreamRef stream = YMConnectionCreateStream(connection, YMStringCreateWithFormat("test-client-write-%s",[fullPath UTF8String], NULL));
        XCTAssert(stream,@"client stream %@",fullPath);
        NSFileHandle *handle = [NSFileHandle fileHandleForReadingAtPath:fullPath];
        XCTAssert(handle,@"client file handle %@",fullPath);
        
        lastClientFileSize = [(NSNumber *)attributes[NSFileSize] unsignedLongLongValue];
        struct ManPageHeader header = { lastClientFileSize , {0} };
        strncpy(header.name, [aFile UTF8String], NAME_MAX+1);
        YMStreamWriteDown(stream, &header, sizeof(header));
        if ( stopping )
            break;
        
        bool testAsync = arc4random_uniform(2);
        ym_thread_dispatch_forward_file_context ctx = {NULL,NULL};
        if ( testAsync )
        {
            ctx.callback = _client_async_forward_callback;
            struct asyncCallbackInfo *info = malloc(sizeof(struct asyncCallbackInfo));
            info->theTest = (__bridge void *)(self);
            info->semaphore = (__bridge void *)(clientAsyncForwardSemaphore);
            ctx.context = info;
        }
        
        YMThreadDispatchForwardFile([handle fileDescriptor], stream, false, 0, !testAsync, ctx);
        
        if (testAsync)
            dispatch_semaphore_wait(clientAsyncForwardSemaphore, DISPATCH_TIME_FOREVER);
        
#define THX_FOR_MAN // disable this to observe running out of open files
#ifdef THX_FOR_MAN
        struct ManPageThanks thx;
        uint16_t outLength = 0, length = sizeof(thx);
        YMIOResult result = YMStreamReadUp(stream, &thx, length,&outLength);
        if ( stopping )
            break;
        XCTAssert(result==YMIOSuccess,@"read thx header");
        XCTAssert(length==outLength,@"length!=outLength");
        NSString *thxFormat = [NSString stringWithFormat:@THXFORMANTEMPLATE,header.name];
        XCTAssert(0==strcmp(thx.thx4Man,[thxFormat cStringUsingEncoding:NSASCIIStringEncoding]),@"is this how one thx for man? %s",thx.thx4Man);
#endif
        
        YMConnectionCloseStream(connection, stream);
    }
    
    NSLog(@"client thread exiting");
    dispatch_semaphore_signal(threadExitSemaphore);
}

- (void)_eatManPage:(YMStreamRef)stream
{
    dispatch_sync(serialQueue, ^{
        if ( ! tempDir )
        {
            char tempd[256] = "/tmp/ymsessiontest-man-XXXXXXXXX";
            char *mkd = mkdtemp(tempd);
            XCTAssert(mkd==tempd,@"man page out dir %d %s",errno,strerror(errno));
            tempDir = [NSString stringWithUTF8String:mkd];
        }
    });
    
    struct ManPageHeader header;
    uint16_t outLength = 0, length = sizeof(header);
    YMIOResult ymResult = YMStreamReadUp(stream, &header, length, &outLength);
    if ( stopping )
        return;
    XCTAssert(ymResult==YMIOSuccess,@"read man header");
    XCTAssert(outLength==length,@"outLength!=length");
    XCTAssert(strlen(header.name)>0&&strlen(header.name)<=NAME_MAX, @"??? %s",header.name);
    uint64_t outBytes = 0;
    
    NSString *filePath = [tempDir stringByAppendingPathComponent:(NSString *_Nonnull)[NSString stringWithUTF8String:header.name]];
    BOOL okay = [[NSFileManager defaultManager] createFileAtPath:filePath contents:[NSData data] attributes:nil];
    XCTAssert(okay,@"touch man file %@",filePath);
    NSFileHandle *outHandle = [NSFileHandle fileHandleForWritingAtPath:filePath];
    XCTAssert(outHandle,@"get handle to %@",filePath);
    
    uint64_t len64 = header.len;
    ymResult = YMStreamWriteToFile(stream, [outHandle fileDescriptor], &len64, &outBytes);
    XCTAssert(ymResult==YMIOSuccess,@"eat man result");
    XCTAssert(outBytes>0,@"eat man outBytes");
    NoisyTestLog(@"_eatManPages: finished: %llu bytes: %@ : %s",outBytes,tempDir,header.name);
    [outHandle closeFile];
    
#define THX_FOR_MAN // disable this to observe running out of open files
#ifdef THX_FOR_MAN
    struct ManPageThanks thx;
    NSString *thxString = [NSString stringWithFormat:@THXFORMANTEMPLATE,header.name];
    strncpy(thx.thx4Man,[thxString cStringUsingEncoding:NSASCIIStringEncoding],sizeof(thx.thx4Man));
    YMStreamWriteDown(stream, &thx, sizeof(thx));
    if ( stopping )
        return;
#endif
}

- (void)_eatRandom:(YMStreamRef)stream
{
    char temp[256] = "/tmp/ymsessiontest-rand-XXXXXXXXX";
    int result = mkstemp(temp);
    tempFile = [NSString stringWithUTF8String:temp];
    XCTAssert(result>=0,@"eat random out handle %d %s",errno,strerror(errno));
    uint64_t outBytes = 0;
    YMIOResult ymResult = YMStreamWriteToFile(stream, result, NULL, &outBytes);
    if ( stopping )
        goto catch_return;
    XCTAssert(ymResult==YMIOSuccess,@"eat random result");
    XCTAssert(outBytes==gSomeLength,@"eat random outBytes");
    NoisyTestLog(@"_eatManPages: finished: %llu bytes",outBytes);
catch_return:
    result = close(result);
    XCTAssert(result==0,@"close rand temp failed %d %s",errno,strerror(errno));
}

void _server_async_forward_callback(void * ctx, uint64_t bytesWritten)
{
    NoisyTestLog(@"%s",__PRETTY_FUNCTION__);
    struct asyncCallbackInfo *info = (struct asyncCallbackInfo *)ctx;
    SessionTests *SELF = (__bridge SessionTests *)info->theTest;
    [SELF _asyncForwardCallback:YES :ctx :bytesWritten];
}

void _client_async_forward_callback(void * ctx, uint64_t bytesWritten)
{
    NoisyTestLog(@"%s",__PRETTY_FUNCTION__);
    struct asyncCallbackInfo *info = (struct asyncCallbackInfo *)ctx;
    SessionTests *SELF = (__bridge SessionTests *)info->theTest;
    [SELF _asyncForwardCallback:NO :ctx :bytesWritten];
}

- (void)_asyncForwardCallback:(BOOL)isServer :(void *)ctx :(uint64_t)written
{
    XCTAssert(ctx,@"client callback ctx null");
    struct asyncCallbackInfo *info = (struct asyncCallbackInfo *)ctx;
    
    if ( isServer )
        XCTAssert(written==gSomeLength,@"lengths don't match");
    else
        XCTAssert(written==lastClientFileSize,@"lengths don't match");
    
    dispatch_semaphore_t sem = (__bridge dispatch_semaphore_t)info->semaphore;
    XCTAssert(sem==clientAsyncForwardSemaphore||sem==serverAsyncForwardSemaphore,@"async callback unknown sem");
    NoisyTestLog(@"_async_forward_callback (%s): %llu",sem==clientAsyncForwardSemaphore?"client":"server",written);
    dispatch_semaphore_signal(sem);
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
    XCTAssert(0==strcmp(YMSTR(YMPeerGetName(peer)),testName),@"added name");
    
    if ( stopping )
        return;
    
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(arc4random_uniform(FAKE_DELAY_MAX) * NSEC_PER_SEC)), dispatch_get_global_queue(0, 0), ^{
        NSLog(@"resolving %s",YMSTR(YMPeerGetName(peer)));
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
    XCTAssert(0==strcmp(YMSTR(YMPeerGetName(peer)),testName),@"removed name");
    XCTAssert(stopping,@"removed");
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
    XCTAssert(0==strcmp(YMSTR(YMPeerGetName(peer)),testName),@"resolveFailed name");
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
    XCTAssert(0==strcmp(YMSTR(YMPeerGetName(peer)),testName),@"resolved name");
    XCTAssert(YMPeerGetAddresses(peer));
    
    if ( stopping )
        return;
    
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(arc4random_uniform(FAKE_DELAY_MAX) * NSEC_PER_SEC)), dispatch_get_global_queue(0, 0), ^{
        BOOL testSync = arc4random_uniform(2);
        NSLog(@"connecting to %s (%ssync)...",YMSTR(YMPeerGetName(peer)),testSync?"":"a");
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
    XCTAssert(0==strcmp(YMSTR(YMPeerGetName(peer)),testName),@"connectFailed name");
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
    
    dispatch_semaphore_signal(connectSemaphore);
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
    XCTAssert(stopping,@"interrupted");
}

// streams
void _ym_session_new_stream_func(YMSessionRef session, YMStreamRef stream, void *context)
{
    NoisyTestLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest newStream:session :stream :context];
}

- (void)newStream:(YMSessionRef)session :(YMStreamRef)stream :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"newStream context");
    XCTAssert(session==clientSession||session==serverSession,@"newStream session");
    XCTAssert(stream,@"newStream stream");
    
    BOOL isServer = session==serverSession;
    
    YMRetain(stream);
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        if ( isServer )
            [self _eatManPage:stream];
        else
            [self _eatRandom:stream];
        
        YMRelease(stream);
    });
}

void _ym_session_stream_closing_func(YMSessionRef session, YMStreamRef stream, void *context)
{
    NoisyTestLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest streamClosing:session :stream :context];
}

- (void)streamClosing:(YMSessionRef)session :(YMStreamRef)stream :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"newStream context");
    XCTAssert(session==clientSession||session==serverSession,@"newStream session");
    XCTAssert(stream,@"newStream stream");
}

@end