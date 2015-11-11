//
//  TLSTests.m
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#import "YammerTests.h"

#import "YMTLSProvider.h"
#import "YMPipe.h"
#import "YMUtilities.h"
#import "YMLock.h"

#include <sys/socket.h>
#include <sys/un.h>

#define TLSTestRoundTrips 1
#define TLSTestMessageRoudTrips 1000
#define TLSTestRandomMessages false
#define TLSTestRandomMessageMaxLength 1024

//#define TLSTestIndefinite
#define TLSTestTimeBased false
#ifdef TLSTestTimeBased
    #ifdef TLSTestIndefinite
    #define TLSTestEndDate ([NSDate distantFuture])
    #else
    #define TLSTestEndDate ([NSDate dateWithTimeIntervalSinceNow:10])
    #endif
#endif

@interface TLSTests : XCTestCase
{
    YMLockRef stateLock;
    uint64_t bytesIn,bytesOut;
    BOOL timeBasedEnd;
    BOOL testRunning;
    
    NSData *lastLocalMessageWritten;
    NSData *lastRemoteMessageWritten;
}
@end

typedef struct
{
    uint16_t length;
} UserMessageHeader;

@implementation TLSTests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testTLS1 {
    
//    char *name = YMStringCreateWithFormat("%s-local",[[self className] UTF8String]);
//    YMPipeRef localOutgoing = YMPipeCreate(name);
//    free(name);
//    name = YMStringCreateWithFormat("%s-remote",[[self className] UTF8String]);
//    YMPipeRef remoteOutgoing = YMPipeCreate(name);
//    free(name);
//    
//    // "output" and "input" get confusing with all these crossed files!
//    int localOutput = YMPipeGetInputFile(localOutgoing);
//    int remoteInput = YMPipeGetOutputFile(localOutgoing);
//    int remoteOutput = YMPipeGetInputFile(remoteOutgoing);
//    int localInput = YMPipeGetOutputFile(remoteOutgoing);
//    
//    NSLog(@"tls tests running with local:%d->%d, remote:%d<-%d",localOutput,remoteInput,localInput,remoteOutput);
//    
//    BOOL localIsServer = arc4random_uniform(2);
//    int sockets[2];
//    BOOL socketsOK = [self _createSockets:sockets];
//    YMTLSProviderRef localProvider = YMTLSProviderCreateWithFullDuplexFile(socket, localIsServer);
//    XCTAssert(localProvider,@"local provider didn't initialize");
//    YMTLSProviderRef remoteProvider = YMTLSProviderCreateWithFullDuplexFile(socket, !localIsServer);
//    XCTAssert(remoteProvider,@"remote provider didn't initialize");
    
    BOOL localIsServer = arc4random_uniform(2);
    
    char * sockName = YMStringCreateWithFormat("ym-tls-test-%u",arc4random());
    
    __block int serverSocket, clientSocket;
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        serverSocket = [self createServerSocket:sockName];
    });
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, false); // xxx let server reach accept()
    dispatch_sync(dispatch_get_global_queue(0, 0), ^{
        clientSocket = [self createClientSocket:sockName];
    });
    XCTAssert(serverSocket>=0,@"server socket is bogus");
    XCTAssert(clientSocket>=0,@"client socket is bogus");
    
    BOOL serverFirst = arc4random_uniform(2);
    const char *testBuffer = "moshi moshi";
    ssize_t testLen = (ssize_t)strlen(testBuffer) + 1;
    ssize_t testResult = write(serverFirst?serverSocket:clientSocket, testBuffer, testLen);
    XCTAssert(testResult==testLen,@"failed to write test message: %d (%s)",errno,strerror(errno));
    char testIncoming[testLen];
    testResult = read(serverFirst?clientSocket:serverSocket, testIncoming, testLen);
    XCTAssert(testResult==testLen,@"failed to receive test message: %d (%s)",errno,strerror(errno));
    XCTAssert(strcmp(testBuffer,testIncoming)==0,@"received test message does not match");
    
    YMTLSProviderRef localProvider = YMTLSProviderCreateWithFullDuplexFile(localIsServer ? serverSocket : clientSocket, localIsServer);
    XCTAssert(localProvider,@"local provider didn't initialize");
    YMTLSProviderRef remoteProvider = YMTLSProviderCreateWithFullDuplexFile(localIsServer ? clientSocket : serverSocket, !localIsServer);
    XCTAssert(remoteProvider,@"remote provider didn't initialize");
    
    stateLock = YMLockCreateWithOptionsAndName(YMLockDefault, [[self className] UTF8String]);
    bytesIn = 0;
    bytesOut = 0;
    testRunning = YES;
    
    YMTLSProviderRef theServer = localIsServer?localProvider:remoteProvider;
    YMTLSProviderRef theClient = localIsServer?remoteProvider:localProvider;
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMSecurityProviderInit((YMSecurityProviderRef)theServer);
        XCTAssert(okay,@"server tls provider didn't init");
        if ( okay )
            [self runLocal:theServer];
    });
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5, false); // xxx let server reach accept()
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMSecurityProviderInit((YMSecurityProviderRef)theClient);
        XCTAssert(okay,@"client tls provider didn't init");
        if ( okay )
            [self runRemote:theClient];
    });
    
    //#define AND_MEASURE
#ifdef AND_MEASURE
    [self measureBlock:^{
#endif
#if TLSTestTimeBased
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, [PlexerTest1EndDate timeIntervalSinceDate:[NSDate date]], false);
    #else
        while ( testRunning )
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.5,false);
#endif
#ifdef AND_MEASURE
    }];
#endif
    
    BOOL clientFirst = arc4random_uniform(2);
    dispatch_async(dispatch_get_global_queue(0,0), ^{
        bool okay = YMSecurityProviderClose((YMSecurityProviderRef)(clientFirst?theClient:theServer));
        XCTAssert(okay,@"client close failed");
    });
    dispatch_sync(dispatch_get_global_queue(0,0),  ^{
        bool okay = YMSecurityProviderClose((YMSecurityProviderRef)(clientFirst?theServer:theClient));
        XCTAssert(okay,@"server close failed");
    });
    
    timeBasedEnd = YES;
    NSLog(@"tls test finished (%llu in, %llu out)",bytesIn,bytesOut);
}

const char *testMessage = "security is important. put in the advanced technology. technology consultants international. please check. thanks.";
const char *testResponse = "i am a technology creative. i am a foodie. i am quirky. here is a picture of my half-eaten food.";

- (void)runRemote:(YMTLSProviderRef)tls
{
#if TLSTestTimeBased
    while ( ! timeBasedEnd )
#else
    for ( unsigned idx = 0; idx < TLSTestMessageRoudTrips; idx++ )
#endif
    {
        NSData *outgoingMessage;
        if ( TLSTestRandomMessages )
            outgoingMessage = YMRandomDataWithMaxLength(TLSTestRandomMessageMaxLength);
        else
            outgoingMessage = [NSData dataWithBytesNoCopy:(void *)testMessage length:strlen(testMessage) + 1 freeWhenDone:NO];
        
        lastRemoteMessageWritten = outgoingMessage;
        [self sendMessage:tls :outgoingMessage];
        
        NSData *incomingMessage = [self receiveMessage:tls];
        XCTAssert([incomingMessage length]&&[lastRemoteMessageWritten length]&&[incomingMessage isEqualToData:lastRemoteMessageWritten],
                  @"incoming and last written do not match (i%zu o%zu)",[incomingMessage length],[lastLocalMessageWritten length]);
        
        YMLockLock(stateLock);
        bytesOut += [outgoingMessage length];
        bytesIn += [incomingMessage length];
        YMLockUnlock(stateLock);
    }
    
    NSLog(@"runRemote exiting...");
}

- (void)runLocal:(YMTLSProviderRef)tls
{
#if TLSTestTimeBased
    while ( ! timeBasedEnd )
#else
    for ( unsigned idx = 0; idx < TLSTestMessageRoudTrips; idx++ )
#endif
    {
        NSData *outgoingMessage;
        if ( TLSTestRandomMessages )
            outgoingMessage = YMRandomDataWithMaxLength(TLSTestRandomMessageMaxLength);
        else
            outgoingMessage = [NSData dataWithBytesNoCopy:(void *)testMessage length:strlen(testMessage) + 1 freeWhenDone:NO];
        
        lastLocalMessageWritten = outgoingMessage;
        [self sendMessage:tls :outgoingMessage];
        
        NSData *incomingMessage = [self receiveMessage:tls];
        XCTAssert([incomingMessage length]&&[lastRemoteMessageWritten length]&&[incomingMessage isEqualToData:lastRemoteMessageWritten],
                  @"incoming and last written do not match (i%zu o%zu)",[incomingMessage length],[lastRemoteMessageWritten length]);
        
        YMLockLock(stateLock);
        bytesOut += [outgoingMessage length];
        bytesIn += [incomingMessage length];
        YMLockUnlock(stateLock);
    }
    
    NSLog(@"runLocal exiting...");
    testRunning = NO;
}

- (void)sendMessage:(YMTLSProviderRef)tls :(NSData *)message
{
    UserMessageHeader header = { [message length] };
    YMSecurityProviderWrite((YMSecurityProviderRef)tls, (void*)&header, sizeof(header));
    //XCTAssert(okay,@"failed to write message length");
    YMSecurityProviderWrite((YMSecurityProviderRef)tls, (void*)[message bytes], [message length]);
    //XCTAssert(okay,@"failed to write message");
}

- (NSData *)receiveMessage:(YMTLSProviderRef)tls
{
    UserMessageHeader header;
    bool okay = YMSecurityProviderRead((YMSecurityProviderRef)tls, (void*)&header, sizeof(header));
    XCTAssert(okay,@"failed to read header");
    uint8_t *buffer = malloc(header.length);
    YMSecurityProviderRead((YMSecurityProviderRef)tls, (void*)buffer, header.length);
    XCTAssert(okay,@"failed to read buffer");
    
    return [NSData dataWithBytesNoCopy:buffer length:header.length freeWhenDone:YES];
}

- (int)createServerSocket:(char *)sockName
{
    struct sockaddr_un name;
    int sock;
    socklen_t size;
    
    sock = socket(PF_LOCAL, SOCK_STREAM, 0/* IP, /etc/sockets man 5 protocols*/);
    
    /* Bind a name to the socket. */
    name.sun_family = AF_LOCAL;
    strncpy (name.sun_path, sockName, sizeof (name.sun_path));
    name.sun_path[sizeof (name.sun_path) - 1] = '\0';
    
    /* The size of the address is
     the offset of the start of the filename,
     plus its length (not including the terminating null byte).
     Alternatively you can just do:
     size = SUN_LEN (&name);
     */
    size = (offsetof (struct sockaddr_un, sun_path)
            + (socklen_t)strlen (name.sun_path));
    
    BOOL triedAgain = NO;
try_again:
    if (bind (sock, (struct sockaddr *) &name, size) < 0)
    {
        int bindErrno = errno;
        if ( errno == EADDRINUSE )
        {
            NSLog(@"EADDRINUSE: %s",sockName);
            triedAgain = YES;
            goto try_again;
        }
        close(sock);
        XCTAssert(NO,@"bind failed: %d (%s)",bindErrno,strerror(bindErrno));
    }
    
    if (listen(sock, 5))
    {
        int listenErrno = errno;
        close(sock);
        XCTAssert(NO,@"listen failed: %d (%s)",listenErrno,strerror(listenErrno));
    }
    
    int acceptedSock = accept(sock, (struct sockaddr *) &name, &size);
    if (acceptedSock<0)
    {
        int acceptErrno = errno;
        close(sock);
        XCTAssert(NO,@"accept failed: %d (%s)",acceptErrno,strerror(acceptErrno));
    }
    
    NSLog(@"accepted: %d",acceptedSock);
    
    return acceptedSock;
}

- (int)createClientSocket:(char *)sockName
{
    struct sockaddr_un name;
    int sock;
    socklen_t size;
    
    sock = socket(PF_LOCAL, SOCK_STREAM, 0/* IP, /etc/sockets man 5 protocols*/);
    
    /* Bind a name to the socket. */
    name.sun_family = AF_LOCAL;
    strncpy (name.sun_path, sockName, sizeof (name.sun_path));
    name.sun_path[sizeof (name.sun_path) - 1] = '\0';
    
    /* The size of the address is
     the offset of the start of the filename,
     plus its length (not including the terminating null byte).
     Alternatively you can just do:
     size = SUN_LEN (&name);
     */
    size = (offsetof (struct sockaddr_un, sun_path)
            + (socklen_t)strlen (name.sun_path));
    
    int result = connect(sock, (struct sockaddr *) &name, size);
    if ( result != 0 )
    {
        int connectErrno = errno;
        close(sock);
        XCTAssert(NO,@"connect failed: %d (%s)",connectErrno,strerror(connectErrno));
    }
    
    NSLog(@"connected: %d",sock);
    
    return sock;
}

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end
