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

#if defined(RPI)
#include <sys/wait.h>
#endif

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
void __ym_task_prepare_atfork();
void __ym_task_parent_atfork();
void __ym_task_child_atfork();
YM_ONCE_DEF(__YMTaskRegisterAtfork);

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
    if ( task->outputThread )
        YMRelease(task->outputThread);
}

YM_ONCE_FUNC(__YMTaskRegisterAtfork, {
    int result = pthread_atfork(__ym_task_prepare_atfork, __ym_task_parent_atfork, __ym_task_child_atfork);
    ymassert(result==0,"failed to register atfork handlers: %d %s",errno,strerror(errno))
});

YM_ONCE_OBJ gYMTaskOnce = YM_ONCE_INIT;

bool YMTaskLaunch(YMTaskRef task_)
{
    __YMTaskRef task = (__YMTaskRef)task_;
    
    YM_IO_BOILERPLATE
    
    if ( task->save )
    {
        task->outputPipe = YMPipeCreate(NULL);
        ymlog("task[%s]: output pipe %d -> %d",YMSTR(task->path),YMPipeGetInputFile(task->outputPipe),YMPipeGetOutputFile(task->outputPipe));
        task->outputThread = YMThreadCreate(task->path, __ym_task_read_output_proc, YMRetain(task));
    }
    
    YM_ONCE_DO(gYMTaskOnce, __YMTaskRegisterAtfork);
    
    _YMLogLock();
    pid_t pid = fork();
    if ( pid == 0 ) // child
    {
        _YMLogUnlock();
        
        int64_t nArgs = task->args ? YMArrayGetCount(task->args) : 0;
        
        int64_t argvSize = nArgs + 2;
        const char **argv = malloc(argvSize*sizeof(char *));
        
        argv[0] = YMSTR(task->path);
        argv[argvSize - 1] = NULL;
        
        fprintf(stderr,"task[%s]: %s",YMSTR(task->path),argv[0]);
        for(int64_t i = 0; i < nArgs; i++) {
            argv[i + 1] = YMArrayGet(task->args, i);
            fprintf(stderr," %s",argv[i + 1]);
        }
        fprintf(stderr,"\n");

        if ( task->save )
        {
            int pipeIn = YMPipeGetInputFile(task->outputPipe);
            result = dup2(pipeIn, STDOUT_FILENO);
            if ( result == -1 ) { fprintf(stderr,"task[%s]: dup2(%d<-%d) failed: %d %s",YMSTR(task->path),pipeIn,STDOUT_FILENO,errno,strerror(errno)); abort(); }
            YMRelease(task->outputPipe); // closes in and out
        }

        execv(argv[0], (char * const *)argv);
        fprintf(stderr,"task[%s]: execv failed: %d %s",YMSTR(task->path),errno,strerror(errno));
        exit(EXIT_FAILURE);
    }
    _YMLogUnlock();
    
    if ( task->save )
        YMThreadStart(task->outputThread);
    
    ymlog("task[%s]: forked: p%d",YMSTR(task->path) ,pid);
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
    if ( WIFEXITED(result) )
        ymlog("task[%s]: p%d exited with %d", YMSTR(task->path), task->childPid, stat_loc)
    else if ( WIFSIGNALED(result) )
        ymlog("task[%s]: p%d exited abnormally with %d", YMSTR(task->path), task->childPid, stat_loc)
    else
        ymlog("task[%s]: p%d unknown exit status %d", YMSTR(task->path), task->childPid, stat_loc)
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
    if ( task->save )
      YMThreadJoin(task->outputThread);
    *outLength = task->outputLen;
    return task->output;
}

void __ym_task_prepare_atfork() {
    fprintf(stderr,"__ym_task_prepare_atfork happened\n");
}

void __ym_task_parent_atfork() {
    fprintf(stderr,"__ym_task_parent_atfork happened\n");
    
}
void __ym_task_child_atfork() {
    fprintf(stderr,"__ym_task_child_atfork happened\n");
}

YM_THREAD_RETURN YM_CALLING_CONVENTION __ym_task_read_output_proc(YM_THREAD_PARAM ctx_)
{
    __YMTaskRef task = ctx_;
    
    ymlog("task[%s]: flush output started...",YMSTR(task->path));
    
    YM_IO_BOILERPLATE
    
    _YMPipeCloseInputFile(task->outputPipe);
    YMFILE outFd = YMPipeGetOutputFile(task->outputPipe);
    
#define OUTPUT_BUF_INIT_SIZE 1024
    task->output = malloc(OUTPUT_BUF_INIT_SIZE);
    off_t outputBufSize = OUTPUT_BUF_INIT_SIZE;
    off_t outputOff = 0;
    
    while(true) {
        while( ( outputOff + OUTPUT_BUF_INIT_SIZE ) > outputBufSize ) {
            outputBufSize *= 2;
            task->output = realloc(task->output, outputBufSize);
        }
        YM_READ_FILE(outFd, task->output + outputOff, OUTPUT_BUF_INIT_SIZE);
        if ( aRead == -1 ) {
            ymerr("task[%s]: error reading output: %d %s",YMSTR(task->path),error,errorStr);
            break;
        } else if ( aRead == 0 ) {
            ymlog("task[%s]: finished reading output: %db",YMSTR(task->path),(int)outputOff);
            break;
        } else {
            ymdbg("task[%s]: flushed %d bytes...",YMSTR(task->path),(int)aRead);
            outputOff += aRead;
        }
    }
    
    if ( outputOff == outputBufSize ) {
        outputBufSize++;
        task->output = realloc(task->output,outputBufSize);
    }
    for( int i = 0; i < outputOff; i++ ) {
        if ( task->output[i] == '\0' )
            task->output[i] = '_';
    }
    task->output[outputOff] = '\0';
    task->outputLen = (uint32_t)outputOff;
    
    YMRelease(task);
    
    YMRelease(task->outputPipe);
    task->outputPipe = NULL;
    
    ymlog("task[%s]: flush output exiting",YMSTR(task->path));
    
    YM_THREAD_END
}
