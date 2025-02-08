//
//  YMTask.c
//  yammer
//
//  Created by david on 12/8/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMTask.h"

#include "YMPipePriv.h"
#include "YMDispatch.h"
#include "YMDispatchUtils.h"

#if defined(YMLINUX)
# include <sys/wait.h>
#elif defined(YMWIN32)
# define pid_t int32_t
#endif

#define ymlog_pre "task[%s]: "
#define ymlog_args YMSTR(t->path)
#define ymlog_type YMLogDefault
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_task
{
    _YMType _common;
    
    YMStringRef path;
    YMArrayRef args;
    bool save;
    
    pid_t childPid;
    bool exited;
    int result;
    unsigned char *output;
    uint32_t outputLen;
    YMPipeRef outputPipe;
    YMDispatchQueueRef outputQueue;
#if defined(YMWIN32)
	HANDLE childHandle;
#endif
} __ym_task;
typedef struct __ym_task __ym_task_t;

#define NULL_PID (-1)

YM_ENTRY_POINT(__ym_task_read_output_proc);
void __ym_task_prepare_atfork();
void __ym_task_parent_atfork();
void __ym_task_child_atfork();
YM_ONCE_DEF(__YMTaskRegisterAtfork);

YMTaskRef YMTaskCreate(YMStringRef path, YMArrayRef args, bool saveOutput)
{
    __ym_task_t *t = (__ym_task_t *)_YMAlloc(_YMTaskTypeID, sizeof(__ym_task_t));
    t->path = YMRetain(path);
    t->args = args ? YMRetain(args) : NULL;
    t->save = saveOutput;
    
    t->childPid = NULL_PID;
    t->exited = false;
    t->output = NULL;
    t->outputPipe = NULL;
    t->outputQueue = NULL;
    t->outputLen = 0;
    t->result = -1;
    return t;
}

void _YMTaskFree(YMTaskRef t)
{
    YMRelease(t->path);
    if ( t->args )
        YMRelease(t->args);
    if ( t->output )
        YMFREE(t->output);
    if ( t->outputQueue )
        YMRelease(t->outputQueue);
}

#if defined(YMWIN32)
#define pthread_atfork(x,y,z) 0
#endif

YM_ONCE_FUNC(__YMTaskRegisterAtfork, {
    int result = pthread_atfork(__ym_task_prepare_atfork, __ym_task_parent_atfork, __ym_task_child_atfork);
    ymassert(result==0,"failed to register atfork handlers: %d %s",errno,strerror(errno))
})

YM_ONCE_OBJ gYMTaskOnce = YM_ONCE_INIT;

bool YMTaskLaunch(YMTaskRef t_)
{
    __ym_task_t *t = (__ym_task_t *)t_;
    
    YM_IO_BOILERPLATE

	bool okay = true;
    
    if ( t->save ) {
        t->outputPipe = YMPipeCreate(NULL);
        ymlog("output pipe %d -> %d",YMPipeGetInputFile(t->outputPipe),YMPipeGetOutputFile(t->outputPipe));
    }

#if !defined(YMWIN32)

    YM_ONCE_DO(gYMTaskOnce, __YMTaskRegisterAtfork);
    
    pid_t pid = fork();
    
    if ( pid == 0 ) { // child
        int64_t nArgs = t->args ? YMArrayGetCount(t->args) : 0;
        
        int64_t argvSize = nArgs + 2;
        const char **argv = malloc((unsigned long)argvSize*sizeof(char *));
        
        argv[0] = YMSTR(t->path);
        argv[argvSize - 1] = NULL;
        
#define ymtask_aline 512
        uint16_t max = ymtask_aline, off = 0;
        char line[ymtask_aline];
        off += snprintf(line+off,max-off,"%s",argv[0]);
        for(int64_t i = 0; i < nArgs; i++) {
            argv[i + 1] = YMArrayGet(t->args, i);
            off += snprintf(line+off,max-off," %s",argv[i + 1]);
        }

        printf("%s\n",argv[0]);

        if ( t->save ) {
            int pipeIn = YMPipeGetInputFile(t->outputPipe);
            result = dup2(pipeIn, STDOUT_FILENO);
            if ( result == -1 ) { printf("fatal: %s: dup2(%d<-%d): %d %s\n",argv[0],pipeIn,STDOUT_FILENO,errno,strerror(errno)); abort(); }
            YMRelease(t->outputPipe); // closes in and out
        }

        execv(argv[0], (char * const *)argv);
        printf("fatal: %s: execv: %d %s\n",argv[0],errno,strerror(errno));
        exit(EXIT_FAILURE);
    }

    t->childPid = pid;

#else
	PROCESS_INFORMATION procInfo = {0};

	STARTUPINFO startupInfo = { 0 };
    if ( t->save ) {
		startupInfo.cb = sizeof(STARTUPINFO);
		startupInfo.hStdOutput = YMPipeGetInputFile(t->outputPipe);
		startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
		startupInfo.dwFlags |= STARTF_USESTDHANDLES;
	}

	int cmdLen = strlen(YMSTR(t->path));
	int cmdlineSize = cmdLen + 128;
	wchar_t *cmdline = YMALLOC(cmdlineSize,sizeof(wchar_t));

	int cmdLenNull = strlen(YMSTR(t->path)) + 1;
	swprintf(cmdline, cmdlineSize, L"%hs", YMSTR(t->path));
	int cmdlineLen = cmdLen;

	if ( t->args ) {
		for ( int64_t i = 0; i < YMArrayGetCount(t->args); i++ ) {
			int argMaxLen = 256;
			const char *arg = YMArrayGet(t->args,i);
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
	YMFREE(cmdline);

	t->childPid = procInfo.dwProcessId;
	t->childHandle = procInfo.hProcess;

#endif

    if ( t->save ) {
        YMStringRef name = YMSTRC("ymtask");
        t->outputQueue = YMDispatchQueueCreate(name);
        YMRelease(name);
        ym_dispatch_user_t dispatch = { __ym_task_read_output_proc, (void *)YMRetain(t), NULL, ym_dispatch_user_context_noop };
        YMDispatchAsync(t->outputQueue,&dispatch);
    }
	ymlog("forked: p%d", t->childPid);

	return okay;
}

void YMTaskWait(YMTaskRef t_)
{
    __ym_task_t *t = (__ym_task_t *)t_;
    ymassert(t->childPid!=NULL_PID,ymlog_pre "asked to wait on non-existant child",YMSTR(t->path));

#if !defined(YMWIN32)

    int stat_loc;
    pid_t result;
    do {
        result = waitpid(t->childPid, &stat_loc, 0);
    } while ( result != t->childPid );
    ymerr("p%d exited%s with %d", t->childPid, WIFEXITED(result) ?
                                                        "" :
                                                        (WIFSIGNALED(result) ? " signaled," : " (*)"), stat_loc);
    t->result = stat_loc;

#else

	DWORD result = WaitForSingleObject(t->childHandle, INFINITE);
	if ( result == WAIT_OBJECT_0 ) {
		DWORD exitCode = 0;
		BOOL okay = GetExitCodeProcess(t->childHandle, &exitCode);
		if ( okay ) {
			ymerr("p%d exited with %d", t->childPid, exitCode);
			t->result = exitCode;
		} else
			ymerr("GetExitCodeProcess(p%d) failed: %x",t->childPid,GetLastError());
	} else
		ymerr("WaitForSingleObject(p%d) failed: %d %x",t->childPid,result,GetLastError());

#endif
    
    if (t->save) {
        YMDispatchJoin(t->outputQueue);
        ymerr("joined on output thread...");
    }

    t->exited = true;
}

int YMTaskGetExitStatus(YMTaskRef t_)
{
    __ym_task_t *t = (__ym_task_t *)t_;
    ymassert(t->exited,ymlog_pre "hasn't exited",YMSTR(t->path));
    return t->result;
}

const unsigned char *YMTaskGetOutput(YMTaskRef t_, uint32_t *outLength)
{
    __ym_task_t *t = (__ym_task_t *)t_;
    if ( t->save )
      YMDispatchJoin(t->outputQueue);
    *outLength = t->outputLen;
    return t->output;
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

YM_ENTRY_POINT(__ym_task_read_output_proc)
{
    __ym_task_t *t = context;
    
    ymlog("flush output started...");
    
    YM_IO_BOILERPLATE
    
    _YMPipeCloseInputFile(t->outputPipe);
    YMFILE outFd = YMPipeGetOutputFile(t->outputPipe);
    
#define OUTPUT_BUF_INIT_SIZE 1024
    t->output = malloc(OUTPUT_BUF_INIT_SIZE);
    off_t outputBufSize = OUTPUT_BUF_INIT_SIZE;
    off_t outputOff = 0;
    
    while(true) {
        while( ( outputOff + OUTPUT_BUF_INIT_SIZE ) > outputBufSize ) {
            outputBufSize *= 2;
            t->output = realloc(t->output, (unsigned long)outputBufSize);
        }
        YM_READ_FILE(outFd, t->output + outputOff, OUTPUT_BUF_INIT_SIZE);
        if ( result == -1 ) {
            ymerr("reading output: %d %s",error,errorStr);
            break;
        } else if ( result == 0 ) {
            ymlog("finished reading output: %db",(int)outputOff);
            break;
        } else {
            ymdbg("flushed %ld bytes...",result);
            outputOff += result;
        }
    }
    
    if ( outputOff == outputBufSize ) {
        outputBufSize++;
        t->output = realloc(t->output, (unsigned long)outputBufSize);
    }
    for( int i = 0; i < outputOff; i++ ) {
        if ( t->output[i] == '\0' )
            t->output[i] = '_';
    }
    t->output[outputOff] = '\0';
    t->outputLen = (uint32_t)outputOff;
    
    YMRelease(t);
    
    YMRelease(t->outputPipe);
    t->outputPipe = NULL;
    
    ymlog("flush output exiting");
}

YM_EXTERN_C_POP
