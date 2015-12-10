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
    YMStringRef path = YMSTRC("ym-dispatch-main-test");
    YMArrayRef args = YMArrayCreate();
    YMArrayAdd(args, "m");
    YMArrayAdd(args, "10000");
    
    YMTaskRef testTask = YMTaskCreate(path, args, false);
    YMTaskLaunch(testTask);
    YMTaskWait(testTask);
    int result = YMTaskGetExitStatus(testTask);
    testassert(result==0,"ym-dispatch-main-test result %d",result);
    
    YMRelease(testTask);
    YMRelease(args);
    YMRelease(path);
}
