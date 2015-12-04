#include "stdafx.h"
#include "CppUnitTest.h"

#include "Tests.h"
#include "DictionaryTests.h"
#include "CryptoTests.h"
#include "LocalSocketPairTests.h"
#include "mDNSTests.h"
#include "TLSTests.h"
#include "PlexerTests.h"
#include "SessionTests.h"

#include <stdarg.h>

//using namespace Microsoft::VisualStudio::CppUnitTestFramework;

void _ym_test_assert_func(const void *ctx, bool exp, const char *fmt, ...)
{
	if ( ! exp )
	{
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);


		fprintf(stderr, "\n");
		fflush(stderr);

		ymabort("yo");
	}
}

bool _ym_test_diff_func(const void *ctx, const char *path1, const char *path2, bool recursive, YMDictionaryRef exceptions)
{
	ymerr("*** win32 diff ***");
	return true;
}

namespace WinTests
{		
	class UnitTest1 //TEST_CLASS(UnitTest1)
	{
	public:
		
		void TestMethod1() //TEST_METHOD(TestMethod1)
		{
			DictionaryTestRun(_ym_test_assert_func, this);
			CryptoTestRun(_ym_test_assert_func, this);
			LocalSocketPairTestRun(_ym_test_assert_func, this);
			mDNSTestRun(_ym_test_assert_func, this);
			TLSTestRun(_ym_test_assert_func, this);
			PlexerTestRun(_ym_test_assert_func, this);
			SessionTestRun(_ym_test_assert_func, _ym_test_diff_func, this);
		}
	};
}

// "Native Unit Test" template gives 0x800700c1 "Unable to start program" WinTests.dll...
// EDIT upon converting to exe, i'm told my computer doesn't have Microsoft.VisualStudio.CppUnitTestFramework.dll, try reinstalling
// NOTE unit test framework needs unit tests
// EDIT upon googling, need to add a reference. not sure what that means in a pure win32 project
// EDIT fuck it it's just an exe now
int CALLBACK WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nCmdShow
	)
{
	WinTests::UnitTest1 theTest;
	theTest.TestMethod1();
}