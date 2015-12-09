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
#if defined(YMMACOS)
# define SomeTruePath "/usr/bin/true"
#elif defined(YMLINUX)
# define SomeTruePath "/bin/true"
#elif defined(YMWIN32)
# define SomeTruePath "c:\\Windows\\System32\\getmac.exe"
#else
# error implement me
#endif
    
    YMStringRef path = YMSTRC(SomeTruePath);
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
    testassert(!output&&len==0,"true output exists");
    
    YMRelease(path);
    YMRelease(task);
}

void _TaskCatSomeLogRun(struct TaskTest *theTest)
{
#if defined(YMMACOS) || defined(YMLINUX)
# define SomeCatPath "/bin/cat"
# if defined(YMMACOS)
#  define SomeLogPath "/var/log/install.log"
# elif defined(YMLINUX)
#  define SomeLogPath "/var/log/syslog"
# endif
#elif defined(YMWIN32)
# define SomeCatPath "c:\\Windows\\System32\\cmd"
# define SomeLogPath "/c \"type c:\\Windows\\WindowsUpdate.log\""
#else
# error implement me
#endif

    YMStringRef path = YMSTRC(SomeCatPath);
    YMArrayRef args = YMArrayCreate();
    YMArrayAdd(args, SomeLogPath);
    
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
    
    ymlog(">>>>> %s output:\n%s\n<<<<< %s output",YMSTR(path),output,YMSTR(path));
    
    YMRelease(args);
    YMRelease(path);
    YMRelease(task);
}

void _TaskOpensslRun(struct TaskTest *theTest)
{
	YMArrayRef args = YMArrayCreate();

#if !defined(YMWIN32)
    YMStringRef path = YMSTRC("/usr/bin/openssl");
    YMArrayAdd(args, "genrsa");
    YMArrayAdd(args, "4096");
#else
	YMStringRef path = YMSTRC("c:\\Windows\\System32\\find.exe");
	YMArrayAdd(args, "/I");
	YMArrayAdd(args, "\"dll\"");
	YMArrayAdd(args, ".\\*");
#endif
    
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
    
    ymlog(">>>>> %s output:\n%s\n<<<<< %s output",YMSTR(path),output,YMSTR(path));
    
    YMRelease(args);
    YMRelease(path);
    YMRelease(task);
}
