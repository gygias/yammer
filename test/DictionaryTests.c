//
//  DictionaryTests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "DictionaryTests.h"

#include "YMDictionary.h"
#include "YMLock.h"
#include "YMSemaphore.h"
#include "YMString.h"
#include "YMUtilities.h"

YM_EXTERN_C_PUSH

#define RunFor 15.0
#define MaxItemLength 2048

YM_ENTRY_POINT(_dictionary_test_proc);

typedef struct DictionaryTest
{
    YMDictionaryRef dictionary;
    YMLockRef lock;
    YMSemaphoreRef semaphore;
    YMDictionaryRef existingKeys;
    bool endTest;
    uint64_t completedTests;
    ym_test_assert_func assert;
    const void *context;
} DictionaryTest;

void DictionaryTestsRun(ym_test_assert_func assert, const void *context)
{
    DictionaryTest theTest = { YMDictionaryCreate(), YMLockCreate(), YMSemaphoreCreate(0), YMDictionaryCreate(), false, 0, assert, context };
    
    YMStringRef name = YMSTRC("DictionaryTestQueue");
    YMRelease(name);
    
    int nThreads = 16;
    ymlog("DictionaryTests has decided to run %d instances",nThreads);
    ym_dispatch_user_t dispatch = { _dictionary_test_proc, &theTest, NULL, ym_dispatch_user_context_noop };
    for ( int i = 0; i < nThreads; i++ ) {
        YMDispatchAsync(YMDispatchGetGlobalQueue(), &dispatch);
    }
    
    sleep(RunFor);
    theTest.endTest = true;
    
    for ( int i = 0; i < nThreads; i++ )
        YMSemaphoreWait(theTest.semaphore);
    
    YMRelease(theTest.dictionary);
    YMRelease(theTest.semaphore);
    YMRelease(theTest.lock);
    YMRelease(theTest.existingKeys);
    
    ymerr("YMDictionary test completed after %"PRIu64" iterations",theTest.completedTests);
}

YM_ENTRY_POINT(_dictionary_test_proc)
{
    struct DictionaryTest *theTest = context;
    
    YMDictionaryKey string_key;
    YMDictionaryKey data_key;
    
    YMLockLock(theTest->lock);
    {
        do { string_key = (YMDictionaryKey)(int64_t)arc4random();
        } while ( YMDictionaryContains(theTest->dictionary, string_key) );
        
        do { data_key = (YMDictionaryKey)(int64_t)arc4random();
        } while ( YMDictionaryContains(theTest->dictionary, data_key) || data_key == string_key );
    }
    YMLockUnlock(theTest->lock);
    
    while (!theTest->endTest) {
        char random_string[MaxItemLength];
        YMRandomASCIIStringWithLength(random_string,MaxItemLength,false,false);
        uint8_t random_data[MaxItemLength];
        YMRandomDataWithLength(random_data,MaxItemLength);
        
        YMLockLock(theTest->lock);
        {
            // add two random values
            YMDictionaryAdd(theTest->dictionary,string_key,random_string);
            YMDictionaryAdd(theTest->dictionary,data_key,random_data);
            
            // test enumeration
            int iters = 0;
            YMDictionaryEnumRef aEnum = YMDictionaryEnumeratorBegin(theTest->dictionary);
            testassert(aEnum,"enumerator should not be nil");
            while ( aEnum ) {
                testassert(aEnum->key==string_key||aEnum->key==data_key,"unknown key: %llu (s%u d%u)",aEnum->key,string_key,data_key);
                testassert(aEnum->value==random_string||aEnum->value==random_data,"unknown value: %p (s%p d%p)",aEnum->value,random_string,random_data);
                iters++;
                aEnum = YMDictionaryEnumeratorGetNext(aEnum);
            }
            YMDictionaryEnumeratorEnd(aEnum);
            testassert(iters==2,"iters %zu!=2",iters);
            
            testassert(YMDictionaryContains(theTest->dictionary,data_key),"dictionary doesn't contain data key!");
            testassert(YMDictionaryContains(theTest->dictionary,string_key),"dictionary doesn't contain string value!");
            
            // test removal
            bool removeRandomly = arc4random_uniform(2);
            if ( removeRandomly ) {
                YMDictionaryKey randomKey = YMDictionaryGetRandomKey(theTest->dictionary);
                YMDictionaryValue randomValue = YMDictionaryRemove(theTest->dictionary, randomKey);
                testassert(randomKey==string_key||randomKey==data_key,"randomKey unknown %llu (s%u d%u)",randomKey,string_key,data_key);
                testassert(randomValue==random_string||randomValue==random_data,"randomValue unknown %p (s%p d%p)",randomValue,random_string,random_data);
                
                YMDictionaryKey randomKey2 = YMDictionaryGetRandomKey(theTest->dictionary);
                YMDictionaryValue randomValue2 = YMDictionaryRemove(theTest->dictionary,randomKey2);
                testassert(randomKey!=randomKey2,"randomKey==randomKey2");
                testassert(randomValue2!=randomValue,"randomValue==randomValue2");
                testassert(randomKey2==string_key||randomKey2==data_key,"randomKey2 unknown %llu (s%u d%u)",randomKey2,string_key,data_key);
                testassert(randomValue2==random_string||randomValue2==random_data,"randomValue2 unknown %p (s%p d%p)",randomValue2,random_string,random_data);
            } else {
                testassert(YMDictionaryRemove(theTest->dictionary, string_key),"failed to remove string by key!");
                testassert(YMDictionaryRemove(theTest->dictionary, data_key),"failed to remove data by key!");
            }
            theTest->completedTests++;
        }
        YMLockUnlock(theTest->lock);
    }
    
    YMSemaphoreSignal(theTest->semaphore);
}

YM_EXTERN_C_POP
