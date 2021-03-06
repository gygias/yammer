//
//  LocalSocketPairTests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "LocalSocketPairTests.h"

#include "YMLocalSocketPair.h"

YM_EXTERN_C_PUSH

typedef struct LocalSocketPairTest
{
    ym_test_assert_func assert;
    const void *context;
} LocalSocketPairTest;

void _TestSpawnFirst(struct LocalSocketPairTest *theTest);
void _TestSpawnManyStopThenSpawnAnother(struct LocalSocketPairTest *theTest);
void _TestSpawnOneMore(struct LocalSocketPairTest *theTest);

void LocalSocketPairTestsRun(ym_test_assert_func assert, const void *context)
{
    struct LocalSocketPairTest theTest = { assert, context };
    
    _TestSpawnFirst(&theTest);
    ymerr(" _TestSpawnFirst completed");
    _TestSpawnManyStopThenSpawnAnother(&theTest);
    ymerr(" _TestSpawnManyStopThenSpawnAnother completed");
    _TestSpawnOneMore(&theTest);
    ymerr(" _TestSpawnOneMore completed");
}

void _TestSpawnFirst(struct LocalSocketPairTest *theTest)
{
    YMStringRef name = YMSTRC("first");
    YMLocalSocketPairRef aPair = YMLocalSocketPairCreate(name, false);
    YMRelease(name);
    
    int socketA = YMLocalSocketPairGetA(aPair);
    int socketB = YMLocalSocketPairGetB(aPair);
    testassert(socketA>=0&&socketB>=0,"sockets < 0");
    
    YMRelease(aPair);
}

void _TestSpawnManyStopThenSpawnAnother(struct LocalSocketPairTest *theTest)
{
    int nPairs = 10;
    YMDictionaryRef pairs = YMDictionaryCreate();
    for ( int i = 0; i < nPairs; i++ ) {
        YMStringRef name = YMSTRCF("test-socket-%d",i);
        YMLocalSocketPairRef aPair = YMLocalSocketPairCreate(name, ( i < nPairs ));
        YMRelease(name);
        
        testassert(aPair,"pair null");
        
        int socketA = YMLocalSocketPairGetA(aPair);
        int socketB = YMLocalSocketPairGetB(aPair);
        testassert(socketA>=0&&socketB>=0,"sockets < 0");
        
        YMDictionaryAdd(pairs, (YMDictionaryKey)aPair, (void *)aPair);
    }
    
    for ( int i = 0; i < nPairs; i++ ) {
        YMLocalSocketPairRef aPair = YMDictionaryRemove(pairs, YMDictionaryGetRandomKey(pairs));
        
        int socketA = YMLocalSocketPairGetA(aPair);
        int socketB = YMLocalSocketPairGetB(aPair);
        testassert(socketA>=0&&socketB>=0,"sockets < 0"); // 'should' be redundant
        
        YMRelease(aPair);
    }
    
    // should have been stopped in our last loop iter, but this should be safe to screw up
    YMLocalSocketPairStop();
    YMRelease(pairs);
}

void _TestSpawnOneMore(struct LocalSocketPairTest *theTest)
{
    YMStringRef name = YMSTRC("whatever");
    YMLocalSocketPairRef oneMorePair = YMLocalSocketPairCreate(name, false);
    YMRelease(name);
    
    YMSOCKET socketA = YMLocalSocketPairGetA(oneMorePair);
    YMSOCKET socketB = YMLocalSocketPairGetB(oneMorePair);
    testassert(socketA>=0&&socketB>=0,"sockets < 0");
    
    YMRelease(oneMorePair);
}

YM_EXTERN_C_POP
