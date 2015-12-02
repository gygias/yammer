//
//  SessionTests.m
//  yammer
//
//  Created by david on 11/12/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YammerTests.h"

#import "YMSession.h"
#import "YMThread.h"
#import "YMStreamPriv.h" // todo need an 'internal' header
#import "YMPipe.h"
#import "YMPipePriv.h"

@interface SessionTests : XCTestCase
{
    YMSessionRef serverSession;
    YMSessionRef clientSession;
    const char *testType;
    const char *testName;
    
    YMConnectionRef serverConnection;
    YMConnectionRef clientConnection;
    BOOL serverAsync, serverBounding, lastClientAsync, lastClientBounded;
    YMPipeRef asyncServerMiddlemanPipe;
    NSFileHandle *randomHandle;
    uint64_t lastClientFileSize;
    uint64_t nManPagesToRead;
    uint64_t nManPagesRead;
    
    NSMutableArray *nonRegularFileNames;
    NSString *tempServerSrc;
    NSString *tempServerDst;
    NSString *tempManDir;
    
    // keeping all of these separate to have as tight a test as possible
    dispatch_queue_t serialQueue;
    dispatch_semaphore_t connectSemaphore;
    dispatch_semaphore_t threadExitSemaphore;
    BOOL stopping;
}
@end

uint64_t gSomeLength = 5678900;
SessionTests *gTheSessionTest = nil;

#define FAKE_DELAY_MAX 3
#define ServerTestFile "/private/var/log/install.log"

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
    nManPagesToRead = UINT64_MAX;
    nManPagesRead = 0;
    YMStringRef type = YMSTRC(testType);
    serverSession = YMSessionCreate(type);
    XCTAssert(serverSession,@"server alloc");
    YMSessionSetCommonCallbacks(serverSession, _ym_session_connected_func, _ym_session_interrupted_func, _ym_session_new_stream_func, _ym_session_stream_closing_func);
    YMSessionSetAdvertisingCallbacks(serverSession, _ym_session_should_accept_func, (__bridge void *)self);
    
    YMStringRef name = YMSTRC(testName);
    BOOL started = YMSessionStartAdvertising(serverSession, name);
    YMRelease(name);
    XCTAssert(started,@"server start");
    
    clientSession = YMSessionCreate(type);
    YMRelease(type);
    XCTAssert(clientSession,@"client alloc");
    YMSessionSetCommonCallbacks(clientSession, _ym_session_connected_func, _ym_session_interrupted_func, _ym_session_new_stream_func, _ym_session_stream_closing_func);
    YMSessionSetBrowsingCallbacks(clientSession, _ym_session_added_peer_func, _ym_session_removed_peer_func, _ym_session_resolve_failed_func, _ym_session_resolved_peer_func, _ym_session_connect_failed_func, (__bridge void *)self);
    
    started = YMSessionStartBrowsing(clientSession);
    XCTAssert(started,@"client start");
    
    nonRegularFileNames = [NSMutableArray new];
    serialQueue = dispatch_queue_create("ymsessiontests", DISPATCH_QUEUE_SERIAL);
    connectSemaphore = dispatch_semaphore_create(0);
    threadExitSemaphore = dispatch_semaphore_create(0);
    
    // wait for 2 connects
    dispatch_semaphore_wait(connectSemaphore, DISPATCH_TIME_FOREVER);
    dispatch_semaphore_wait(connectSemaphore, DISPATCH_TIME_FOREVER);
 
#define RUN_SERVER
#ifdef RUN_SERVER
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self _serverWriteRandom:serverSession];
    });
#endif
#define CLIENT_TOO // debugging forward-file hang-up
#ifdef CLIENT_TOO
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        [self _clientWriteManPages:clientSession];
    });
#endif
    
    // wait for 4 thread exits
#ifdef RUN_SERVER
    dispatch_semaphore_wait(threadExitSemaphore, DISPATCH_TIME_FOREVER); // write random
    dispatch_semaphore_wait(threadExitSemaphore, DISPATCH_TIME_FOREVER); // read random
#endif
#ifdef CLIENT_TOO
    dispatch_semaphore_wait(threadExitSemaphore, DISPATCH_TIME_FOREVER); // write man pages
    //dispatch_semaphore_wait(threadExitSemaphore, DISPATCH_TIME_FOREVER); // read man pages done differently
    while ( nManPagesRead < nManPagesToRead )
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
#endif
    
    YMConnectionRef sC = YMSessionGetDefaultConnection(serverSession);
    XCTAssert(sC,@"server connection");
    YMConnectionRef cC = YMSessionGetDefaultConnection(clientSession);
    XCTAssert(cC,@"client connection");
    
    stopping = YES;
    bool okay = true;
    bool stopServerFirst = arc4random_uniform(2);
    okay = stopServerFirst ? YMSessionStopAdvertising(serverSession) : YMSessionStopBrowsing(clientSession);
    okay = stopServerFirst ? YMSessionCloseAllConnections(serverSession) : YMSessionCloseAllConnections(clientSession);
    XCTAssert(okay,@"first (%@) session close",stopServerFirst?@"server":@"client");
    okay = stopServerFirst ? YMSessionStopBrowsing(clientSession) : YMSessionStopAdvertising(serverSession);
    okay = stopServerFirst ? YMSessionCloseAllConnections(clientSession) : YMSessionCloseAllConnections(serverSession);
    // i don't think we can expect this to always succeed in-process.
    // we're racing the i/o threads as soon as we stop the server
    // but we can randomize which we close first to find real bugs.
    //XCTAssert(okay,@"second (%@) session close",stopServerFirst?@"client":@"server");
    
    //YMRelease(serverConnection);
    YMRelease(serverSession);
    //YMRelease(clientConnection);
    YMRelease(clientSession);
    
#ifdef CLIENT_TOO
    NSLog(@"diffing %@",tempManDir);
    NSPipe *outputPipe = [NSPipe pipe];
    NSTask *diff = [NSTask new];
    [diff setLaunchPath:@"/usr/bin/diff"];
    [diff setArguments:@[@"-r",@"/usr/share/man/man2",tempManDir]];
    [diff setStandardOutput:outputPipe];
    __block BOOL manChecked = NO;
    __block BOOL manOK = YES;
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        NSData *output = [[outputPipe fileHandleForReading] readDataToEndOfFile];
        NSString *outputStr = [[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding];
        NSArray *lines = [outputStr componentsSeparatedByString:@"\n"];
        [lines enumerateObjectsUsingBlock:^(id  _Nonnull line, __unused NSUInteger idx, BOOL * _Nonnull stop) {
            if ( [(NSString *)line length] == 0 )
                return;
            __block BOOL lineOK = NO;
            [nonRegularFileNames enumerateObjectsUsingBlock:^(id  _Nonnull fileName, __unused NSUInteger idx2, BOOL * _Nonnull stop2) {
                if ( [(NSString *)line containsString:(NSString *)fileName] )
                {
                    NSLog(@"making exception for %@ based on '%@'",fileName,line);
                    lineOK = YES;
                    *stop2 = YES;
                }
            }];
            if ( ! lineOK )
            {
                NSLog(@"no match for '%@'",line);
                manOK = NO;
                manChecked = YES;
                *stop = YES;
            }
        }];
        dispatch_semaphore_signal(threadExitSemaphore);
    });
    [diff launch];
    [diff waitUntilExit];
    XCTAssert(manOK,@"man diff");
    
    diff = [NSTask new];
    [diff setLaunchPath:@"/usr/bin/diff"];
    [diff setArguments:@[tempServerSrc,tempServerDst]];
    [diff launch];
    [diff waitUntilExit];
    BOOL fileOK = [diff terminationStatus] == 0;
    XCTAssert(fileOK,@"file diff");
    
    dispatch_semaphore_wait(threadExitSemaphore, DISPATCH_TIME_FOREVER);
    NSLog(@"cleaning up");
    okay = [[NSFileManager defaultManager] removeItemAtPath:tempManDir error:NULL];
    XCTAssert(okay,@"tempDir");
#endif
    okay = [[NSFileManager defaultManager] removeItemAtPath:tempServerSrc error:NULL];
    XCTAssert(okay,@"tempFile");
    okay = [[NSFileManager defaultManager] removeItemAtPath:tempServerDst error:NULL];
    XCTAssert(okay,@"tempFile");
    
    YMFreeGlobalResources();
    NSLog(@"session test finished");
}

typedef struct ManPageHeader
{
    uint64_t len;
    char     name[NAME_MAX+1];
    BOOL     willBoundDataStream;
}_ManPageHeader;

#define THXFORMANTEMPLATE "thx 4 man, man!%s"
typedef struct ManPageThanks
{
    char thx4Man[NAME_MAX+1+15];
}_ManPageThanks;

- (void)_serverWriteRandom:(YMSessionRef)server
{
    YMConnectionRef connection = YMSessionGetDefaultConnection(server);
    // todo sometimes this is inexplicably null, yet not in the session by the time the test runs
    XCTAssert(connection,@"server connection");
    
    NSString *origFile = @ServerTestFile;
    
    tempServerSrc = [NSString stringWithFormat:@"/tmp/ymsessiontest-%@-org",[origFile lastPathComponent]];
    [[NSFileManager defaultManager] removeItemAtPath:tempServerSrc error:NULL];
    XCTAssert([[NSFileManager defaultManager] copyItemAtPath:origFile toPath:tempServerSrc error:NULL],@"copy random orig");
    
    serverBounding = arc4random_uniform(2);
    if ( serverBounding )
    {
        NSFileHandle *update = [NSFileHandle fileHandleForUpdatingAtPath:tempServerSrc];
        [update truncateFileAtOffset:gSomeLength];
        [update closeFile];
    }
    
    randomHandle = [NSFileHandle fileHandleForReadingAtPath:tempServerSrc]; // member so it doesn't get deallocated and closed if we forward async and go out of scope
    XCTAssert(randomHandle,@"server file handle");
    YMStringRef name = YMSTRCF("test-server-write-%s",[[origFile lastPathComponent] UTF8String]);
    YMStreamRef stream = YMConnectionCreateStream(connection, name);
    YMRelease(name);
    XCTAssert(stream,@"server create stream");
    
    serverAsync = arc4random_uniform(2);
    ym_connection_forward_context_t *ctx = NULL;
    if ( serverAsync )
    {
        ctx = calloc(sizeof(struct ym_connection_forward_context_t),1);
        ctx->callback = _server_async_forward_callback;
        ctx->context = (__bridge void *)self;
    }
    
    int readFd = [randomHandle fileDescriptor];
    if ( ! serverBounding )
    {
        uint32_t writeRandomUnboundedFor = arc4random_uniform(10) + 10;
        YMStringRef str = YMSTRC("middleman");
        asyncServerMiddlemanPipe = YMPipeCreate(str);
        YMRelease(str);
        readFd = YMPipeGetOutputFile(asyncServerMiddlemanPipe);
        int writeFd = YMPipeGetInputFile(asyncServerMiddlemanPipe);
        dispatch_async(dispatch_get_global_queue(0, 0), ^{
            unsigned char buf[1024];
            NSDate *startDate = [NSDate date];
            while(1) {
                ssize_t aRead = read([randomHandle fileDescriptor], buf, 1024);
                //XCTAssert(aRead==1024,"middleman read");
                
                ssize_t aWrite = write(writeFd, buf, aRead);
                XCTAssert(aWrite==aRead,"middleman write");
                
                if ( [[NSDate date] timeIntervalSinceDate:startDate] > writeRandomUnboundedFor )
                {
                    NSLog(@"closing /dev/random handle (f%d)",[randomHandle fileDescriptor]);
                    _YMPipeCloseInputFile(asyncServerMiddlemanPipe);
                    return;
                }
            }
            
            if ( serverAsync )
                YMRelease(asyncServerMiddlemanPipe);
        });
    }
    
    NSLog(@"writing random %sbounded, %ssync from f%d",serverBounding?"":"un",serverAsync?"a":"",[randomHandle fileDescriptor]);
    BOOL okay = YMConnectionForwardFile(connection, readFd, stream, serverBounding ? &gSomeLength : NULL, !serverAsync, ctx);
    XCTAssert(okay,@"server forward file");
    
    if ( ! serverAsync )
    {
        if ( ! serverBounding ) YMRelease(asyncServerMiddlemanPipe);
        YMConnectionCloseStream(connection,stream);
        dispatch_semaphore_signal(threadExitSemaphore);
    }
    
    NSLog(@"writing random thread (%sSYNC) exiting",serverAsync?"A":"");
}

- (void)_clientWriteManPages:(YMSessionRef)client
{
    YMConnectionRef connection = YMSessionGetDefaultConnection(client);
    NSString *basePath = @"/usr/share/man/man2";
    
    tempManDir = @"/tmp/ymsessiontest-man";
    BOOL okay = [[NSFileManager defaultManager] removeItemAtPath:tempManDir error:NULL];
    okay = [[NSFileManager defaultManager] createDirectoryAtPath:tempManDir withIntermediateDirectories:NO attributes:NULL error:NULL];
    XCTAssert(okay,@"temp dir");
    
    uint64_t actuallyWritten = 0;
    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray *contents = [fm contentsOfDirectoryAtPath:basePath error:NULL];
    for ( NSUInteger i = 0 ; i < contents.count; i++ )
    {
        NSString *aFile = contents[i];
        NSString *fullPath = [basePath stringByAppendingPathComponent:aFile];
        NSDictionary *attributes = [fm attributesOfItemAtPath:fullPath error:NULL];
        if ( ! [NSFileTypeRegular isEqualToString:(NSString *)attributes[NSFileType]] )
        {
            NSLog(@"client skipping %@",fullPath);
            [nonRegularFileNames addObject:aFile];
            continue;
        }
        NoisyTestLog(@"client sending %@",fullPath);
        
        YMStringRef name = YMSTRCF("test-client-write-%s",[fullPath UTF8String]);
        YMStreamRef stream = YMConnectionCreateStream(connection, name);
        YMRelease(name);
        XCTAssert(stream,@"client stream %@",fullPath);
        NSFileHandle *handle = [NSFileHandle fileHandleForReadingAtPath:fullPath];
        XCTAssert(handle,@"client file handle %@",fullPath);
        
        lastClientFileSize = [(NSNumber *)attributes[NSFileSize] unsignedLongLongValue];
        lastClientAsync = ( i == contents.count - 1 ) ? NO : arc4random_uniform(2); // don't let filehandle go out of scope on the last iter
        lastClientBounded = arc4random_uniform(2); // also tell remote whether we're bounding the file or pretending we don't know for testing
        struct ManPageHeader header = { lastClientFileSize , {0}, lastClientBounded };
        strncpy(header.name, [aFile UTF8String], NAME_MAX+1);
        YMStreamWriteDown(stream, &header, sizeof(header));
        if ( stopping )
        {
            YMConnectionCloseStream(connection, stream);
            break;
        }
        
        ym_connection_forward_context_t *ctx = NULL;
        if ( lastClientAsync )
        {
            ctx = calloc(sizeof(struct ym_connection_forward_context_t),1);
            ctx->callback = _client_async_forward_callback;
            ctx->context = (__bridge void *)self;
        }
        
        NoisyTestLog(@"writing man page '%@' %sbounded, %ssync from f%d",aFile,lastClientBounded?"":"un",lastClientAsync?"a":"",[handle fileDescriptor]);
        okay = YMConnectionForwardFile(connection, [handle fileDescriptor], stream, lastClientBounded ? &lastClientFileSize : NULL, !lastClientAsync, ctx);
        XCTAssert(okay,@"forwardfile failed");
        
        struct ManPageThanks thx;
        uint16_t outLength = 0, length = sizeof(thx);
        YMIOResult result = YMStreamReadUp(stream, &thx, length, &outLength);
        if ( stopping )
        {
            YMConnectionCloseStream(connection, stream);
            break;
        }
        XCTAssert(result==YMIOSuccess,@"read thx header");
        XCTAssert(length==outLength,@"length!=outLength");
        NSString *thxFormat = [NSString stringWithFormat:@THXFORMANTEMPLATE,header.name];
        XCTAssert(0==strcmp(thx.thx4Man,[thxFormat cStringUsingEncoding:NSASCIIStringEncoding]),@"is this how one thx for man? %s",thx.thx4Man);
        
        YMConnectionCloseStream(connection, stream);
        
        actuallyWritten++;
        NoisyTestLog(@"wrote the %lluth man page",actuallyWritten);
    }
    
    nManPagesToRead = actuallyWritten;
    dispatch_semaphore_signal(threadExitSemaphore);
    NSLog(@"write man pages thread exiting");
}

- (void)_eatManPage:(YMConnectionRef)connection :(YMStreamRef)stream
{
    struct ManPageHeader header;
    uint16_t outLength = 0, length = sizeof(header);
    YMIOResult ymResult = YMStreamReadUp(stream, &header, length, &outLength);
    if ( stopping )
    {
        YMConnectionCloseStream(connection, stream);
        return;
    }
    XCTAssert(ymResult==YMIOSuccess&&outLength==length,@"read man header");
    XCTAssert(strlen(header.name)>0&&strlen(header.name)<=NAME_MAX, @"??? %s",header.name);
    uint64_t outBytes = 0;
    
    NSString *filePath = [tempManDir stringByAppendingPathComponent:(NSString *_Nonnull)[NSString stringWithUTF8String:header.name]];
    BOOL okay = [[NSFileManager defaultManager] createFileAtPath:filePath contents:[NSData data] attributes:nil];
    XCTAssert(okay,@"touch man file %@",filePath);
    NSFileHandle *outHandle = [NSFileHandle fileHandleForWritingAtPath:filePath];
    XCTAssert(outHandle,@"get handle to %@",filePath);
    
    uint64_t len64 = header.len;
    NoisyTestLog(@"reading man page '%s'[%llu] %sbounded, sync to f%d",header.name,header.len,header.willBoundDataStream?"":"un",[outHandle fileDescriptor]);
    ymResult = YMStreamWriteToFile(stream, [outHandle fileDescriptor], header.willBoundDataStream ? &len64 : NULL, &outBytes);
    XCTAssert(ymResult==YMIOSuccess||(!header.willBoundDataStream&&ymResult==YMIOEOF),@"eat man result");
    XCTAssert(outBytes==header.len,"eat man result");
    NoisyTestLog(@"_eatManPages: finished: %llu bytes: %@ : %s",outBytes,tempDir,header.name);
    [outHandle closeFile];
    
    struct ManPageThanks thx;
    NSString *thxString = [NSString stringWithFormat:@THXFORMANTEMPLATE,header.name];
    strncpy(thx.thx4Man,[thxString cStringUsingEncoding:NSASCIIStringEncoding],sizeof(thx.thx4Man));
    YMStreamWriteDown(stream, &thx, sizeof(thx));
    if ( stopping )
    {
        YMConnectionCloseStream(connection, stream);
        return;
    }
    
    // todo randomize whether we close here, during streamClosing, after streamClosing, dispatch_after?
    // it's also worth noting that if you [forcibly] interrupt the session and immediately
    // dealloc the session, async clients working on incoming streams might fault doing this
    YMConnectionCloseStream(connection,stream);
    
    nManPagesRead++;
}

- (void)_eatRandom:(YMConnectionRef)connection :(YMStreamRef)stream
{
    tempServerDst = [NSString stringWithFormat:@"/tmp/ymsessiontest-%@-dst",[@ServerTestFile lastPathComponent]];
    [[NSFileManager defaultManager] removeItemAtPath:tempServerDst error:NULL];
    XCTAssert([[NSFileManager defaultManager] createFileAtPath:tempServerDst contents:nil attributes:nil],@"create out file");
    NSFileHandle *writeHandle = [NSFileHandle fileHandleForWritingAtPath:tempServerDst];
    XCTAssert(tempServerDst,@"eat file handle %d %s",errno,strerror(errno));
    uint64_t outBytes = 0;
    NSLog(@"reading random %sbounded...",serverBounding?"":"un");
    YMIOResult ymResult = YMStreamWriteToFile(stream, [writeHandle fileDescriptor], serverBounding ? &gSomeLength : NULL, &outBytes);
    XCTAssert(ymResult!=YMIOError,@"eat random result");
    XCTAssert(!serverBounding||outBytes==gSomeLength,@"eat random outBytes");
    NSLog(@"reading random finished: %llu bytes",outBytes);
    [writeHandle closeFile];
    
    // todo randomize whether we close here, during streamClosing, after streamClosing, dispatch_after?
    // it's also worth noting that if you [forcibly] interrupt the session and immediately
    // dealloc the session, async clients working on incoming streams might fault doing this
    YMConnectionCloseStream(connection,stream);
    
    NSLog(@"eat random exiting");
}

void _server_async_forward_callback(YMConnectionRef connection, YMStreamRef stream, YMIOResult result, uint64_t bytesWritten, void * ctx)
{
    NoisyTestLog(@"%s",__PRETTY_FUNCTION__);
    SessionTests *SELF = (__bridge SessionTests *)ctx;
    [SELF _asyncForwardCallback:connection :stream :result :bytesWritten :YES];
}

void _client_async_forward_callback(YMConnectionRef connection, YMStreamRef stream, YMIOResult result, uint64_t bytesWritten, void * ctx)
{
    NoisyTestLog(@"%s",__PRETTY_FUNCTION__);
    SessionTests *SELF = (__bridge SessionTests *)ctx;
    [SELF _asyncForwardCallback:connection :stream :result :bytesWritten :NO];
}

- (void)_asyncForwardCallback:(YMConnectionRef)connection :(YMStreamRef)stream :(YMIOResult)result :(uint64_t)bytesWritten :(BOOL)isServer
{
    XCTAssert(connection,@"connection nil");
    XCTAssert(stream,@"stream nil");
    XCTAssert(result==YMIOSuccess||(isServer&&!serverBounding&&result==YMIOEOF)||
                                    (!isServer&&!lastClientBounded&&result==YMIOEOF),@"!result");
    XCTAssert((isServer&&serverAsync)||(!isServer&&lastClientAsync),@"callback for sync forward (%d)",isServer);
    
    if ( isServer && serverBounding )
        XCTAssert(bytesWritten==gSomeLength,@"lengths don't match");
    else if ( ! isServer )
        XCTAssert(bytesWritten==lastClientFileSize,@"lengths don't match");
    
    if ( isServer )
        NSLog(@"_async_forward_callback (random): %llu",bytesWritten);
    else
        NoisyTestLog(@"_async_forward_callback (man): %llu",bytesWritten);
    if ( isServer )
    {
        YMConnectionCloseStream(connection, stream); // client is effectively synchronized by the 'thx for man' writeback
        dispatch_semaphore_signal(threadExitSemaphore);
    }
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
        YMSessionResolvePeer(session, peer);
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
    
    uint32_t fakeDelay = arc4random_uniform(FAKE_DELAY_MAX);
    int64_t fakeDelayNsec = (int64_t)fakeDelay * NSEC_PER_SEC;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, fakeDelay), dispatch_get_global_queue(0, 0), ^{
        BOOL testSync = arc4random_uniform(2);
        NSLog(@"connecting to %s after %lld delay (%ssync)...",YMSTR(YMPeerGetName(peer)),fakeDelayNsec,testSync?"":"a");
        bool okay = YMSessionConnectToPeer(session,peer,testSync);
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
void _ym_session_new_stream_func(YMSessionRef session, YMConnectionRef connection, YMStreamRef stream, void *context)
{
    NoisyTestLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest newStream:session :connection :stream :context];
}

- (void)newStream:(YMSessionRef)session :(YMConnectionRef)connection :(YMStreamRef)stream :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"newStream context");
    XCTAssert(session==clientSession||session==serverSession,@"newStream session");
    XCTAssert(stream,@"newStream stream");
    
    BOOL isServer = session==serverSession;
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        if ( isServer )
            [self _eatManPage:connection :stream];
        else
        {
            [self _eatRandom:connection :stream];
            dispatch_semaphore_signal(threadExitSemaphore);
        }
    });
}

void _ym_session_stream_closing_func(YMSessionRef session, YMConnectionRef connection, YMStreamRef stream, void *context)
{
    NoisyTestLog(@"%s",__PRETTY_FUNCTION__);
    [gTheSessionTest streamClosing:session :connection :stream :context];
}

- (void)streamClosing:(YMSessionRef)session :(YMConnectionRef)connection :(YMStreamRef)stream :(void *)context
{
    XCTAssert(context==(__bridge void *)self,@"streamClosing context");
    XCTAssert(session==clientSession||session==serverSession,@"streamClosing session");
    XCTAssert(connection,@"streamClosing connection");
    XCTAssert(stream,@"streamClosing stream");
}

@end
