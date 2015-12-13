//
//  Tests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "Tests.h"

#ifdef YMWIN32
#include "arc4random.h"
#endif

YM_EXTERN_C_PUSH

#include "TaskTests.h"
#include "ThreadTests.h"
#include "CryptoTests.h"
#include "DictionaryTests.h"
#include "LocalSocketPairTests.h"
#include "TLSTests.h"
#include "mDNSTests.h"
#include "PlexerTests.h"
#include "SessionTests.h"

#include "YMLock.h"

#include <stdarg.h>

int gFailures = 0;
YMLockRef gLock = NULL;

void _ym_test_assert_func(__unused const void *ctx, bool exp, const char *fmt, ...)
{
	if (!exp)
	{
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);


		fprintf(stderr, "\n");
		fflush(stderr);

#define HARD
#ifdef HARD
		ymabort("yo");
#else
		YMLockLock(gLock);
		gFailures++;
		YMLockUnlock(gLock);
#endif
	}
}

bool _ym_test_diff_func(__unused const void *ctx, __unused const char *path1, __unused const char *path2, __unused bool recursive, __unused YMDictionaryRef exceptions)
{
	ymerr("*** diff ***");
	return true;
}

void RunAllTests()
{
    gLock = YMLockCreate();
    
    ymerr("------ task tests start ------");
    TaskTestsRun(_ym_test_assert_func, NULL);
    ymerr("------ task tests end ------");
    ymerr("------ thread tests start ------");
    ThreadTestsRun(_ym_test_assert_func, NULL);
    ymerr("------ thread tests end ------");
	ymerr("------ dictionary tests start ------");
	DictionaryTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ dictionary tests end ------");
	ymerr("------ crypto tests start ------");
	CryptoTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ crypto tests end ------");
	ymerr("------ local socket pair tests start ------");
	LocalSocketPairTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ local socket pair tests end ------");
	ymerr("------ mdns tests start ------");
	mDNSTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ mdns tests end ------");
	ymerr("------ tls tests start ------");
	TLSTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ tls tests end ------");
	ymerr("------ plexer tests start ------");
	PlexerTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ plexer tests end ------");
	ymerr("------ session tests start ------");
	SessionTestsRun(_ym_test_assert_func, _ym_test_diff_func, NULL);
	ymerr("------ session tests end ------");
}

char *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, bool for_mDNSServiceName, bool for_txtKey)
{
    uint8_t randomLength = (uint8_t)arc4random_uniform(maxLength + 1 + 1);
    if ( randomLength < 2 ) randomLength = 2;
    
    char *string = malloc(randomLength);
    string[--randomLength] = '\0';
    
    uint8_t maxChar = for_mDNSServiceName ? 'z' : 0x7E, minChar = for_mDNSServiceName ? 'a' : 0x20;
    uint8_t range = maxChar - minChar;    
    while ( randomLength-- )
    {
        char aChar;
        do {
            aChar = (char)arc4random_uniform(range + 1) + minChar;
        } while ( for_txtKey && (aChar == '='));
        
        string[randomLength] = aChar;
    }
    
    return string;
}

uint8_t *YMRandomDataWithMaxLength(uint16_t length, uint16_t *outLength)
{
    uint16_t randomLength = (uint16_t)arc4random_uniform(length+1);
    if ( randomLength == 0 ) randomLength = 1;
    uint8_t *randomData = malloc(randomLength);
    
    uint16_t countdown = randomLength;
    while ( countdown-- )
    {
        uint8_t aByte = (uint8_t)arc4random_uniform(0x100);
        randomData[countdown] = aByte;
    }
    
    if ( outLength )
        *outLength = randomLength;
    
    return randomData;
}

YM_EXTERN_C_POP
