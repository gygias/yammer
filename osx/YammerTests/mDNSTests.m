//
//  mDNSTests.m
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YammerTests.h"

#import "YMmDNS.h"
#import "YMmDNSService.h"
#import "YMmDNSBrowser.h"

#include <stdint.h> // uintptr_t and ptr ^ ptr

@interface mDNSTests : XCTestCase
{
    NSString *testServiceName;
    YMmDNSTxtRecordKeyPair **testKeyPairs;
    uint16_t nTestKeyPairs;

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
#define testKeyPairReserved ( 2 ) // length char and '=', length char seems to include itself
#define testKeyMaxLen ( UINT8_MAX - testKeyPairReserved )

#if 0 // actually debugging
#define testTimeout (10 * 60)
#else
#define testTimeout 10
#endif

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
    
    waitingOnAppearance = YES;
    waitingOnResolution = YES;
    waitingOnDisappearance = YES;
    gGlobalSelf = self;
    self.continueAfterFailure = NO;
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
    
    if ( testKeyPairs )
        _YMmDNSTxtRecordKeyPairsFree(testKeyPairs, nTestKeyPairs);
}

- (void)dont_test_mDNSTxtRecordParsing
{
    for(int i = 0; i < 1000; i++)
    {
        uint16_t desiredAndActualSize = (size_t)arc4random_uniform(3);
        YMmDNSTxtRecordKeyPair **keyPairList = [self makeTxtRecordKeyPairs:&desiredAndActualSize];
        uint16_t inSizeOutBlobLen = desiredAndActualSize;
        const unsigned char *listBlob = _YMmDNSCreateTxtBlobFromKeyPairs(keyPairList, &inSizeOutBlobLen);
        size_t outListLen = 0;
        YMmDNSTxtRecordKeyPair **outKeyPairList = _YMmDNSCreateTxtKeyPairs(listBlob, inSizeOutBlobLen, &outListLen);
        [self compareList:keyPairList size:desiredAndActualSize toList:outKeyPairList size:outListLen];
        
        _YMmDNSTxtRecordKeyPairsFree(keyPairList, desiredAndActualSize);
        _YMmDNSTxtRecordKeyPairsFree(outKeyPairList, outListLen);
    }
}

- (void)testmDNSCreateDiscoverResolve
{
    BOOL okay;
    testServiceName = YMRandomASCIIStringWithMaxLength(mDNS_SERVICE_NAME_LENGTH_MAX - 1, YES);
    service = YMmDNSServiceCreate(YMSTRC(testServiceType), YMSTRC([testServiceName UTF8String]), 5050);
    
    nTestKeyPairs = arc4random_uniform(10);
    testKeyPairs = [self makeTxtRecordKeyPairs:&nTestKeyPairs];
    
    okay = YMmDNSServiceSetTXTRecord(service, testKeyPairs, nTestKeyPairs);
    XCTAssert(okay||nTestKeyPairs==0,@"YMmDNSServiceSetTXTRecord failed");
    okay = YMmDNSServiceStart(service);
    XCTAssert(okay,@"YMmDNSServiceStart failed");
    
    // i had these as separate functions, but apparently "self" is a new object for each -test* method, which isn't what we need here
    browser = YMmDNSBrowserCreateWithCallbacks(YMSTRC(testServiceType), test_service_appeared, test_service_updated, test_service_resolved, test_service_removed, (__bridge void *)(self));
    _YMmDNSBrowserDebugSetExpectedTxtKeyPairs(browser,nTestKeyPairs);
    okay = YMmDNSBrowserStart(browser);
    XCTAssert(okay,@"YMmDNSBrowserStartBrowsing failed");
    
    NSArray *steps = @[ @[ @"appearance", [NSValue valueWithPointer:&waitingOnAppearance] ],
                        @[ @"resolution", [NSValue valueWithPointer:&waitingOnResolution] ],
                        @[ @"disappearance", [NSValue valueWithPointer:&waitingOnDisappearance] ] ];
    
    [steps enumerateObjectsUsingBlock:^(id  _Nonnull obj, __unused NSUInteger _idx, BOOL * _Nonnull stop) {
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
    
    okay = YMmDNSBrowserStop(browser);
    YMRelease(browser);
}

- (YMmDNSTxtRecordKeyPair **)makeTxtRecordKeyPairs:(uint16_t *)inOutnKeypairs
{
    size_t requestedSize = *inOutnKeypairs;
    size_t actualSize = 0;
    size_t debugBlobSize = 0;
    
    if ( requestedSize == 0 )
        return NULL;
    
    YMmDNSTxtRecordKeyPair **keyPairs = (YMmDNSTxtRecordKeyPair **)calloc(*inOutnKeypairs,sizeof(YMmDNSTxtRecordKeyPair *));
    
    uint16_t remaining = UINT16_MAX;
    for ( size_t idx = 0; idx < requestedSize; idx++ )
    {
        keyPairs[idx] = calloc(1,sizeof(YMmDNSTxtRecordKeyPair));
        
        remaining -= testKeyPairReserved; // '=' and length byte (which seems to be included in its own length)
        
        // The "Name" MUST be at least one character. Strings beginning with an '=' character (i.e. the name is missing) SHOULD be silently ignored.
        uint8_t aKeyLenMax = ( testKeyMaxLen > remaining ) ? ( remaining - testKeyPairReserved ) : testKeyMaxLen;
        NSString *randomKey = YMRandomASCIIStringWithMaxLength(aKeyLenMax, NO);
        keyPairs[idx]->key = YMSTRC([randomKey cStringUsingEncoding:NSASCIIStringEncoding]);//"test-key";
        
        remaining -= [randomKey length];
        
        // as far as i can tell, value can be empty
        uint8_t valueLenMax = ( UINT8_MAX - [randomKey length] - testKeyPairReserved );
        uint16_t aValueLenMax = ( valueLenMax > remaining ) ? remaining : valueLenMax;
        NSData *valueData = YMRandomDataWithMaxLength(aValueLenMax);
        keyPairs[idx]->value = calloc(1,[valueData length]);
        memcpy((void *)keyPairs[idx]->value, [valueData bytes], [valueData length]);
        keyPairs[idx]->valueLen = (uint8_t)[valueData length];
        
        remaining -= [valueData length];
        
        actualSize++;
        debugBlobSize += testKeyPairReserved + [randomKey length] + [valueData length];
        NoisyTestLog(@"aKeyPair[%zd]: [%zu] <= [%zu]'%s'", idx,  [valueData length], [randomKey length], [randomKey UTF8String]);
        
        if ( remaining == 0 )
            break;
        if ( remaining < 0 )
            abort();
    }
    
    NoisyTestLog(@"made txt record length %zu out of requested %zu (blob size %zu)",actualSize,requestedSize,debugBlobSize);
    *inOutnKeypairs = actualSize;
    
    return keyPairs;
}

- (void)compareList:(YMmDNSTxtRecordKeyPair **)aList size:(size_t)aSize
             toList:(YMmDNSTxtRecordKeyPair **)bList size:(size_t)bSize
{
    if ( aSize != bSize )
    {
        for ( size_t i = 0; i < nTestKeyPairs; i++ )
        {
            if ( i < aSize )
            {
                YMmDNSTxtRecordKeyPair *aPair = aList[i];
                NSLog(@"a [%zd]: %zd -> %d (%s)",i,YMStringGetLength(aPair->key),aPair->valueLen,YMSTR(aPair->key));
            }
            if ( i < bSize )
            {
                YMmDNSTxtRecordKeyPair *aPair = bList[i];
                NSLog(@"b [%zd]: %zd -> %d (%s)",i,YMStringGetLength(aPair->key),aPair->valueLen,YMSTR(aPair->key));
            }
        }
    }
    
    XCTAssert(aSize==bSize,@"sizes don't match"); // todo still happens... documentation
    
    if ( aList == NULL && bList == NULL ) // i guess
        return;
    
    XCTAssert( (uintptr_t)aList ^ (uintptr_t)bList, @"null list vs non-null list");
    
    for ( size_t i = 0; i < aSize; i++ )
    {
        XCTAssert(aList[i]->key&&bList[i],@"a key %zdth null",i);
        XCTAssert(0 == strcmp(YMSTR(aList[i]->key), YMSTR(bList[i]->key)), @"%zd-th keys '%s' and '%s' don't match",i,YMSTR(aList[i]->key),YMSTR(bList[i]->key));
        XCTAssert(aList[i]->value&&aList[i]->value,@"a value %zdth null",i);
        XCTAssert(aList[i]->valueLen == bList[i]->valueLen, @"%zd-th values have different lengths of %u and %u",i,aList[i]->valueLen,bList[i]->valueLen);
        XCTAssert(0 == memcmp(aList[i]->value, bList[i]->value, aList[i]->valueLen), @"%zu-th values of length %u don't match",i,aList[i]->valueLen);
    }
}

void test_service_appeared(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    mDNSTests *SELF = (__bridge mDNSTests *)context;
    if (!context || SELF != gGlobalSelf) [NSException raise:@"test failed" format:@"context is nil or != gGlobalSelf in %s",__FUNCTION__]; // XCT depends on self, NSAssert on _cmd...
    [SELF appeared:browser :service];
}

- (void)appeared:(YMmDNSBrowserRef)aBrowser :(YMmDNSServiceRecord *)aService
{
    NSLog(@"%s/%s:? appeared",YMSTR(aService->type),YMSTR(aService->name));
    XCTAssert(aBrowser==browser,@"browser pointers are not equal on service appearance");
    if ( waitingOnAppearance && 0 == strcmp(YMSTR(aService->name), [testServiceName cStringUsingEncoding:NSASCIIStringEncoding]) )
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
    NSLog(@"%s/%s:? updated",YMSTR(aService->type),YMSTR(aService->name));
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
    
    NSLog(@"%s/%s:%d resolved",YMSTR(aService->type),YMSTR(aService->name),aService->port);
    YMmDNSTxtRecordKeyPair **keyPairs = aService->txtRecordKeyPairs;
    size_t keyPairsSize = aService->txtRecordKeyPairsSize;
    
    [self compareList:keyPairs size:keyPairsSize toList:testKeyPairs size:nTestKeyPairs];
    
    waitingOnResolution = NO;
    YMmDNSServiceStop(service, false);
}

void test_service_removed(YMmDNSBrowserRef browser, YMStringRef serviceName, void *context)
{
    mDNSTests *SELF = (__bridge mDNSTests *)context;
    if (!context || SELF != gGlobalSelf) [NSException raise:@"test failed" format:@"context is nil or != gGlobalSelf in %s",__FUNCTION__]; // XCT depends on self, NSAssert on _cmd...
    [SELF removed:browser :serviceName];
}

- (void)removed:(YMmDNSBrowserRef)aBrowser :(YMStringRef)serviceName
{
    NSLog(@"%s/%s:? disappeared",testServiceType,YMSTR(serviceName));
    XCTAssert(aBrowser==browser,@"browser pointers %p and %p are not equal on service disappearance",browser,aBrowser);
    
    if ( waitingOnAppearance || waitingOnResolution )
        XCTAssert(strcmp(YMSTR(serviceName),[testServiceName UTF8String]), @"test service disappeared before tearDown");
    else
    {
        NSLog(@"target service removed");
        waitingOnDisappearance = NO;
    }
}

@end
