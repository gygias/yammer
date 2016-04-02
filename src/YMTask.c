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

#if defined(YMLINUX)
# include <sys/wait.h>
#elif defined(YMWIN32)
# define pid_t int32_t
#endif

#define ymlog_pre "task[%s]: "
#define ymlog_args YMSTR(task->path)
#define ymlog_type YMLogDefault
#include "YMLog.h"

YM_EXTERN_C_PUSH

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
#if defined(YMWIN32)
	HANDLE childHandle;
#endif
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
    free(task->output);
    if ( task->outputThread )
        YMRelease(task->outputThread);
}

#if defined(YMWIN32)
#define pthread_atfork(x,y,z) 0
#endif

YM_ONCE_FUNC(__YMTaskRegisterAtfork, {
    int result = pthread_atfork(__ym_task_prepare_atfork, __ym_task_parent_atfork, __ym_task_child_atfork);
    ymassert(result==0,"failed to register atfork handlers: %d %s",errno,strerror(errno))
})

YM_ONCE_OBJ gYMTaskOnce = YM_ONCE_INIT;

bool YMTaskLaunch(YMTaskRef task_)
{
    __YMTaskRef task = (__YMTaskRef)task_;
    
    YM_IO_BOILERPLATE

	bool okay = true;
    
    if ( task->save ) {
        task->outputPipe = YMPipeCreate(NULL);
        ymlog("output pipe %d -> %d",YMPipeGetInputFile(task->outputPipe),YMPipeGetOutputFile(task->outputPipe));
        task->outputThread = YMThreadCreate(task->path, __ym_task_read_output_proc, (void *)YMRetain(task));
    }

#if !defined(YMWIN32)

    YM_ONCE_DO(gYMTaskOnce, __YMTaskRegisterAtfork);
    
    _YMLogLock();
    pid_t pid = fork();
    if ( pid == 0 ) { // child
        _YMLogUnlock();
        
        int64_t nArgs = task->args ? YMArrayGetCount(task->args) : 0;
        
        int64_t argvSize = nArgs + 2;
        const char **argv = malloc((unsigned long)argvSize*sizeof(char *));
        
        argv[0] = YMSTR(task->path);
        argv[argvSize - 1] = NULL;
        
        ymlogi("%s",argv[0]);
        for(int64_t i = 0; i < nArgs; i++) {
            argv[i + 1] = YMArrayGet(task->args, i);
            ymlogi(" %s",argv[i + 1]);
        }
        ymlogr();

        if ( task->save ) {
            int pipeIn = YMPipeGetInputFile(task->outputPipe);
            result = dup2(pipeIn, STDOUT_FILENO);
            if ( result == -1 ) { ymerr("dup2(%d<-%d): %d %s",pipeIn,STDOUT_FILENO,errno,strerror(errno)); abort(); }
            YMRelease(task->outputPipe); // closes in and out
        }

        execv(argv[0], (char * const *)argv);
        ymerr("execv: %d %s",errno,strerror(errno));
        exit(EXIT_FAILURE);
    }
    _YMLogUnlock();

    task->childPid = pid;

#else
	PROCESS_INFORMATION procInfo = {0};

	STARTUPINFO startupInfo = { 0 };
    if ( task->save ) {
		startupInfo.cb = sizeof(STARTUPINFO);
		startupInfo.hStdOutput = YMPipeGetInputFile(task->outputPipe);
		startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
		startupInfo.dwFlags |= STARTF_USESTDHANDLES;
	}

	int cmdLen = strlen(YMSTR(task->path));
	int cmdlineSize = cmdLen + 128;
	wchar_t *cmdline = calloc(cmdlineSize,sizeof(wchar_t));

	int cmdLenNull = strlen(YMSTR(task->path)) + 1;
	swprintf(cmdline, cmdlineSize, L"%hs", YMSTR(task->path));
	int cmdlineLen = cmdLen;

	if ( task->args ) {
		for ( int64_t i = 0; i < YMArrayGetCount(task->args); i++ ) {
			int argMaxLen = 256;
			const char *arg = YMArrayGet(task->args,i);
			int argLen = strlen(arg);
			ymassert(argLen<argMaxLen,"arg len >= 255, fix hard code");
			if ( ( argLen + 2 ) > ( cmdlineSize - cmdlineLen ) ) {
				cmdlineSize *= 2;
				cmdline = realloc(cmdline, cmdlineSize);
			}

			wcscat(cmdline,L" ");
			wchar_t wArg[256];
			swprintf(wArg, argMaxLen, L"%hs", arg);
			wcscat(cmdline,wArg);
		}
	}

	okay = CreateProcess(NULL,cmdline,NULL,NULL,true,0,NULL,NULL,&startupInfo,&procInfo);
	free(cmdline);

	task->childPid = procInfo.dwProcessId;
	task->childHandle = procInfo.hProcess;

#endif

	if (task->save)
		YMThreadStart(task->outputThread);

	ymlog("forked: p%d", task->childPid);

	return okay;
}

void YMTaskWait(YMTaskRef task_)
{
    __YMTaskRef task = (__YMTaskRef)task_;
    ymassert(task->childPid!=NULL_PID,ymlog_pre "asked to wait on non-existant child",YMSTR(task->path));

#if !defined(YMWIN32)

    int stat_loc;
    pid_t result;
    do {
        result = waitpid(task->childPid, &stat_loc, 0);
    } while ( result != task->childPid );
    ymerr("p%d exited%s with %d", task->childPid, WIFEXITED(result) ?
                                                        "" :
                                                        (WIFSIGNALED(result) ? " signaled," : " (*)"), stat_loc);
    task->result = stat_loc;

#else

	DWORD result = WaitForSingleObject(task->childHandle, INFINITE);
	if ( result == WAIT_OBJECT_0 ) {
		DWORD exitCode = 0;
		BOOL okay = GetExitCodeProcess(task->childHandle, &exitCode);
		if ( okay ) {
			ymerr("p%d exited with %d", task->childPid, exitCode);
			task->result = exitCode;
		} else
			ymerr("GetExitCodeProcess(p%d) failed: %x",task->childPid,GetLastError());
	} else
		ymerr("WaitForSingleObject(p%d) failed: %d %x",task->childPid,result,GetLastError());

#endif
    
    if (task->save) {
        YMThreadJoin(task->outputThread);
        ymerr("joined on output thread...");
    }

    task->exited = true;
}

int YMTaskGetExitStatus(YMTaskRef task_)
{
    __YMTaskRef task = (__YMTaskRef)task_;
    ymassert(task->exited,ymlog_pre "hasn't exited",YMSTR(task->path));
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

// printf: lazily avoid log chicken-and-egg
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
    
    ymlog("flush output started...");
    
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
            task->output = realloc(task->output, (unsigned long)outputBufSize);
        }
        YM_READ_FILE(outFd, task->output + outputOff, OUTPUT_BUF_INIT_SIZE);
        if ( aRead == -1 ) {
            ymerr("reading output: %d %s",error,errorStr);
            break;
        } else if ( aRead == 0 ) {
            ymlog("finished reading output: %db",(int)outputOff);
            break;
        } else {
            ymdbg("flushed %d bytes...",(int)aRead);
            outputOff += aRead;
        }
    }
    
    if ( outputOff == outputBufSize ) {
        outputBufSize++;
        task->output = realloc(task->output, (unsigned long)outputBufSize);
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
    
    ymlog("flush output exiting");
    
    YM_THREAD_END
}

YM_EXTERN_C_POP
