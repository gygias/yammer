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
#include "YMUtilities.h"

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
#define testKeyPairReserved ( 2 ) // length byte, '='
#define testKeyMaxLen ( UINT8_MAX - testKeyPairReserved )

#if 0 // actually debugging
#define testTimeout (10 * 60)
#else
#define testTimeout 60
#endif

void _TestmDNSTxtRecordParsing(struct mDNSTest *theTest);
void _TestmDNSCreateDiscoverResolve(struct mDNSTest *theTest);

void test_service_appeared(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context);
void test_service_updated(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context);
void test_service_resolved(YMmDNSBrowserRef browser, bool resolved, YMmDNSServiceRecord *service, void *context);
void test_service_removed(YMmDNSBrowserRef browser, YMStringRef serviceName, void *context);

YMmDNSTxtRecordKeyPair ** _MakeTxtRecordKeyPairs(uint16_t *inOutnKeypairs);
void _CompareTxtList(struct mDNSTest *theTest, YMmDNSTxtRecordKeyPair **aList, size_t aSize, YMmDNSTxtRecordKeyPair **bList, size_t bSize);

void mDNSTestsRun(ym_test_assert_func assert, const void *context)
{
    struct mDNSTest theTest = { assert, context, NULL, NULL, 0, NULL, NULL, true, true, true };
    
    _TestmDNSTxtRecordParsing(&theTest);
    ymerr(" _TestmDNSTxtRecordParsing completed");
    _TestmDNSCreateDiscoverResolve(&theTest);
    ymerr(" _TestmDNSCreateDiscoverResolve completed");
    
    free(theTest.testServiceName);
    if ( theTest.testKeyPairs )
        _YMmDNSTxtKeyPairsFree(theTest.testKeyPairs, theTest.nTestKeyPairs);
}

void _TestmDNSTxtRecordParsing(struct mDNSTest *theTest)
{
    for(int i = 0; i < 1000; i++) {
        uint16_t desiredAndActualSize = (uint16_t)arc4random_uniform(3);
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
    theTest->testServiceName = calloc(1,mDNS_SERVICE_NAME_LENGTH_MAX + 1);
    YMRandomASCIIStringWithLength(theTest->testServiceName,mDNS_SERVICE_NAME_LENGTH_MAX, true, false);
    YMStringRef serviceType = YMSTRC(testServiceType);
    YMStringRef serviceName = YMSTRC(theTest->testServiceName);
    theTest->service = YMmDNSServiceCreate(serviceType, serviceName, 5050); // todo making our own bogus name
    testassert(theTest->service,"YMmDNSServiceCreate");
    YMRelease(serviceName);
    
    theTest->nTestKeyPairs = (uint16_t)arc4random_uniform(10);
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
    
    for ( int i = 0; i < nSteps; i++ ) {
        const char *name = (const char *)steps[i][0];
        bool *flag = (bool *)steps[i][1];
     
        time_t startTime = time(NULL);
        while ( *flag ) {
            if ( ( time(NULL) - startTime ) >= testTimeout ) {
                testassert(false, "timed out waiting for %s",name);
                return;
            }
	
			sleep(1);
        }
        ymlog("%s happened",name);
    }
    
    
    okay = YMmDNSBrowserStop(theTest->browser);
    YMRelease(theTest->browser);
    theTest->browser = NULL;
    YMRelease(theTest->service);
    theTest->service = NULL;
}

YMmDNSTxtRecordKeyPair ** _MakeTxtRecordKeyPairs(uint16_t *inOutnKeypairs)
{
    size_t requestedSize = *inOutnKeypairs;
    size_t actualSize = 0;
    size_t debugBlobSize = 0;
    
    if ( requestedSize == 0 )
        return NULL;
    
    YMmDNSTxtRecordKeyPair **keyPairs = (YMmDNSTxtRecordKeyPair **)calloc(*inOutnKeypairs,sizeof(YMmDNSTxtRecordKeyPair *));
    
    // drafts/specs/implementations seem to converge toward txt record /should/ be 1300 bytes or shorter, wrt 1500 ethernet mtu
    // avahi seems to enforce a hard limit around 1300
    int remaining = 1200; //UINT16_MAX;
    for ( size_t idx = 0; idx < requestedSize; idx++ ) {
        keyPairs[idx] = calloc(1,sizeof(YMmDNSTxtRecordKeyPair));
        
        remaining -= testKeyPairReserved; // length char, '='
        
        // The "Name" MUST be at least one character. Strings beginning with an '=' character (i.e. the name is missing) SHOULD be silently ignored.
        uint8_t aKeyLenMax = (uint8_t)(( testKeyMaxLen > remaining ) ? ( remaining - testKeyPairReserved ) : testKeyMaxLen);
        char randomKey[testKeyMaxLen];
        YMRandomASCIIStringWithLength(randomKey,aKeyLenMax, false, true);
        keyPairs[idx]->key = YMSTRC(randomKey);//"test-key";
        
        size_t keyLen = strlen(randomKey);
        remaining -= (uint16_t)keyLen;
        
        // as far as i can tell, value can be empty
        uint8_t valueLenMax = (uint8_t)( UINT8_MAX - keyLen - testKeyPairReserved );
        uint8_t aValueLen = ( valueLenMax > remaining ) ? (uint8_t)remaining : valueLenMax;
        uint8_t *value_data = calloc(1,aValueLen);
        YMRandomDataWithLength((uint8_t *)value_data, aValueLen);
        keyPairs[idx]->value = value_data;
        keyPairs[idx]->valueLen = (uint8_t)aValueLen;
        
        remaining -= aValueLen;
        
        actualSize++;
        debugBlobSize += ( testKeyPairReserved + keyLen + aValueLen );
        ymdbg("aKeyPair[%zd]: 3 + [%u] => [%zu]... [%lu] (%d)",idx,aValueLen,keyLen,debugBlobSize,remaining);
        
        if ( remaining < 0 )
            ymlog("warning: txt record format remaining %d",remaining);
        if ( remaining <= testKeyPairReserved )
            break;
    }
    
    ymdbg("made txt record length %zu out of requested %zu (blob size %zu)\n",actualSize,requestedSize,debugBlobSize);
    *inOutnKeypairs = (uint16_t)actualSize;
    
    return keyPairs;
}

void _CompareTxtList(struct mDNSTest *theTest, YMmDNSTxtRecordKeyPair **aList, size_t aSize, YMmDNSTxtRecordKeyPair **bList, size_t bSize)
{
    if ( aSize != bSize ) {
        for ( size_t i = 0; i < theTest->nTestKeyPairs; i++ ) {
            if ( i < aSize ) {
                YMmDNSTxtRecordKeyPair *aPair = aList[i];
                ymlog("a [%zd]: %zd -> %d (%s)",i,YMStringGetLength(aPair->key),aPair->valueLen,YMSTR(aPair->key));
            }
            if ( i < bSize ) {
                YMmDNSTxtRecordKeyPair *bPair = bList[i];
                ymlog("b [%zd]: %zd -> %d (%s)",i,YMStringGetLength(bPair->key),bPair->valueLen,YMSTR(bPair->key));
            }
        }
    }
    
    testassert(aSize==bSize,"sizes don't match"); // todo still happens... documentation
    
    if ( aList == NULL && bList == NULL ) // i guess
        return;
    
    testassert((uintptr_t)aList ^ (uintptr_t)bList,"null list vs non-null list");
    
    for ( size_t i = 0; i < aSize; i++ ) {
        testassert(aList[i]->key,"(a) key %zdth null (%p %p)",i,aList[i]->key,bList[i]->key);

        // this happened to work once, list preserved order for whatever reason, no more on ubuntu 24.04 / libavahi-core7/noble,now 0.8-13ubuntu6 amd64
        //testassert(0 == strcmp(YMSTR(aList[i]->key), YMSTR(bList[i]->key)),"%zd-th keys '%s' and '%s' don't match (%d)",i,YMSTR(aList[i]->key),YMSTR(bList[i]->key),aSize);
        ssize_t j = 0;
        for ( ; j <= (ssize_t)bSize; j++ ) {
            if ( j == (ssize_t)bSize ) {
                j = -1;
                break;
            }

            testassert(bList[j]->key,"(b) key %zdth null",j);
            if ( 0 == strcmp(YMSTR(aList[i]->key), YMSTR(bList[j]->key)) )
                break;
        }
        testassert(j>=0,"couldn't find corresponding b key-value for '%s'",YMSTR(aList[i]->key));
        testassert(aList[i]->value&&aList[i]->value,"a value %zdth null",i);
        testassert(aList[i]->valueLen == bList[j]->valueLen,"%zd-th values have different lengths of %u and %u",i,aList[i]->valueLen,bList[j]->valueLen);
        testassert(0 == memcmp(aList[i]->value, bList[j]->value, aList[i]->valueLen),"%zu-th values of length %u don't match",i,aList[i]->valueLen);
    }
}

void test_service_appeared(YMmDNSBrowserRef browser, YMmDNSServiceRecord *service, void *context)
{
    struct mDNSTest *theTest = context;
    testassert(theTest,"appeared callback context");
    
    ymlog("%s/%s:? appeared",YMSTR(service->type),YMSTR(service->name));
    testassert(browser==theTest->browser,"browser pointers are not equal on service appearance");
    if ( theTest->waitingOnAppearance && 0 == strcmp(YMSTR(service->name), theTest->testServiceName) ) {
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
    _CompareTxtList(theTest,service->txtRecordKeyPairs,service->txtRecordKeyPairsSize,theTest->testKeyPairs,theTest->nTestKeyPairs);
    
    theTest->waitingOnResolution = false;
    YMmDNSServiceStop(theTest->service);
}

void test_service_removed(YMmDNSBrowserRef browser, YMStringRef serviceName, void *context)
{
    struct mDNSTest *theTest = context;
    testassert(theTest,"resolved callback context");
    testassert(theTest->browser==browser,"browser pointers %p != %p",theTest->browser,browser);
    
    ymlog("%s/%s:? disappeared",testServiceType,YMSTR(serviceName));
    
    if ( theTest->waitingOnAppearance || theTest->waitingOnResolution )
        testassert(strcmp(YMSTR(serviceName),theTest->testServiceName),"test service disappeared before tearDown")
    else {
        ymlog("target service removed");
        theTest->waitingOnDisappearance = false;
    }
}

YM_EXTERN_C_POP
