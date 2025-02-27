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
#include "YMDispatch.h"

#include <stdarg.h>

#ifndef YMWIN32
# define myexit _Exit
#else
# define myexit exit
#endif

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

#if defined(YMLINUX) || defined(YMAPPLE)
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

typedef void (*TestFunc)(ym_test_assert_func, const void *);
typedef struct TestDesc
{
    char *name;
    TestFunc func;
    void *ctx;
} TestDesc;
typedef struct TestDesc ATest;

YM_ENTRY_POINT(run_all_tests)
{
    gLock = YMLockCreate();
    
    ATest tests[] = {
        { "misc", GrabBagTestsRun, NULL },
        { "compression", CompressionTestsRun, NULL },
        { "task", TaskTestsRun, NULL },
        { "thread", ThreadTestsRun, NULL },
        { "dictionary", DictionaryTestsRun, NULL },
        { "crypto", CryptoTestsRun, NULL },
        { "local socket pair", LocalSocketPairTestsRun, NULL },
        { "mdns", mDNSTestsRun, NULL },
        { "tls", TLSTestsRun, NULL },
        { "plexer", PlexerTestsRun, NULL },
        { "session", SessionTestsRun, (void *)_ym_test_diff_func }
    };

    for ( int i = 0; i < 11; i++ ) {
        ymerr("------ %s tests start ------",tests[i].name);
        tests[i].func(_ym_test_assert_func, tests[i].ctx);
        ymerr("------ %s tests end ------",tests[i].name);
        ymlog("there are %d open files and %d threads in the current process",YMGetNumberOfOpenFilesForCurrentProcess(),YMGetNumberOfThreadsInCurrentProcess());
        
    }
    
    myexit(0);
}

void RunAllTests(void)
{
    ym_dispatch_user_t user = { run_all_tests, NULL, NULL, ym_dispatch_user_context_noop };
    YMDispatchAsync(YMDispatchGetGlobalQueue(), &user);
    
    YMDispatchMain();
}

YM_EXTERN_C_POP
