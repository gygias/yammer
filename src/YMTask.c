//
//  YMTask.c
//  yammer
//
//  Created by david on 12/8/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMTask.h"

#include "YMPipePriv.h"
#include "YMThread.h"

#define ymlog_type YMLogDefault
#include "YMLog.h"

typedef struct __ym_task_t
{
    _YMType _type;
    
    YMStringRef path;
    YMArrayRef args;
    bool save;
    
    pid_t childPid;
    bool exited;
    int result;
    unsigned char *output;
    uint32_t outputLen;
    YMPipeRef outputPipe;
    YMThreadRef outputThread;
} __ym_task_t;
typedef struct __ym_task_t *__YMTaskRef;

#define NULL_PID (-1)

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_task_read_output_proc(YM_THREAD_PARAM ctx_);

YMTaskRef YMTaskCreate(YMStringRef path, YMArrayRef args, bool saveOutput)
{
    __YMTaskRef task = (__YMTaskRef)_YMAlloc(_YMTaskTypeID, sizeof(struct __ym_task_t));
    task->path = YMRetain(path);
    task->args = args ? YMRetain(args) : NULL;
    task->save = saveOutput;
    
    task->childPid = NULL_PID;
    task->exited = false;
    task->output = NULL;
    task->outputPipe = NULL;
    task->outputThread = NULL;
    task->outputLen = 0;
    task->result = -1;
    return task;
}

void _YMTaskFree(YMTaskRef task_)
{
    __YMTaskRef task = (__YMTaskRef)task_;
    YMRelease(task->path);
    if ( task->args )
        YMRelease(task->args);
    if ( task->output )
        free(task->output);
}

bool YMTaskLaunch(YMTaskRef task_)
{
    __YMTaskRef task = (__YMTaskRef)task_;
    
    YM_IO_BOILERPLATE
    
    if ( task->save )
    {
        task->outputPipe = YMPipeCreate(NULL);
        ymlog("task[%s]: output pipe %d -> %d",YMSTR(task->path),YMPipeGetInputFile(task->outputPipe),YMPipeGetOutputFile(task->outputPipe));
        task->outputThread = YMThreadCreate(task->path, __ym_task_read_output_proc, YMRetain(task));
        YMThreadStart(task->outputThread);
    }
    
    pid_t pid = fork();
    if ( pid == 0 ) // child
    {
        if ( task->save )
        {
            int pipeIn = YMPipeGetInputFile(task->outputPipe);
            result = dup2(pipeIn, STDOUT_FILENO);
            ymassert(result!=-1,"task[%s]: dup2(%d<-%d) failed: %d %s",YMSTR(task->path),pipeIn,STDOUT_FILENO,errno,strerror(errno));
            YMRelease(task->outputPipe); // closes in and out
        }
        
        int64_t argsSize = task->args ? YMArrayGetCount(task->args) : 0;
        ymerr("task[%s]: execv %lld args...",YMSTR(task->path),argsSize);
        
        const char **args = malloc(argsSize*sizeof(char *) + 1);
        args[0] = YMSTR(task->path);
        for(int64_t i = 1; i < argsSize + 1; i++) {
            args[i] = YMArrayGet(task->args, i - 1);
            ymerr("arg[%lld]: %s",i,args[i]);
        }
        execv(args[0], (char * const *)args);
        ymerr("task[%s]: execv failed: %d %s",YMSTR(task->path),errno,strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    ymlog("task[%s]: forked: %d",YMSTR(task->path) ,pid);
    task->childPid = pid;
    
    return true;
}
void YMTaskWait(YMTaskRef task_)
{
    __YMTaskRef task = (__YMTaskRef)task_;
    ymassert(task->childPid!=NULL_PID,"task[%s]: asked to wait on non-existant child",YMSTR(task->path));
    int stat_loc;
    pid_t result;
    do {
        result = waitpid(task->childPid, &stat_loc, 0);
    } while ( result != task->childPid );
    task->result = stat_loc;
    task->exited = true;
}

int YMTaskGetExitStatus(YMTaskRef task_)
{
    __YMTaskRef task = (__YMTaskRef)task_;
    ymassert(task->exited,"task[%s]: hasn't exited",YMSTR(task->path));
    return task->result;
}

unsigned char *YMTaskGetOutput(YMTaskRef task_, uint32_t *outLength)
{
    __YMTaskRef task = (__YMTaskRef)task_;
    *outLength = task->outputLen;
    return task->output;
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_task_read_output_proc(YM_THREAD_PARAM ctx_)
{
    __YMTaskRef task = ctx_;
    
    ymlog("task[%s]: flush output started...",YMSTR(task->path));
    
    YM_IO_BOILERPLATE
    
    _YMPipeCloseInputFile(task->outputPipe);
    YMFILE outFd = YMPipeGetOutputFile(task->outputPipe);
    
#define OUTPUT_BUF_SIZE 1024
    unsigned char buf[OUTPUT_BUF_SIZE];
    task->output = malloc(OUTPUT_BUF_SIZE);
    task->outputLen = OUTPUT_BUF_SIZE;
    off_t outputOff = 0;
    
    while(true) {
        YM_READ_FILE(outFd, buf, 1024);
        if ( aRead == -1 ) {
            ymerr("task[%s]: error reading output: %d %s",YMSTR(task->path),error,errorStr);
            break;
        } else if ( aRead == 0 ) {
            ymlog("task[%s]: finished reading output: %lldb",YMSTR(task->path),outputOff);
            break;
        } else {
            if ( ( outputOff + aRead ) >= task->outputLen ) {
                task->outputLen *= 2;
                task->output = realloc(task->output, task->outputLen);
            }
            
            ymdbg("task[%s]: flushed %zu bytes...",YMSTR(task->path),aRead);
            memcpy(task->output + outputOff, buf, aRead);
            outputOff += aRead;
        }
    }
    
    if ( outputOff + 1 >= task->outputLen ) {
        task->outputLen++;
        task->output = realloc(task->output,task->outputLen);
    }
    task->output[task->outputLen-1] = '\0';
    
    YMRelease(task);
    
    YMRelease(task->outputPipe);
    task->outputPipe = NULL;
    YMRelease(task->outputThread);
    task->outputThread = NULL;
    
    ymlog("task[%s]: flush output exiting",YMSTR(task->path));
    
    YM_THREAD_END
}
