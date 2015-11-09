//
//  DictionaryTest.m
//  yammer
//
//  Created by david on 11/7/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YammerTests.h"

#include "YMBase.h"
#include "YMDictionary.h"

// at a glance wasn't able to find another way to order test *classes*
//#define RunDictionaryTestFirst 1
#ifdef RunDictionaryTestFirst
#define DictionaryTest_Class DictionaryTest
#else
#define DictionaryTest_Class ZDictionaryTest
#endif

@interface DictionaryTest_Class : XCTestCase
{
    YMDictionaryRef theDictionary;
    NSMutableArray *existingKeys;
    BOOL endTest;
    NSUInteger completedTests;
}
@end

#define NumberOfThreads 8
#define RunFor 5.0
#define MaxItemLength 2048

@implementation DictionaryTest_Class

- (void)setUp {
    [super setUp];
    theDictionary = YMDictionaryCreate();
    existingKeys = [NSMutableArray array];
    completedTests = 0;
    self.continueAfterFailure = NO;
}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
    [super tearDown];
}

- (void)testSerialized {
    
    dispatch_queue_t queue = dispatch_queue_create("dictionary-test-serialized-queue", DISPATCH_QUEUE_SERIAL);
    for ( NSUInteger idx = 0; idx < NumberOfThreads; idx++ )
    {
        dispatch_async(queue, ^{
            [self doDictionaryTest];
        });
    }
    
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(RunFor * NSEC_PER_SEC)), dispatch_get_global_queue(0, 0), ^{
        endTest = YES;
    });
    
    while ( ! endTest )
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, RunFor, false);
    
    NSLog(@"YMDictionary test completed after %zu iterations",completedTests);
}

- (void)doDictionaryTest
{
    NSNumber *stringKey;
    do
    {
        stringKey = @(arc4random());
    } while ( [existingKeys containsObject:stringKey] );
        
    NSNumber *dataKey;
    do
    {
        dataKey = @(arc4random());
    } while ( [existingKeys containsObject:stringKey] );
    
    uint32_t string_key = (uint32_t)[stringKey unsignedLongValue];
    uint32_t data_key = (uint32_t)[dataKey unsignedLongValue];
    
    while (!endTest)
    {
        // add two random values
        NSString *randomString = YMRandomASCIIStringWithMaxLength(arc4random_uniform(MaxItemLength), NO);
        const char* random_string = [randomString UTF8String];
        YMDictionaryAdd(theDictionary,string_key,random_string);
        NSData *randomData = YMRandomDataWithMaxLength(MaxItemLength);
        const void* random_data = [randomData bytes];
        YMDictionaryAdd(theDictionary,data_key,random_data);
        
        // test enumeration
        NSUInteger iters = 0;
        YMDictionaryEnumRef aEnum = YMDictionaryEnumeratorBegin(theDictionary);
        XCTAssert(aEnum,@"enumerator should not be nil");
        while ( aEnum )
        {
            XCTAssert(aEnum->key==string_key||aEnum->key==data_key,@"unknown key: %llu (s%u d%u)",aEnum->key,string_key,data_key);
            XCTAssert(aEnum->value==random_string||aEnum->value==random_data,@"unknown value: %p (s%p d%p)",aEnum->value,random_string,random_data);
            iters++;
            aEnum = YMDictionaryEnumeratorGetNext(aEnum);
        }
        YMDictionaryEnumeratorEnd(aEnum);
        XCTAssert(iters==2,@"iters %zu!=2",iters);
        
        XCTAssert(YMDictionaryContains(theDictionary,data_key),@"dictionary doesn't contain data key!");
        XCTAssert(YMDictionaryContains(theDictionary,string_key),@"dictionary doesn't contain string value!");
        
        // test removal
#ifdef FIXED_POP
        BOOL doPop = arc4random_uniform(2);
        
        if ( doPop )
        {
            BOOL last = arc4random_uniform(2);
            YMDictionaryKey popKey = 0, popKey2 = 0;
            YMDictionaryValue popValue = NULL, popValue2 = NULL;;
            XCTAssert(YMDictionaryPopKeyValue(theDictionary, last, &popKey, &popValue),@"PopKeyValue failed");
            XCTAssert(popKey==string_key||popKey==data_key,@"popKey %llu (s%u d%u)",popKey,string_key,data_key);
            XCTAssert(popValue==random_string||popValue==random_data,@"popValue %p (s%p d%p)",popValue,random_string,random_data);
            
            XCTAssert(YMDictionaryPopKeyValue(theDictionary, last, &popKey2, &popValue2),@"PopKeyValue failed");
            XCTAssert(popKey2==string_key||popKey2==data_key,@"popKey %llu (s%u d%u)",popKey2,string_key,data_key);
            XCTAssert(popValue2!=popValue,@"popValue2==popValue");
            XCTAssert(popValue2==random_string||popValue2==random_data,@"popValue %p (s%p d%p)",popValue2,random_string,random_data);
        }
        else
        {
#endif
            BOOL removeRandomly = arc4random_uniform(2);
            if ( removeRandomly)
            {
                YMDictionaryKey randomKey = YMDictionaryRandomKey(theDictionary);
                YMDictionaryValue randomValue = YMDictionaryRemove(theDictionary, randomKey);
                XCTAssert(randomKey==string_key||randomKey==data_key,@"randomKey unknown %llu (s%u d%u)",randomKey,string_key,data_key);
                XCTAssert(randomValue==random_string||randomValue==random_data,@"randomValue unknown %p (s%p d%p)",randomValue,random_string,random_data);
                
                YMDictionaryKey randomKey2 = YMDictionaryRandomKey(theDictionary);
                YMDictionaryValue randomValue2 = YMDictionaryRemove(theDictionary,randomKey2);
                XCTAssert(randomKey!=randomKey2,@"randomKey==randomKey2");
                XCTAssert(randomValue2!=randomValue,@"randomValue==randomValue2");
                XCTAssert(randomKey2==string_key||randomKey2==data_key,@"randomKey2 unknown %llu (s%u d%u)",randomKey2,string_key,data_key);
                XCTAssert(randomValue2==random_string||randomValue2==random_data,@"randomValue2 unknown %p (s%p d%p)",randomValue2,random_string,random_data);
            }
            else
            {
                XCTAssert(YMDictionaryRemove(theDictionary, string_key),@"failed to remove string by key!");
                XCTAssert(YMDictionaryRemove(theDictionary, data_key),@"failed to remove data by key!");
            }
#ifdef FIXED_POP
        }
#endif
        
        completedTests++;
    }
}

@end
