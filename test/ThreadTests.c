//
//  ThreadTests.c
//  yammer
//
//  Created by david on 12/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "ThreadTests.h"

#include "YMTask.h"
#include "YMUtilities.h"
#include "YMLog.h"

struct ThreadTest {
    ym_test_assert_func assert;
    const void *context;
} ThreadTest;

void _ThreadDispatchMainTestRun(struct ThreadTest *theTest);

void ThreadTestsRun(ym_test_assert_func assert, const void *context)
{
    struct ThreadTest theTest = { assert, context };
    
    _ThreadDispatchMainTestRun(&theTest);
}

void _ThreadDispatchMainTestRun(struct ThreadTest *theTest)
{
    YMArrayRef options = YMArrayCreate();
    YMArrayAdd(options,"m");
    YMArrayAdd(options,"g");
    YMArrayAdd(options,"gr");
    YMArrayAdd(options,"u");
    YMArrayAdd(options,"dos");
    YMArrayAdd(options,"after");
#define begin_argless_tests_index 4
#define usually_races_index 2
    for ( int i = 0; i < YMArrayGetCount(options); i++ ) {
        YMStringRef path = YMSTRC("ym-dispatch-test");
        YMArrayRef args = YMArrayCreate();
        YMArrayAdd(args, YMArrayGet(options,i));
        if ( i < begin_argless_tests_index )
            YMArrayAdd(args, "1000000");
        else if ( i == 5 ) //after
            YMArrayAdd(args, "20");

        printf("fork: %s",YMSTR(path));
        for( int j = 0; j < YMArrayGetCount(args); j++ )
            printf(" %s",(const char *)YMArrayGet(args,j));
        printf("\n");

        YMTaskRef testTask = YMTaskCreate(path, args, false);
        YMTaskLaunch(testTask);
        YMTaskWait(testTask);
        int result = YMTaskGetExitStatus(testTask);
        testassert(result==0||i==usually_races_index,"ym-dispatch-test result %d",result);

        YMRelease(testTask);
        YMRelease(args);
        YMRelease(path);

    }
}
