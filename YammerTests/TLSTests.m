//
//  TLSTests.m
//  yammer
//
//  Created by david on 11/10/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YammerTests.h"

#import "YMTLSProvider.h"
#import "YMLocalSocketPair.h"
#import "YMUtilities.h"
#import "YMLock.h"
#import "YMSemaphore.h"

#define TLSTestRoundTrips 1
#define TLSTestMessageRoundTrips 100
#define TLSTestRandomMessages true
#define TLSTestRandomMessageMaxLength 1024

//#define TLSTestIndefinite
#define TLSTestTimeBased true
#if TLSTestTimeBased
    #ifdef TLSTestIndefinite
    #define TLSTestEndDate ([NSDate distantFuture])
    #else
    #define TLSTestEndDate ([NSDate dateWithTimeIntervalSinceNow:10])
    #endif
#endif

@interface TLSTests : XCTestCase
{
    YMLockRef stateLock;
    YMSemaphoreRef threadExitSemaphore;
    uint64_t bytesIn,bytesOut;
    BOOL isTimeBased;
    BOOL timeBasedEnd;
    
    NSData *lastMessageSent;
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
    
    BOOL localIsServer = arc4random_uniform(2);
    
    NSString *template = @"ym-tls-test-%u";
    uint32_t suffix = arc4random();
    NSString *nsSockName = [NSString stringWithFormat:template,suffix];
    YMStringRef sockName = YMStringCreateWithFormat("ym-tls-test-%u",arc4random(), NULL);
    XCTAssert(strcmp([nsSockName UTF8String],YMSTR(sockName))==0,@"ymstring: %s",YMSTR(sockName));
    
    YMLocalSocketPairRef localSocketPair = YMLocalSocketPairCreate(sockName);
    XCTAssert(localSocketPair,@"socket pair didn't initialize");
    int serverSocket = YMLocalSocketPairGetA(localSocketPair);
    int clientSocket = YMLocalSocketPairGetB(localSocketPair);
    
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
    threadExitSemaphore = YMSemaphoreCreate([[self className] UTF8String], 0);
    bytesIn = 0;
    bytesOut = 0;
    isTimeBased = TLSTestTimeBased;
    
    YMTLSProviderRef theServer = localIsServer?localProvider:remoteProvider;
    YMTLSProviderRef theClient = localIsServer?remoteProvider:localProvider;
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMSecurityProviderInit((YMSecurityProviderRef)theServer);
        XCTAssert(okay,@"server tls provider didn't init");
        if ( okay )
            [self runEndpoint:theServer :localIsServer];
    });
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 5, false); // xxx let server reach accept()
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMSecurityProviderInit((YMSecurityProviderRef)theClient);
        XCTAssert(okay,@"client tls provider didn't init");
        if ( okay )
            [self runEndpoint:theClient :!localIsServer];
    });
    
    //#define AND_MEASURE
#ifdef AND_MEASURE
    [self measureBlock:^{
#endif
#if TLSTestTimeBased
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, [TLSTestEndDate timeIntervalSinceDate:[NSDate date]], false);
#endif
#ifdef AND_MEASURE
    }];
#endif
    
    // signal threads to exit and wait for them
    YMLockLock(stateLock); // ensure the iterate the same number of times (for time-based where this thread flags the end)
    timeBasedEnd = YES;
    YMLockUnlock(stateLock);
    YMSemaphoreWait(threadExitSemaphore); // todo: there's still a deadlock bug in this code, where server races client and timeBasedEnd to try to receive an additional message
    YMSemaphoreWait(threadExitSemaphore);
    
    BOOL clientFirst = arc4random_uniform(2);
    dispatch_async(dispatch_get_global_queue(0,0), ^{
        bool okay = YMSecurityProviderClose((YMSecurityProviderRef)(clientFirst?theClient:theServer));
        XCTAssert(okay,@"client close failed");
    });
    dispatch_sync(dispatch_get_global_queue(0,0),  ^{
        bool okay = YMSecurityProviderClose((YMSecurityProviderRef)(clientFirst?theServer:theClient));
        XCTAssert(okay,@"server close failed");
    });
    
    NSLog(@"tls test finished (%llu in, %llu out)",bytesIn,bytesOut);
}

const char *testMessage = "security is important. put in the advanced technology. technology consultants international. please check. thanks.";
const char *testResponse = "i am a technology creative. i am a foodie. i am quirky. here is a picture of my half-eaten food.";

- (void)runEndpoint:(YMTLSProviderRef)tls :(BOOL)isServer
{
    BOOL looped = NO;
    for ( unsigned idx = 0; (isTimeBased && ! timeBasedEnd) || (idx < TLSTestMessageRoundTrips); idx++ )
    {
        if ( looped )
            YMLockUnlock(stateLock);
        
        NSData *outgoingMessage;
        if ( TLSTestRandomMessages )
            outgoingMessage = YMRandomDataWithMaxLength(TLSTestRandomMessageMaxLength);
        else
            outgoingMessage = [NSData dataWithBytesNoCopy:(void *)testMessage length:strlen(testMessage) + 1 freeWhenDone:NO];
        
        if ( isServer )
        {
            NSData *incomingMessage = [self receiveMessage:tls];
            XCTAssert([incomingMessage length]&&[lastMessageSent length]&&[incomingMessage isEqualToData:lastMessageSent],
                  @"incoming and last written do not match (i%zu o%zu)",[incomingMessage length],[lastMessageSent length]);
            
            lastMessageSent = outgoingMessage;
            [self sendMessage:tls :outgoingMessage];
        }
        else
        {
            lastMessageSent = outgoingMessage;
            [self sendMessage:tls :outgoingMessage];
            
            NSData *incomingMessage = [self receiveMessage:tls];
            XCTAssert([incomingMessage length]&&[lastMessageSent length]&&[incomingMessage isEqualToData:lastMessageSent],
                      @"incoming and last written do not match (i%zu o%zu)",[incomingMessage length],[lastMessageSent length]);
        }
        
        
        // for races between server, client and main thread signaling time-based end
        looped = YES;
        YMLockLock(stateLock);
        bytesOut += [outgoingMessage length];
        bytesIn += [lastMessageSent length];
    }
    
    if ( looped )
        YMLockUnlock(stateLock);
    
    NSLog(@"runLocal exiting...");
    YMSemaphoreSignal(threadExitSemaphore);
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

@end
