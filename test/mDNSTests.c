//
//  mDNSTests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "mDNSTests.h"

#include "YMmDNSBrowser.h"
#include "YMmDNSService.h"

YM_EXTERN_C_PUSH

typedef struct mDNSTest
{
    ym_test_assert_func assert;
    const void *context;
    
    char *testServiceName;
    YMmDNSTxtRecordKeyPair **testKeyPairs;
    uint16_t nTestKeyPairs;
    
    YMmDNSBrowserRef browser;
    YMmDNSServiceRef service;
    
    bool waitingOnAppearance;
    bool waitingOnResolution;
    bool waitingOnDisappearance;
} mDNSTest;

#define testServiceType "_yammer._tcp"
#define testKeyPairReserved ( 3 ) // length char, '=' and zero-length
#define testKeyMaxLen ( UINT8_MAX - testKeyPairReserved )

#if 0 // actually debugging
#define testTimeout (10 * 60)
#else
#define testTimeout 10
#endif

void _TestmDNSTxtRecordParsing(struct mDNSTest *theTest);
void _TestmDNSCreateDiscoverResolve(struct mDNSTest *theTest);

void test_service_appeared(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context);
void test_service_updated(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context);
void test_service_resolved(YMmDNSBrowserRef browser, bool resolved, YMmDNSServiceRecord *service, void *context);
void test_service_removed(YMmDNSBrowserRef browser, YMStringRef serviceName, void *context);

YMmDNSTxtRecordKeyPair ** _MakeTxtRecordKeyPairs(uint16_t *inOutnKeypairs);
void _CompareTxtList(struct mDNSTest *theTest, YMmDNSTxtRecordKeyPair **aList, size_t aSize, YMmDNSTxtRecordKeyPair **bList, size_t bSize);

void mDNSTestRun(ym_test_assert_func assert, const void *context)
{
    struct mDNSTest theTest = { assert, context, NULL, NULL, 0, NULL, NULL, true, true, true };
    
    _TestmDNSTxtRecordParsing(&theTest);
    _TestmDNSCreateDiscoverResolve(&theTest);
    
    free(theTest.testServiceName);
    if ( theTest.testKeyPairs )
        _YMmDNSTxtKeyPairsFree(theTest.testKeyPairs, theTest.nTestKeyPairs);
}

void _TestmDNSTxtRecordParsing(struct mDNSTest *theTest)
{
    for(int i = 0; i < 1000; i++)
    {
        uint16_t desiredAndActualSize = (size_t)arc4random_uniform(3);
        YMmDNSTxtRecordKeyPair **keyPairList = _MakeTxtRecordKeyPairs(&desiredAndActualSize);
        uint16_t inSizeOutBlobLen = desiredAndActualSize;
        unsigned char *listBlob = _YMmDNSTxtBlobCreate(keyPairList, &inSizeOutBlobLen);
        size_t outListLen = 0;
        YMmDNSTxtRecordKeyPair **outKeyPairList = _YMmDNSTxtKeyPairsCreate(listBlob, inSizeOutBlobLen, &outListLen);
        _CompareTxtList(theTest,keyPairList,desiredAndActualSize,outKeyPairList,outListLen);
        
        _YMmDNSTxtKeyPairsFree(keyPairList, desiredAndActualSize);
        _YMmDNSTxtKeyPairsFree(outKeyPairList, outListLen);
        free(listBlob);
    }
}

void _TestmDNSCreateDiscoverResolve(struct mDNSTest *theTest)
{
    bool okay;
    theTest->testServiceName = YMRandomASCIIStringWithMaxLength(mDNS_SERVICE_NAME_LENGTH_MAX - 1, true, false);
    YMStringRef serviceType = YMSTRC(testServiceType);
    YMStringRef serviceName = YMSTRC(theTest->testServiceName);
    theTest->service = YMmDNSServiceCreate(serviceType, serviceName, 5050);
    testassert(theTest->service,"YMmDNSServiceCreate");
    YMRelease(serviceName);
    
    theTest->nTestKeyPairs = arc4random_uniform(10);
    theTest->testKeyPairs = _MakeTxtRecordKeyPairs(&theTest->nTestKeyPairs);
    
    okay = YMmDNSServiceSetTXTRecord(theTest->service, theTest->testKeyPairs, theTest->nTestKeyPairs);
    testassert(okay||theTest->nTestKeyPairs==0,"YMmDNSServiceSetTXTRecord failed");
    okay = YMmDNSServiceStart(theTest->service);
    testassert(okay,"YMmDNSServiceStart");
    
    // i had these as separate functions, but apparently "self" is a new object for each -test* method, which isn't what we need here
    theTest->browser = YMmDNSBrowserCreateWithCallbacks(serviceType, test_service_appeared, test_service_updated, test_service_resolved, test_service_removed, theTest);
    YMRelease(serviceType);
    okay = YMmDNSBrowserStart(theTest->browser);
    testassert(okay,"YMmDNSBrowserStartBrowsing");
    
    int nSteps = 3;
    intptr_t steps[3][2] = { { (intptr_t)"appearance", (intptr_t)&theTest->waitingOnAppearance },
                            { (intptr_t)"resolution", (intptr_t)&theTest->waitingOnResolution },
                            { (intptr_t)"disappearance", (intptr_t)&theTest->waitingOnDisappearance } };
    
    for ( int i = 0; i < nSteps; i++ )
    {
        const char *name = (const char *)steps[i][0];
        bool *flag = (bool *)steps[i][1];
     
        time_t startTime = time(NULL);
        while ( *flag )
        {
            if ( ( time(NULL) - startTime ) >= testTimeout )
            {
                testassert(false, "timed out waiting for %s",name);
                return;
            }
	
			usleep(10000);
        }
        ymlog("%s happened",name);
    }
    
    
    okay = YMmDNSBrowserStop(theTest->browser);
    YMRelease(theTest->browser);
}

YMmDNSTxtRecordKeyPair ** _MakeTxtRecordKeyPairs(uint16_t *inOutnKeypairs)
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
        
        remaining -= testKeyPairReserved; // length char, '=' and zero-length
        
        // The "Name" MUST be at least one character. Strings beginning with an '=' character (i.e. the name is missing) SHOULD be silently ignored.
        uint8_t aKeyLenMax = ( testKeyMaxLen > remaining ) ? ( remaining - testKeyPairReserved ) : testKeyMaxLen;
        char *randomKey = YMRandomASCIIStringWithMaxLength(aKeyLenMax, false, true);
        keyPairs[idx]->key = YMSTRC(randomKey);//"test-key";
        
        size_t keyLen = strlen(randomKey);
        remaining -= (uint16_t)keyLen;
        
        // as far as i can tell, value can be empty
        uint8_t valueLenMax = (uint8_t)( UINT8_MAX - keyLen - testKeyPairReserved );
        uint16_t aValueLenMax = ( valueLenMax > remaining ) ? remaining : valueLenMax;
        uint16_t valueLen;
        const uint8_t *value_data = YMRandomDataWithMaxLength(aValueLenMax, &valueLen);
        keyPairs[idx]->value = value_data;
        keyPairs[idx]->valueLen = (uint8_t)valueLen;
        
        remaining -= valueLen;
        
        actualSize++;
        debugBlobSize += testKeyPairReserved + keyLen + valueLen;
        NoisyTestLog("aKeyPair[%zd]: [%zu] <= [%zu]'%s'", idx,  [valueData length], [randomKey length], [randomKey UTF8String]);
        
        if ( remaining == 0 )
            break;
        if ( remaining < 0 )
            abort();
    }
    
    NoisyTestLog(@"made txt record length %zu out of requested %zu (blob size %zu)",actualSize,requestedSize,debugBlobSize);
    *inOutnKeypairs = (uint16_t)actualSize;
    
    return keyPairs;
}

void _CompareTxtList(struct mDNSTest *theTest, YMmDNSTxtRecordKeyPair **aList, size_t aSize, YMmDNSTxtRecordKeyPair **bList, size_t bSize)
{
    if ( aSize != bSize )
    {
        for ( size_t i = 0; i < theTest->nTestKeyPairs; i++ )
        {
            if ( i < aSize )
            {
                YMmDNSTxtRecordKeyPair *aPair = aList[i];
                ymlog("a [%zd]: %zd -> %d (%s)",i,YMStringGetLength(aPair->key),aPair->valueLen,YMSTR(aPair->key));
            }
            if ( i < bSize )
            {
                YMmDNSTxtRecordKeyPair *bPair = bList[i];
                ymlog("b [%zd]: %zd -> %d (%s)",i,YMStringGetLength(bPair->key),bPair->valueLen,YMSTR(bPair->key));
            }
        }
    }
    
    testassert(aSize==bSize,"sizes don't match"); // todo still happens... documentation
    
    if ( aList == NULL && bList == NULL ) // i guess
        return;
    
    testassert((uintptr_t)aList ^ (uintptr_t)bList,"null list vs non-null list");
    
    for ( size_t i = 0; i < aSize; i++ )
    {
        testassert(aList[i]->key&&bList[i],"a key %zdth null",i);
        testassert(0 == strcmp(YMSTR(aList[i]->key), YMSTR(bList[i]->key)),"%zd-th keys '%s' and '%s' don't match",i,YMSTR(aList[i]->key),YMSTR(bList[i]->key));
        testassert(aList[i]->value&&aList[i]->value,"a value %zdth null",i);
        testassert(aList[i]->valueLen == bList[i]->valueLen,"%zd-th values have different lengths of %u and %u",i,aList[i]->valueLen,bList[i]->valueLen);
        testassert(0 == memcmp(aList[i]->value, bList[i]->value, aList[i]->valueLen),"%zu-th values of length %u don't match",i,aList[i]->valueLen);
    }
}

void test_service_appeared(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    struct mDNSTest *theTest = context;
    testassert(theTest,"appeared callback context");
    
    ymlog("%s/%s:? appeared",YMSTR(service->type),YMSTR(service->name));
    testassert(browser==theTest->browser,"browser pointers are not equal on service appearance");
    if ( theTest->waitingOnAppearance && 0 == strcmp(YMSTR(service->name), theTest->testServiceName) )
    {
        theTest->waitingOnAppearance = false;
        ymlog("resolving...");
        bool startedResolve = YMmDNSBrowserResolve(browser, service->name);
        testassert(startedResolve,"YMmDNSBrowserResolve failed");
    }
}

void test_service_updated(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    struct mDNSTest *theTest = context;
    testassert(theTest,"updated callback context");
    testassert(theTest->browser==browser,"browser pointers %p != %p",theTest->browser,browser);

    ymlog("%s/%s:? updated",YMSTR(service->type),YMSTR(service->name));
}

void test_service_resolved(YMmDNSBrowserRef browser, bool resolved, YMmDNSServiceRecord *service, void *context)
{
    struct mDNSTest *theTest = context;
    testassert(theTest,"resolved callback context");
    testassert(theTest->browser==browser,"browser pointers %p != %p",theTest->browser,browser);
    
    testassert(resolved, "service did not resolve");
    
    ymlog("%s/%s:%d resolved",YMSTR(service->type),YMSTR(service->name),service->port);
    YMmDNSTxtRecordKeyPair **keyPairs = service->txtRecordKeyPairs;
    size_t keyPairsSize = service->txtRecordKeyPairsSize;
    
    _CompareTxtList(theTest,keyPairs,keyPairsSize,theTest->testKeyPairs,theTest->nTestKeyPairs);
    
    theTest->waitingOnResolution = false;
    YMmDNSServiceStop(theTest->service, false);
}

void test_service_removed(YMmDNSBrowserRef browser, YMStringRef serviceName, void *context)
{
    struct mDNSTest *theTest = context;
    testassert(theTest,"resolved callback context");
    testassert(theTest->browser==browser,"browser pointers %p != %p",theTest->browser,browser);
    
    ymlog("%s/%s:? disappeared",testServiceType,YMSTR(serviceName));
    
    if ( theTest->waitingOnAppearance || theTest->waitingOnResolution )
        testassert(strcmp(YMSTR(serviceName),theTest->testServiceName),"test service disappeared before tearDown")
    else
    {
        ymlog("target service removed");
        theTest->waitingOnDisappearance = false;
    }
}

YM_EXTERN_C_POP
