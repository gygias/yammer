//
//  DictionaryTests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "DictionaryTests.h"

#include "YMDictionary.h"
#include "YMThread.h"
#include "YMLock.h"
#include "YMSemaphore.h"
#include "YMString.h"

#define NumberOfThreads 8
#define RunFor 5.0
#define MaxItemLength 2048

#define testassert(x,y,...) theTest->assertFunc(theTest->funcContext,(x),y,##__VA_ARGS__);

void _dictionary_test_proc(void *ctx);

typedef struct DictionaryTest
{
    YMDictionaryRef dictionary;
    YMLockRef lock;
    YMSemaphoreRef semaphore;
    YMDictionaryRef existingKeys;
    bool endTest;
    uint64_t completedTests;
    ym_test_assert_func assertFunc;
    const void *funcContext;
} DictionaryTest;

void DictionaryTestRun(ym_test_assert_func assertFunc, const void *funcContext)
{
    DictionaryTest theTest = { YMDictionaryCreate(), YMLockCreate(), YMSemaphoreCreate(0), YMDictionaryCreate(), false, 0, assertFunc, funcContext };
    
    YMStringRef name = YMSTRC("DictionaryTestQueue");
    YMRelease(name);
    
    YMThreadRef threads[NumberOfThreads];
    for ( int i = 0; i < NumberOfThreads; i++ )
    {
        name = YMSTRCF("DictionaryTest-%d",i);
        threads[i] = YMThreadCreate(name, _dictionary_test_proc, &theTest);
        YMThreadStart(threads[i]);
        YMRelease(name);
    }
    
    sleep(RunFor);
    theTest.endTest = true;
    
    for ( int i = 0; i < NumberOfThreads; i++ )
        YMSemaphoreWait(theTest.semaphore);
    for ( int i = 0; i < NumberOfThreads; i++ )
        YMRelease(threads[i]);
    
    ymlog("YMDictionary test completed after %llu iterations",theTest.completedTests);
}    

void _dictionary_test_proc(void *ctx)
{
    struct DictionaryTest *theTest = ctx;
    
    uint32_t string_key;
    uint32_t data_key;
    
    YMLockLock(theTest->lock);
    {
        do { string_key = arc4random();
        } while ( YMDictionaryContains(theTest->dictionary, string_key) );
        
        do { data_key = arc4random();
        } while ( YMDictionaryContains(theTest->dictionary, data_key) );
    }
    YMLockUnlock(theTest->lock);
    
    while (!theTest->endTest)
    {
        const char *random_string = YMRandomASCIIStringWithMaxLength(arc4random_uniform(MaxItemLength), false);
        const uint8_t *random_data = YMRandomDataWithMaxLength(MaxItemLength,NULL);
        
        YMLockLock(theTest->lock);
        {
            // add two random values
            YMDictionaryAdd(theTest->dictionary,string_key,random_string);
            YMDictionaryAdd(theTest->dictionary,data_key,random_data);
            
            // test enumeration
            int iters = 0;
            YMDictionaryEnumRef aEnum = YMDictionaryEnumeratorBegin(theTest->dictionary);
            testassert(aEnum,"enumerator should not be nil");
            while ( aEnum )
            {
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
            if ( removeRandomly )
            {
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
            }
            else
            {
                testassert(YMDictionaryRemove(theTest->dictionary, string_key),"failed to remove string by key!");
                testassert(YMDictionaryRemove(theTest->dictionary, data_key),"failed to remove data by key!");
            }
            theTest->completedTests++;
        }
        YMLockUnlock(theTest->lock);
        
        free((void *)random_string);
        free((void *)random_data);
    }
    
    YMSemaphoreSignal(theTest->semaphore);
}