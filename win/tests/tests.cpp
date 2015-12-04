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

bool _ym_test_diff_func(const void *ctx, const char *path1, const char *path2, bool recursive, YMDictionaryRef exceptions)
{
	ymerr("*** win32 diff ***");
	return true;
}


int main()
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

    return 0;
}

