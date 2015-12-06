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
    
    NSArray *noLibrariesPattern = @[@"-v", @"> /"];
    [self _runTaskAndGrep:@"/usr/bin/sample" :@[@"-file",@"/dev/stdout",@"xctest",@"1",@"1000"] :noLibrariesPattern :YES :NO];
    [self _runTaskAndGrep:@"/usr/bin/leaks"
                         :@[[NSString stringWithFormat:@"%d",[[NSProcessInfo processInfo] processIdentifier]],@"--nocontext"]
                         :noLibrariesPattern
                         :NO
                         :YES];
}

- (void)_runTaskAndGrep:(NSString *)path :(NSArray *)args :(NSArray *)grepArgs :(BOOL)printOutputOnSuccess :(BOOL)assert
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
    
    BOOL fail = assert && ( [task terminationStatus] != 0 );
    
    if ( printOutputOnSuccess || fail )
    {
        NSData *output = [[pipe2 fileHandleForReading] readDataToEndOfFile];
        NSLog(@"%@",[[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding]);
    }
    
    NSLog(@"`%@ %@` returned %d",path,[args componentsJoinedByString:@" "],[task terminationStatus]);
    
    XCTAssert(!fail,@"%@ isn't supposed to return that.",path);
}

@end
