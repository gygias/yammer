//
//  mDNSTests.m
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#import <XCTest/XCTest.h>

#import "YMmDNS.h"
#import "YMmDNSService.h"
#import "YMmDNSBrowser.h"

@interface mDNSTests : XCTestCase
{
    NSString *testServiceName;
    YMmDNSTxtRecordKeyPair **testKeyPairs;
    size_t nTestKeyPairs;

    YMmDNSBrowserRef browser;
    YMmDNSServiceRef service;

    BOOL waitingOnAppearance;
    BOOL waitingOnResolution;
    BOOL waitingOnDisappearance;
}
@end

mDNSTests *gGlobalSelf;

@implementation mDNSTests

#pragma mark mDNS tests

#define testServiceType "_yammer._tcp"
#define testKeyLengthBound 128

#if 0 // actually debugging
#define testTimeout (10 * 60)
#else
#define testTimeout 10
#endif

- (NSString *)_randomASCIIStringWithMaxLength:(uint8_t)maxLength :(BOOL)forServiceName
{
    NSMutableString *string = [NSMutableString string];
    
    uint8_t randomLength = arc4random_uniform(maxLength);
    if ( randomLength == 0 ) randomLength = 1;
    uint8_t maxChar = forServiceName ? 'z' : 0x7E, minChar = forServiceName ? 'a' : 0x20;
    uint8_t range = maxChar - minChar;
    
    while ( randomLength-- )
    {
        char aChar;
        while ( ( aChar = arc4random_uniform(range + 1) + minChar ) == '=' );
        [string appendFormat:@"%c",aChar];
    }
    
    return string;
}

- (NSData *)_randomValueWithMaxLength:(uint8_t)maxLength
{
    NSMutableData *data = [NSMutableData data];
    
    uint8_t randomLength = arc4random_uniform(maxLength);
    
    while ( randomLength-- )
    {
        uint8_t aByte = arc4random_uniform(0x100);
        [data appendBytes:&aByte length:sizeof(aByte)];
    }
    
    return data;
}

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
    
    waitingOnAppearance = YES;
    waitingOnResolution = YES;
    waitingOnDisappearance = YES;
    gGlobalSelf = self;
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
    
    if ( testKeyPairs )
        _YMmDNSTxtRecordKeyPairsFree(testKeyPairs, nTestKeyPairs);
}

void test_service_appeared(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    mDNSTests *SELF = (__bridge mDNSTests *)context;
    if (!context || SELF != gGlobalSelf) [NSException raise:@"test failed" format:@"context is nil or != gGlobalSelf in %s",__FUNCTION__]; // XCT depends on self, NSAssert on _cmd...
    [SELF appeared:browser :service];
}

- (void)appeared:(YMmDNSBrowserRef)aBrowser :(YMmDNSServiceRecord *)aService
{
    NSLog(@"%s/%s:? appeared",aService->type,aService->name);
    XCTAssert(aBrowser==browser,@"browser pointers are not equal on service appearance");
    if ( waitingOnAppearance && 0 == strcmp(aService->name, [testServiceName cStringUsingEncoding:NSASCIIStringEncoding]) )
    {
        waitingOnAppearance = NO;
        NSLog(@"resolving...");
        BOOL startedResolve = YMmDNSBrowserResolve(browser, aService->name);
        XCTAssert(startedResolve,@"YMmDNSBrowserResolve failed");
    }
}

void test_service_updated(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    mDNSTests *SELF = (__bridge mDNSTests *)context;
    if (!context || SELF != gGlobalSelf) [NSException raise:@"test failed" format:@"context is nil or != gGlobalSelf in %s",__FUNCTION__]; // XCT depends on self, NSAssert on _cmd...
    [SELF updated:browser :service];
}

- (void)updated:(YMmDNSBrowserRef)aBrowser :(YMmDNSServiceRecord *)aService
{
    NSLog(@"%s/%s:? updated",aService->type,aService->name);
    XCTAssert(browser==aBrowser,@"browser pointers %p and %p are not equal on service update",browser,aBrowser);
}

void test_service_resolved(YMmDNSBrowserRef browser, bool resolved, YMmDNSServiceRecord *service, void *context)
{
    mDNSTests *SELF = (__bridge mDNSTests *)context;
    if (!context || SELF != gGlobalSelf) [NSException raise:@"test failed" format:@"context is nil or != gGlobalSelf in %s",__FUNCTION__]; // XCT depends on self, NSAssert on _cmd...
    [SELF resolved:browser :service :resolved];
}

- (void)resolved:(YMmDNSBrowserRef)aBrowser :(YMmDNSServiceRecord *)aService :(bool)resolved
{
    XCTAssert(resolved, @"service did not resolve");
    XCTAssert(browser==aBrowser,@"browser pointers %p and %p are not equal on service resolution",browser,aBrowser);
    
    NSLog(@"%s/%s:%d resolved",aService->type,aService->name,aService->port);
    YMmDNSTxtRecordKeyPair **keyPairs = aService->txtRecordKeyPairs;
    size_t keyPairsSize = aService->txtRecordKeyPairsSize,
    idx = 0;
    
    XCTAssert(keyPairsSize == nTestKeyPairs, @"txt record sizes do not match (%u,%u)",(unsigned)keyPairsSize,(unsigned)nTestKeyPairs);
    
    for ( ; idx < keyPairsSize; idx++ )
    {
        XCTAssert(0 == strcmp(keyPairs[idx]->key, testKeyPairs[idx]->key), @"%zu-th keys '%s' and '%s' don't match",idx,keyPairs[idx]->key,testKeyPairs[idx]->key);
        XCTAssert(keyPairs[idx]->valueLen == testKeyPairs[idx]->valueLen, @"%zu-th values have different lengths of %u and %u",idx,keyPairs[idx]->valueLen,testKeyPairs[idx]->valueLen);
        XCTAssert(0 == memcmp(keyPairs[idx]->value, testKeyPairs[idx]->value, keyPairs[idx]->valueLen), @"%zu-th values of length %u don't match",idx,keyPairs[idx]->valueLen);
    }
    
    waitingOnResolution = NO;
    YMmDNSServiceStop(service, false);
}

void test_service_removed(YMmDNSBrowserRef browser, const char *serviceName, void *context)
{
    mDNSTests *SELF = (__bridge mDNSTests *)context;
    if (!context || SELF != gGlobalSelf) [NSException raise:@"test failed" format:@"context is nil or != gGlobalSelf in %s",__FUNCTION__]; // XCT depends on self, NSAssert on _cmd...
    [SELF removed:browser :serviceName];
}

- (void)removed:(YMmDNSBrowserRef)aBrowser :(const char *)serviceName
{
    NSLog(@"%s/%s:? disappeared",testServiceType,serviceName);
    XCTAssert(aBrowser==browser,@"browser pointers %p and %p are not equal on service disappearance",browser,aBrowser);
    
    if ( waitingOnAppearance || waitingOnResolution )
        XCTAssert(strcmp(serviceName,[testServiceName UTF8String]), @"test service disappeared before tearDown");
    else
    {
        NSLog(@"target service removed");
        waitingOnDisappearance = NO;
    }
}

- (void)testBonjourCreateServiceDiscoverAndResolve
{
    BOOL okay;
    testServiceName = [self _randomASCIIStringWithMaxLength:mDNS_SERVICE_NAME_LENGTH_MAX :YES];
    service = YMmDNSServiceCreate(testServiceType, [testServiceName UTF8String], 5050);
    
    nTestKeyPairs = arc4random_uniform(10);
    size_t idx = 0;
    testKeyPairs = (YMmDNSTxtRecordKeyPair **)malloc(nTestKeyPairs * sizeof(YMmDNSTxtRecordKeyPair *));
    
    for ( ; idx < nTestKeyPairs; idx++ )
    {
        testKeyPairs[idx] = calloc(1,sizeof(YMmDNSTxtRecordKeyPair));
        NSString *randomKey = [self _randomASCIIStringWithMaxLength:testKeyLengthBound :NO];
        testKeyPairs[idx]->key = strdup((char *)[randomKey cStringUsingEncoding:NSASCIIStringEncoding]);//"test-key";
        NSData *valueData = [self _randomValueWithMaxLength:(254 - [randomKey lengthOfBytesUsingEncoding:NSASCIIStringEncoding])]; // 256 - '=' - size prefix, ok?
        testKeyPairs[idx]->value = malloc([valueData length]);
        memcpy((void *)testKeyPairs[idx]->value, [valueData bytes], [valueData length]);
        testKeyPairs[idx]->valueLen = [valueData length];
        
        NSLog(@"aKeyPair[%u]: [%u]%s => [%d]", (unsigned)idx, (unsigned)[randomKey length], [randomKey UTF8String], (int)[valueData length]);
    }
    
    okay = YMmDNSServiceSetTXTRecord(service, testKeyPairs, nTestKeyPairs);
    XCTAssert(okay,@"YMmDNSServiceSetTXTRecord failed");
    okay = YMmDNSServiceStart(service);
    XCTAssert(okay,@"YMmDNSServiceStart failed");
    
    // i had these as separate functions, but apparently "self" is a new object for each -test* method, which isn't what we need here
    browser = YMmDNSBrowserCreateWithCallbacks(testServiceType, test_service_appeared, test_service_updated, test_service_resolved, test_service_removed, (__bridge void *)(self));
    okay = YMmDNSBrowserStart(browser);
    XCTAssert(okay,@"YMmDNSBrowserStartBrowsing failed");
    
    NSArray *steps = @[ @[ @"appearance", [NSValue valueWithPointer:&waitingOnAppearance] ],
                        @[ @"resolution", [NSValue valueWithPointer:&waitingOnResolution] ],
                        @[ @"disappearance", [NSValue valueWithPointer:&waitingOnDisappearance] ] ];
    
    [steps enumerateObjectsUsingBlock:^(id  _Nonnull obj, NSUInteger idx, BOOL * _Nonnull stop) {
        NSArray *stepArray = (NSArray *)obj;
        NSString *stepName = stepArray[0];
        BOOL *flag = [(NSValue *)stepArray[1] pointerValue];
        
        NSDate *then = [NSDate date];
        while ( *flag )
        {
            if ( [[NSDate date] timeIntervalSinceDate:then] >= testTimeout )
            {
                *stop = YES;
                XCTAssert(NO, @"timed out waiting for %@",stepName);
                return;
            }
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false); // not using semaphore to avoid starving whatever thread, likely main, this runs on. event delivery isn't configurable.
        }
        
        NSLog(@"%@ happened",stepName);
    }];
}

//- (void)testPerformanceExample {
//    // This is an example of a performance test case.
//    [self measureBlock:^{
//        // Put the code you want to measure the time of here.
//    }];
//}

@end
