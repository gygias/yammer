//
//  TaskTests.c
//  yammer
//
//  Created by david on 12/8/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "TaskTests.h"

#include "YMTask.h"

#define ymlog_type YMLogDefault
#include "YMLog.h"

typedef struct TaskTest
{
    ym_test_assert_func assert;
    const void *context;
} TaskTest;

void _TaskUsrBinTrueRun(struct TaskTest *theTest);
void _TaskCatSomeLogRun(struct TaskTest *theTest);
void _TaskOpensslRun(struct TaskTest *theTest);

void TaskTestRun(ym_test_assert_func assert, const void *context)
{
    struct TaskTest theTest = { assert, context };
    
    _TaskUsrBinTrueRun(&theTest);
    ymerr(" _TaskUsrBinTrueRun completed");
    _TaskCatSomeLogRun(&theTest);
    ymerr(" _TaskCatSomeLogRun completed");
    _TaskOpensslRun(&theTest);
    ymerr(" _TaskOpensslRun completed");
}

void _TaskUsrBinTrueRun(struct TaskTest *theTest)
{
    YMStringRef path = YMSTRC("/usr/bin/true");
    YMArrayRef args = NULL;
    
    YMTaskRef task = YMTaskCreate(path, args, false);
    testassert(task, "create true task");
    bool okay = YMTaskLaunch(task);
    testassert(okay, "launch true task");
    YMTaskWait(task);
    int result = YMTaskGetExitStatus(task);
    testassert(result==0,"task true result");
    
    uint32_t len = 0;
    unsigned char *output = YMTaskGetOutput(task, &len);
    testassert(!output&&len==0,"true output");
    
    YMRelease(path);
    YMRelease(task);
}

void _TaskCatSomeLogRun(struct TaskTest *theTest)
{
    YMStringRef path = YMSTRC("/bin/cat");
    YMArrayRef args = YMArrayCreate();
    YMArrayAdd(args, "/var/log/install.log");
    
    YMTaskRef task = YMTaskCreate(path, args, true);
    testassert(task, "create task");
    bool okay = YMTaskLaunch(task);
    testassert(okay, "launch task");
    YMTaskWait(task);
    int result = YMTaskGetExitStatus(task);
    testassert(result==0,"task result");
    
    uint32_t len = 0;
    unsigned char *output = YMTaskGetOutput(task, &len);
    testassert(output&&len>0,"output");
    
    ymlog("%s",output);
    
    YMRelease(args);
    YMRelease(path);
    YMRelease(task);
}

void _TaskOpensslRun(struct TaskTest *theTest)
{
    YMStringRef path = YMSTRC("/usr/bin/openssl");
    YMArrayRef args = YMArrayCreate();
    YMArrayAdd(args, "genrsa");
    YMArrayAdd(args, "4096");
    
    YMTaskRef task = YMTaskCreate(path, args, true);
    testassert(task, "create openssl task");
    bool okay = YMTaskLaunch(task);
    testassert(okay, "launch openssl task");
    YMTaskWait(task);
    int result = YMTaskGetExitStatus(task);
    testassert(result==0,"task openssl result");
    
    uint32_t len = 0;
    unsigned char *output = YMTaskGetOutput(task, &len);
    testassert(output&&len>0,"openssl output");
    
    ymlog("%s",output);
    
    YMRelease(args);
    YMRelease(path);
    YMRelease(task);
}
