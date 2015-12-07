//
//  CheckStateTest.m
//  yammer
//
//  Created by david on 11/21/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <XCTest/XCTest.h>

#import "Tests.h"

@interface CheckStateTest : XCTestCase

@end

@implementation CheckStateTest

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testCheckStateTest {
    YMFreeGlobalResources();
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2, false);
    
    NSArray *magicNoLibrariesPattern = @[@"-v", @"> /"];
    
    // TODO today: ensure none of our threads exist in this sample
    [self _runTaskAndGrep:@"/usr/bin/sample" :@[@"-file",@"/dev/stdout",@"xctest",@"1",@"1000"] :magicNoLibrariesPattern :YES :NO :@[@"_ym_"]];
    [self _runTaskAndGrep:@"/usr/bin/leaks"
                         :@[[NSString stringWithFormat:@"%d",[[NSProcessInfo processInfo] processIdentifier]],@"--nocontext"]
                         :magicNoLibrariesPattern
                         :NO
                         :YES
                         :nil];
}

- (void)_runTaskAndGrep:(NSString *)path :(NSArray *)args :(NSArray *)grepArgs :(BOOL)printOutputOnSuccess :(BOOL)assertExitStatus :(NSArray *)assertOutputDoesNotContainStrings
{
    NSTask *task = [NSTask new];
    [task setLaunchPath:path];
    [task setArguments:args];
    NSTask *grep = [NSTask new];
    [grep setLaunchPath:@"/usr/bin/grep"];
    [grep setArguments:grepArgs];
    
    NSPipe *pipe = [NSPipe new];
    [task setStandardOutput:pipe];
    [grep setStandardInput:pipe];
    NSPipe *pipe2 = [NSPipe new];
    [grep setStandardOutput:pipe2];
    
    [task launch];
    [grep launch];
    
    [grep waitUntilExit];
    
    BOOL fail = assertExitStatus && ( [task terminationStatus] != 0 );
    
    NSData *output = [[pipe2 fileHandleForReading] readDataToEndOfFile];
    NSString *outputStr = [[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding];
    
    if ( ! fail && assertOutputDoesNotContainStrings )
    {
        for ( NSString *string in assertOutputDoesNotContainStrings )
        {
            fail = ( [outputStr rangeOfString:string].location != NSNotFound );
            if ( fail )
            {
                NSLog(@"output contains '%@'",string);
                break;
            }
        }
    }
    if ( printOutputOnSuccess || fail )
        NSLog(@"%@",outputStr);
    
    NSLog(@"`%@ %@` returned %d",path,[args componentsJoinedByString:@" "],[task terminationStatus]);
    
    XCTAssert(!fail,@"%@ isn't supposed to return that.",path);
}

@end
