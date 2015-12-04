// tests.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "Tests.h"
#include "DictionaryTests.h"
#include "CryptoTests.h"
#include "LocalSocketPairTests.h"
#include "mDNSTests.h"
#include "TLSTests.h"
#include "PlexerTests.h"
#include "SessionTests.h"

#include "YMLock.h"

#include <stdarg.h>

int gFailures = 0;
YMLockRef gLock = NULL;

void _ym_test_assert_func(const void *ctx, bool exp, const char *fmt, ...)
{
	if (!exp)
	{
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);


		fprintf(stderr, "\n");
		fflush(stderr);

#ifdef HARD
		ymabort("yo");
#else
		YMLockLock(gLock);
		gFailures++;
		YMLockUnlock(gLock);
#endif
	}

}

bool _ym_test_diff_func(const void *ctx, const char *path1, const char *path2, bool recursive, YMDictionaryRef exceptions)
{
	ymerr("*** win32 diff ***");
	return true;
}


int main()
{
	gLock = YMLockCreate();

	DictionaryTestRun(_ym_test_assert_func, NULL);
	CryptoTestRun(_ym_test_assert_func, NULL);
	LocalSocketPairTestRun(_ym_test_assert_func, NULL);
	mDNSTestRun(_ym_test_assert_func, NULL);
	TLSTestRun(_ym_test_assert_func, NULL);
	PlexerTestRun(_ym_test_assert_func, NULL);
	SessionTestRun(_ym_test_assert_func, _ym_test_diff_func, NULL);

    return 0;
}

