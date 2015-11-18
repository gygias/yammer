//
//  LocalSocketPairTests.m
//  yammer
//
//  Created by david on 11/17/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YammerTests.h"

#import "YMLocalSocketPair.h"

@interface LocalSocketPairTests : XCTestCase

@end

@implementation LocalSocketPairTests

- (void)setUp {
    [super setUp];
    // Put setup code here. This method is called before the invocation of each test method in the class.
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testSpawnFirst
{
    YMLocalSocketPairRef oneMorePair = YMLocalSocketPairCreate(YMSTRC("first"), false);
    
    int socketA = YMLocalSocketPairGetA(oneMorePair);
    int socketB = YMLocalSocketPairGetB(oneMorePair);
    XCTAssert(socketA>=0&&socketB>=0,@"sockets < 0");
    
    YMRelease(oneMorePair);
}

- (void)testSpawnManyStopThenSpawnAnother {
    
    int nPairs = 10;
    NSMutableArray *pairs = [NSMutableArray array];
    for ( int i = 0; i < nPairs; i++ )
    {
        YMStringRef name = YMSTRCF("test-socket-%d",i,NULL);
        YMLocalSocketPairRef aPair = YMLocalSocketPairCreate(name, ( i < nPairs - 1 ));
        YMRelease(name);
        
        XCTAssert(aPair,@"pair null");
        
        int socketA = YMLocalSocketPairGetA(aPair);
        int socketB = YMLocalSocketPairGetB(aPair);
        XCTAssert(socketA>=0&&socketB>=0,@"sockets < 0");
    
        [pairs addObject:[NSValue valueWithPointer:aPair]];
    }
    
    for ( int i = 0; i < nPairs; i++ )
    {
        YMLocalSocketPairRef aPair = [(NSValue *)[pairs lastObject] pointerValue];
        [pairs removeLastObject];
        
        int socketA = YMLocalSocketPairGetA(aPair);
        int socketB = YMLocalSocketPairGetB(aPair);
        XCTAssert(socketA>=0&&socketB>=0,@"sockets < 0"); // 'should' be redundant
        
        YMRelease(aPair);
    }
    
    // should have been stopped in our last loop iter, but this should be safe to screw up
    YMLocalSocketPairStop();
}

- (void)testSpawnOneMore
{
    YMLocalSocketPairRef oneMorePair = YMLocalSocketPairCreate(YMSTRC("whatever"), false);
    
    int socketA = YMLocalSocketPairGetA(oneMorePair);
    int socketB = YMLocalSocketPairGetB(oneMorePair);
    XCTAssert(socketA>=0&&socketB>=0,@"sockets < 0");
    
    YMRelease(oneMorePair);
}

@end
