//
//  YammerTests.m
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <XCTest/XCTest.h>

#import "TaskTests.h"
#import "DictionaryTests.h"
#import "CryptoTests.h"
#import "LocalSocketPairTests.h"
#import "mDNSTests.H"
#import "TLSTests.h"
#import "PlexerTests.h"
#import "SessionTests.h"

#import "YMDictionary.h"
#import "YMUtilities.h"

@interface YammerTests : XCTestCase

@end

@implementation YammerTests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

void ym_test_assert_proc(const void *ctx, bool exp, const char *fmt, ...)
{
    if ( ! exp )
    {
        va_list args;
        va_start(args,fmt);
        vprintf(fmt, args);
        va_end(args);
        
        YammerTests *SELF = (__bridge YammerTests *)(ctx);
        [SELF ymTestAssert];
    }
}

bool ym_test_diff_proc(const void *ctx, const char *path1, const char *path2, bool recursive, YMDictionaryRef exceptions)
{
    __unused YammerTests *SELF = (__bridge YammerTests *)(ctx);
    NSLog(@"diffing %s%s vs %s",recursive?"-r ":"",path1,path2);
    NSPipe *outputPipe = [NSPipe pipe];
    NSTask *diff = [NSTask new];
    [diff setLaunchPath:@"/usr/bin/diff"];
    [diff setArguments:@[@"-r",@(path1),@(path2)]];
    [diff setStandardOutput:outputPipe];
    __block BOOL outputAndOK = YES;
    __block dispatch_semaphore_t outputCheckSem = dispatch_semaphore_create(0);
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        NSData *output = [[outputPipe fileHandleForReading] readDataToEndOfFile];
        NSString *outputStr = [[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding];
        NSArray *lines = [outputStr componentsSeparatedByString:@"\n"];
        [lines enumerateObjectsUsingBlock:^(id  _Nonnull line, __unused NSUInteger idx, BOOL * _Nonnull stop) {
            outputAndOK = YES;
            if ( [(NSString *)line length] == 0 )
                return;
            BOOL lineOK = NO;
            if ( exceptions )
            {
                YMDictionaryEnumRef dEnum = YMDictionaryEnumeratorBegin(exceptions);
                while (dEnum)
                {
                    if ( strstr([line UTF8String],dEnum->value) )
                    {
                        NSLog(@"making exception for %s based on '%@'",dEnum->value,line);
                        lineOK = true;
                        break;
                    }
                    dEnum = YMDictionaryEnumeratorGetNext(dEnum);
                }
                YMDictionaryEnumeratorEnd(dEnum);
            }
            if ( ! lineOK )
            {
                NSLog(@"no match for '%@'",line);
                outputAndOK = NO;
                *stop = YES;
            }
        }];
        dispatch_semaphore_signal(outputCheckSem);
    });
    [diff launch];
    [diff waitUntilExit];
    
    dispatch_semaphore_wait(outputCheckSem, DISPATCH_TIME_FOREVER);
    
    return outputAndOK || ([diff terminationStatus] == 0);
}

- (void)ymTestAssert
{
    XCTAssert(false,"custom assert, see stdout");
}

// this is the best way of ordering XCTestCase classes that i could find, at the time
#define testTask                test_B_Task
#define testDictionary          test_C_Dictionary
#define testCrypto              test_D_Crypto
#define testLocalSocketPair     test_E_LocalSocketPair
#define testmDNS                test_F_mDNS
#define testTLS                 test_G_TLS
#define testPlexer              test_H_Plexer
#define testSession             test_I_Session

- (void)testTask {
    YMCreateLocalInterfaceMap();
    const void *SELF = (__bridge const void *)(self);
    TaskTestsRun(ym_test_assert_proc, SELF);
}

- (void)testDictionary {
    const void *SELF = (__bridge const void *)(self);
    DictionaryTestsRun(ym_test_assert_proc, SELF);
}

- (void)testCrypto {
    const void *SELF = (__bridge const void *)(self);
    CryptoTestsRun(ym_test_assert_proc, SELF);
}

- (void)testLocalSocketPair {
    const void *SELF = (__bridge const void *)(self);
    LocalSocketPairTestsRun(ym_test_assert_proc, SELF);
}

- (void)testmDNS {
    const void *SELF = (__bridge const void *)(self);
    mDNSTestsRun(ym_test_assert_proc, SELF);
}

- (void)testTLS {
    const void *SELF = (__bridge const void *)(self);
    TLSTestsRun(ym_test_assert_proc, SELF);
}

- (void)testPlexer {
    const void *SELF = (__bridge const void *)(self);
    PlexerTestsRun(ym_test_assert_proc, SELF);
}

- (void)testSession {
    const void *SELF = (__bridge const void *)(self);
    SessionTestsRun(ym_test_assert_proc, ym_test_diff_proc, SELF);
}

@end
