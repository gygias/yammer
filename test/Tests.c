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

#include "GrabBagTests.h"
#include "CompressionTests.h"
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
#include "YMTask.h"
#include "YMUtilities.h"

#include <stdarg.h>

int gFailures = 0;
YMLockRef gLock = NULL;

void _ym_test_assert_func(__unused const void *ctx, bool exp, const char *fmt, ...)
{
    if (!exp) {
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);


		fprintf(stderr, "\n");
		fflush(stderr);

#define HARD
#ifdef HARD
		ymabort("something happened");
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

#if defined(YMAPPLE)
# warning is apple's diff in /usr/bin?
# define SomeDiffPath "/usr/bin/diff"
#elif defined(YMLINUX)
# define SomeDiffPath "/usr/bin/diff"
#elif defined(YMWIN32)
# error implement me
#endif

    YMArrayRef args = YMArrayCreate();
    if ( recursive )
        YMArrayAdd(args,"-r");
    YMArrayAdd(args,path1);
    YMArrayAdd(args,path2);
    YMStringRef diffPath = YMSTRC(SomeDiffPath);
    YMTaskRef diff = YMTaskCreate(diffPath,args,false);
    YMTaskLaunch(diff);
    YMTaskWait(diff);
    bool okay = YMTaskGetExitStatus(diff) == 0;

    YMRelease(diffPath);
    YMRelease(args);
    YMRelease(diff);

	return okay;
}

void RunAllTests()
{
    gLock = YMLockCreate();
    
    ymerr("------ misc tests start ------");
    GrabBagTestsRun(_ym_test_assert_func, NULL);
    ymerr("------ misc tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
    ymerr("------ task tests start ------");
    TaskTestsRun(_ym_test_assert_func, NULL);
    ymerr("------ task tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
    ymerr("------ thread tests start ------");
    ThreadTestsRun(_ym_test_assert_func, NULL);
    ymerr("------ thread tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
    ymerr("------ compression tests start ------");
    CompressionTestsRun(_ym_test_assert_func, NULL);
    ymerr("------ compression tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
	ymerr("------ dictionary tests start ------");
	DictionaryTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ dictionary tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
	ymerr("------ crypto tests start ------");
	CryptoTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ crypto tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
	ymerr("------ local socket pair tests start ------");
	LocalSocketPairTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ local socket pair tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
	ymerr("------ mdns tests start ------");
	mDNSTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ mdns tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
	ymerr("------ tls tests start ------");
	TLSTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ tls tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
	ymerr("------ plexer tests start ------");
	PlexerTestsRun(_ym_test_assert_func, NULL);
	ymerr("------ plexer tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
	ymerr("------ session tests start ------");
	SessionTestsRun(_ym_test_assert_func, _ym_test_diff_func, NULL);
	ymerr("------ session tests end ------");
    ymlog("there are %d threads in the current process",YMGetNumberOfThreadsInCurrentProcess());
}

char *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, bool for_mDNSServiceName, bool for_txtKey)
{
    uint8_t randomLength = (uint8_t)arc4random_uniform(maxLength + 1 + 1);
    if ( randomLength < 2 ) randomLength = 2;
    
    char *string = calloc(1,randomLength);
    string[--randomLength] = '\0';
    
    uint8_t maxChar = for_mDNSServiceName ? 'z' : 0x7E, minChar = for_mDNSServiceName ? 'a' : 0x20;
    uint8_t range = maxChar - minChar;    
    while ( randomLength-- ) {
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
    uint8_t *randomData = calloc(1,randomLength);
    
    uint16_t countdown = randomLength;
    while ( countdown-- ) {
        uint8_t aByte = (uint8_t)arc4random_uniform(0x100);
        randomData[countdown] = aByte;
    }
    
    if ( outLength )
        *outLength = randomLength;
    
    return randomData;
}

YM_EXTERN_C_POP
