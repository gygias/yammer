//
//  ThreadTests.c
//  yammer
//
//  Created by david on 12/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "ThreadTests.h"

#include "YMTask.h"

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
#warning magic is forbidden
#define magic_index 4
#define black_magic_index 2
    for ( int i = 0; i < YMArrayGetCount(options); i++ ) {
        YMStringRef path = YMSTRC("ym-dispatch-main-test");
        YMArrayRef args = YMArrayCreate();
        YMArrayAdd(args, YMArrayGet(options,i));
        if ( i != magic_index )
            YMArrayAdd(args, "1000000");

        YMTaskRef testTask = YMTaskCreate(path, args, false);
        YMTaskLaunch(testTask);
        YMTaskWait(testTask);
        int result = YMTaskGetExitStatus(testTask);
        testassert(i==black_magic_index?result!=0:result==0,"ym-dispatch-main-test result %d",result);

        YMRelease(testTask);
        YMRelease(args);
        YMRelease(path);

    }
}
