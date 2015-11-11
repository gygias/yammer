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

#define TLSTestTimeBased false
#define TLSTestRoundTrips 1
#define TLSTestRandomMessages false
#define TLSTestRandomMessageMaxLength 1024

//#define TLSTestIndefinite
#ifdef TLSTestIndefinite
#define TLSTestEndDate ([NSDate distantFuture])
#else
#define TLSTestEndDate ([NSDate dateWithTimeIntervalSinceNow:10])
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
    
    char *name = YMStringCreateWithFormat("%s-local",[[self className] UTF8String]);
    YMPipeRef localOutgoing = YMPipeCreate(name);
    free(name);
    name = YMStringCreateWithFormat("%s-remote",[[self className] UTF8String]);
    YMPipeRef remoteOutgoing = YMPipeCreate(name);
    free(name);
    
    // "output" and "input" get confusing with all these crossed files!
    int localOutput = YMPipeGetInputFile(localOutgoing);
    int remoteInput = YMPipeGetOutputFile(localOutgoing);
    int remoteOutput = YMPipeGetInputFile(remoteOutgoing);
    int localInput = YMPipeGetOutputFile(remoteOutgoing);
    
    NSLog(@"tls tests running with local:%d->%d, remote:%d<-%d",localOutput,remoteInput,localInput,remoteOutput);
    
    BOOL localIsServer = arc4random_uniform(2);
    YMTLSProviderRef localProvider = YMTLSProviderCreate(localOutput, remoteInput, localIsServer);
    XCTAssert(localProvider,@"local provider didn't initialize");
    YMTLSProviderRef remoteProvider = YMTLSProviderCreate(remoteOutput, localInput, !localIsServer);
    XCTAssert(remoteProvider,@"remote provider didn't initialize");
    
    stateLock = YMLockCreateWithOptionsAndName(YMLockDefault, [[self className] UTF8String]);
    bytesIn = 0;
    bytesOut = 0;
    testRunning = YES;
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMSecurityProviderInit((YMSecurityProviderRef)localProvider);
        XCTAssert(okay,@"local tls provider didn't init");
        if ( okay )
            [self runLocal:localProvider];
    });
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        bool okay = YMSecurityProviderInit((YMSecurityProviderRef)remoteProvider);
        XCTAssert(okay,@"remote tls provider didn't init");
        if ( okay )
            [self runRemote:localProvider];
    });
    
    //#define AND_MEASURE
#ifdef AND_MEASURE
    [self measureBlock:^{
#endif
#ifndef PlexerTest1TimeBased
        while ( testRunning )
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.5,false);
#else
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, [PlexerTest1EndDate timeIntervalSinceDate:[NSDate date]], false);
#endif
#ifdef AND_MEASURE
    }];
#endif
    timeBasedEnd = YES;
    NSLog(@"tls test finished (%llu in, %llu out)",bytesIn,bytesOut);
}

const char *testMessage = "security is important. put in the advanced technology. technology consultants international. please check. thanks.";
const char *testResponse = "i am a technology creative. i am a foodie. i am quirky. here is a picture of my half-eaten food.";

- (void)runRemote:(YMTLSProviderRef)tls
{
#ifdef TLSTestTimeBased
    while ( ! timeBasedEnd )
#else
    for ( unsigned idx = 0; idx < TLSTestRoundTrips; idx++ )
#endif
    {
        NSData *outgoingMessage;
        if ( TLSTestRandomMessages )
            outgoingMessage = YMRandomDataWithMaxLength(TLSTestRandomMessageMaxLength);
        else
            outgoingMessage = [NSData dataWithBytesNoCopy:(void *)testMessage length:strlen(testMessage) + 1];
        
        [self sendMessage:tls :outgoingMessage];
        
        NSData *incomingMessage = [self receiveMessage:tls];
        XCTAssert([incomingMessage length]&&[lastLocalMessageWritten length]&&[incomingMessage isEqualToData:lastLocalMessageWritten],
                  @"incoming and last written do not match (i%zu o%zu)",[incomingMessage length],[lastLocalMessageWritten length]);
        
        YMLockLock(stateLock);
        bytesOut += [outgoingMessage length];
        bytesIn += [incomingMessage length];
        YMLockUnlock(stateLock);
    }
    
    testRunning = NO;
}

- (void)runLocal:(YMTLSProviderRef)tls
{
#ifdef TLSTestTimeBased
    while ( ! timeBasedEnd )
#else
    for ( unsigned idx = 0; idx < TLSTestRoundTrips; idx++ )
#endif
    {
        NSData *outgoingMessage;
        if ( TLSTestRandomMessages )
            outgoingMessage = YMRandomDataWithMaxLength(TLSTestRandomMessageMaxLength);
        else
            outgoingMessage = [NSData dataWithBytesNoCopy:(void *)testMessage length:strlen(testMessage) + 1];

        [self sendMessage:tls :outgoingMessage];
        
        NSData *incomingMessage = [self receiveMessage:tls];
        XCTAssert([incomingMessage length]&&[lastRemoteMessageWritten length]&&[incomingMessage isEqualToData:lastRemoteMessageWritten],
                  @"incoming and last written do not match (i%zu o%zu)",[incomingMessage length],[lastRemoteMessageWritten length]);
        
        YMLockLock(stateLock);
        bytesOut += [outgoingMessage length];
        bytesIn += [incomingMessage length];
        YMLockUnlock(stateLock);
    }
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

- (void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end
