//
//  YammerTests.m
//  YammerTests
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <XCTest/XCTest.h>

// for now, cuz nothing wraps it
#import "YMmDNSService.h"
#import "YMmDNSBrowser.h"

@interface YammerTests : XCTestCase
{
    NSString *testServiceName;
    dispatch_semaphore_t waitSemaphore;
    YMmDNSTxtRecordKeyPair **testKeyPairs;
    size_t nTestKeyPairs;
    
    YMmDNSBrowserRef browser;
    YMmDNSServiceRef service;
    
    BOOL stopping;
}

@end

@implementation YammerTests

#define testType "_yammer._tcp"
#define testKeyLengthBound 128
#define testTimeout 5

- (NSString *)_randomASCIIStringWithMaxLength:(uint8_t)maxLength
{
    NSMutableString *string = [NSMutableString string];
    
    uint8_t randomLength = arc4random_uniform(testKeyLengthBound);
    // http://www.zeroconf.org/rendezvous/txtrecords.html
    // The characters of "Name" MUST be printable US-ASCII values (0x20-0x7E), excluding '=' (0x3D).
    uint8_t maxChar = 0x7E, minChar = 0x20;
    uint8_t range = maxChar - minChar;
    
    while ( randomLength-- )
    {
        uint8_t aChar = arc4random_uniform(range + 1) - minChar;
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

static YammerTests *gGlobalSelf = nil;
- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
    
    gGlobalSelf = self; // think callbacks might need a context pointer... TODO
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testCreateService
{
    // This is an example of a functional test case.
    // Use XCTAssert and related functions to verify your tests produce the correct results.
    
    waitSemaphore = dispatch_semaphore_create(0);
    testServiceName = [self _randomASCIIStringWithMaxLength:15];
    
    service = YMmDNSServiceCreate(testType, [testServiceName UTF8String], 5050);
    
    nTestKeyPairs = arc4random_uniform(10);
    size_t idx = 0;
    testKeyPairs = (YMmDNSTxtRecordKeyPair **)malloc(nTestKeyPairs * sizeof(YMmDNSTxtRecordKeyPair *));
    
    for ( ; idx < nTestKeyPairs; idx++ )
    {
        testKeyPairs[idx] = calloc(1,sizeof(YMmDNSTxtRecordKeyPair));
        NSString *randomKey = [self _randomASCIIStringWithMaxLength:testKeyLengthBound];
        testKeyPairs[idx]->key = (char *)[randomKey cStringUsingEncoding:NSASCIIStringEncoding];//"test-key";
        NSData *valueData = [self _randomValueWithMaxLength:(255 - [randomKey lengthOfBytesUsingEncoding:NSASCIIStringEncoding])];//"test-value"; // XXX 255 or 256 (include =?, probably)
        testKeyPairs[idx]->value = malloc([valueData length]);
        memcpy((void *)testKeyPairs[idx]->value, [valueData bytes], [valueData length]);
        testKeyPairs[idx]->valueLen = [valueData length];
    }
    
    YMmDNSServiceSetTXTRecord(service, testKeyPairs, nTestKeyPairs);
    YMmDNSServiceStart(service);
    
    XCTAssert(0 == dispatch_semaphore_wait(waitSemaphore, dispatch_time(DISPATCH_TIME_NOW, testTimeout * NSEC_PER_SEC)),
                  @"service not signaled in %u seconds", testTimeout);
}

void test_service_appeared(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service)
{
    [gGlobalSelf appeared:browser :service];
}

- (void)appeared:(YMmDNSBrowserRef)aBrowser :(YMmDNSServiceRecord *)aService
{
    NSLog(@"%s/%s:? appeared",aService->type,aService->name);
    XCTAssert(aBrowser==browser,@"browser pointers are not equal on service appearance");
    if ( 0 == strcmp(aService->name, [testServiceName UTF8String]) )
        YMmDNSBrowserResolve(browser, aService->name, test_service_resolved);
}

void test_service_removed(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service)
{
    [gGlobalSelf removed:browser :service];
}

- (void)removed:(YMmDNSBrowserRef)aBrowser :(YMmDNSServiceRecord *)aService
{
    NSLog(@"%s/%s:? disappeared",aService->type,aService->name);
    XCTAssert(aBrowser==browser,@"browser pointers %p and %p are not equal on service disappearance",browser,aBrowser);
    
    if ( ! stopping )
        XCTAssert(strcmp(aService->name,[testServiceName UTF8String]), @"test service disappeared before tearDown");
    else
    {
        NSLog(@"target service removed");
        dispatch_semaphore_signal(waitSemaphore);
    }
}

void test_service_updated(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service)
{
    [gGlobalSelf updated:browser :service];
}

- (void)updated:(YMmDNSBrowserRef)aBrowser :(YMmDNSServiceRecord *)aService
{
    NSLog(@"%s/%s:? updated",aService->type,aService->name);
    XCTAssert(browser==aBrowser,@"browser pointers %p and %p are not equal on service update",browser,aBrowser);
}

void test_service_resolved(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, bool resolved)
{
    [gGlobalSelf resolved:browser :service :resolved];
}

- (void)resolved:(YMmDNSBrowserRef)aBrowser :(YMmDNSServiceRecord *)aService :(bool)resolved
{
    XCTAssert(resolved, @"service did not resolve");
    XCTAssert(browser==aBrowser,@"browser pointers %p and %p are not equal on service resolution",browser,aBrowser);
    
    NSLog(@"%s/%s:%d resolved",aService->type,aService->name,aService->port);
    YMmDNSTxtRecordKeyPair **keyPairs = aService->txtRecordKeyPairs;
    size_t keyPairsSize = aService->txtRecordKeyPairsSize,
            idx = 0;
    
    XCTAssert(keyPairsSize == nTestKeyPairs, @"txt record sizes do not match");
    
    for ( ; idx < keyPairsSize; idx++ )
    {
        XCTAssert(0 == strcmp(keyPairs[idx]->key, testKeyPairs[idx]->key), @"%zu-th keys '%s' and '%s' don't match",idx,keyPairs[idx]->key,testKeyPairs[idx]->key);
        XCTAssert(keyPairs[idx]->valueLen == testKeyPairs[idx]->valueLen, @"%zu-th values have different lengths of %u and %u",idx,keyPairs[idx]->valueLen,testKeyPairs[idx]->valueLen);
        XCTAssert(0 == memcmp(keyPairs[idx]->value, testKeyPairs[idx]->value, keyPairs[idx]->valueLen), @"%zu-th values of length %u don't match",idx,keyPairs[idx]->valueLen);
    }
    
    stopping = YES;
    YMmDNSServiceStop(service, false);
    XCTAssert(0==dispatch_semaphore_wait(waitSemaphore, dispatch_time(DISPATCH_TIME_NOW, testTimeout * NSEC_PER_SEC)),@"service didn't disappear within %u seconds",testTimeout);
}


- (void)testBrowse
{
    browser = YMmDNSBrowserCreate(testType, test_service_appeared, test_service_removed);
    YMmDNSBrowserSetServiceUpdatedFunc(browser, test_service_updated);
    YMmDNSBrowserSetServiceResolvedFunc(browser, test_service_resolved);
}

//- (void)testPerformanceExample {
//    // This is an example of a performance test case.
//    [self measureBlock:^{
//        // Put the code you want to measure the time of here.
//    }];
//}

@end
