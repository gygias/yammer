//
//  GrabBagTests.c
//  yammer
//
//  Created by david on 3/14/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#include "GrabBagTests.h"

#include "YMUtilities.h"

YM_EXTERN_C_PUSH

typedef struct GrabBagTest
{
    ym_test_assert_func assert;
    const void *context;
} GrabBagTest;

void _InterfacesTestRun(struct GrabBagTest *theTest);

void GrabBagTestsRun(ym_test_assert_func assert, const void *context)
{
    struct GrabBagTest theTest = { assert, context };
    
    _InterfacesTestRun(&theTest);
    ymerr(" _X509GenerationTestRun completed");
}

void _InterfacesTestRun(struct GrabBagTest *theTest)
{    
    YMDictionaryRef map = YMInterfaceMapCreateLocal();
    testassert(YMDictionaryGetCount(map)>0, "interface map is empty");
    
    YMDictionaryEnumRef denum = YMDictionaryEnumeratorBegin(map);
    testassert(denum, "interface map is not empty but enumerator is null");
    testassert(YMDictionaryGetCount(denum->value),"ifInfo dict is empty");
    while ( denum ) {
        YMArrayRef someAddresses = YMDictionaryGetItem(denum->value, kYMIFMapAddressesKey);
        testassert(someAddresses&&YMArrayGetCount(someAddresses)>0, "interface '%s' has no addresses",YMSTR((YMStringRef)denum->key));
        denum = YMDictionaryEnumeratorGetNext(denum);
    }
    YMDictionaryEnumeratorEnd(denum);
    
    YMRelease(map);
}

YM_EXTERN_C_POP

