//
//  Tests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "Tests.h"

#ifdef WIN32
#include "arc4random.h"
#endif

YM_EXTERN_C_PUSH

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

#if defined(RPI) || defined(MACOS_TEST_TOOL)

int main( __unused int argc, __unused const char *argv[] )
{
    bool indefinite = argc > 1;
    
    do {
        RunAllTests();
    } while (indefinite);
    
    return 0;
}

#endif

void RunAllTests()
{
	gLock = YMLockCreate();

	ymerr("------ dictionary test start ------");
	DictionaryTestRun(_ym_test_assert_func, NULL);
	ymerr("------ dictionary test end ------");
	ymerr("------ crypto test start ------");
	CryptoTestRun(_ym_test_assert_func, NULL);
	ymerr("------ crypto test end ------");
	ymerr("------ local socket pair test start ------");
	LocalSocketPairTestRun(_ym_test_assert_func, NULL);
	ymerr("------ local socket pair test end ------");
	ymerr("------ mdns test start ------");
	mDNSTestRun(_ym_test_assert_func, NULL);
	ymerr("------ mdns test end ------");
	ymerr("------ tls test start ------");
	TLSTestRun(_ym_test_assert_func, NULL);
	ymerr("------ tls test end ------");
	ymerr("------ plexer test start ------");
	PlexerTestRun(_ym_test_assert_func, NULL);
	ymerr("------ plexer test end ------");
	ymerr("------ session test start ------");
	SessionTestRun(_ym_test_assert_func, _ym_test_diff_func, NULL);
	ymerr("------ session test end ------");
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
