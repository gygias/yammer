//
//  YMPipe.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPipe.h"
#include "YMPipePriv.h"
#include "YMUtilities.h"

#define ymlog_type YMLogIO
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_pipe_t
{
    _YMType _type;
    
    YMStringRef name;
	YMFILE inFd;
	YMFILE outFd;
} __ym_pipe_t;
typedef struct __ym_pipe_t *__YMPipeRef;

void __YMPipeCloseOutputFile(YMPipeRef pipe_);
void __YMPipeCloseFile(__YMPipeRef pipe, YMFILE *fdPtr);

YMPipeRef YMPipeCreate(YMStringRef name)
{
    YM_IO_BOILERPLATE
    
    uint64_t iter = 1;
	YMFILE fds[2];
    YM_CREATE_PIPE(fds);
    
    while ( result != 0 )
    {
#if !defined(YMWIN32)
        if ( errno == EFAULT )
        {
            ymerr("pipe[%s]: error: invalid address space",YMSTR(name));
            return NULL;
        }
#endif
		sleep(1);

        if ( iter )
        {
            iter++;
            if ( iter > 100 )
                ymerr("pipe[%s]: warning: new files unavailable for pipe()",YMSTR(name));
        }
        
        YM_CREATE_PIPE(fds);
    }
  
#if defined(YMDEBUG) \
			&& defined(QUANTUM_PROFESSOR)
    // todo this number is based on mac test cases, if open files rises above 200
    // something isn't closing/releasing their streams (in practice doesn't seem to go above 100)
#define TOO_MANY_FILES 200
    int openFiles = YMGetNumberOfOpenFilesForCurrentProcess();
	ymsoftassert(openFiles<TOO_MANY_FILES, "too many open files");
#endif

	__YMPipeRef aPipe = (__YMPipeRef)_YMAlloc(_YMPipeTypeID, sizeof(struct __ym_pipe_t));

	aPipe->name = name ? YMRetain(name) : YMSTRC("*");

    aPipe->outFd = fds[0];
    aPipe->inFd = fds[1];
    
    return aPipe;
}

void _YMPipeFree(YMTypeRef object)
{
    __YMPipeRef pipe = (__YMPipeRef)object;
    
    _YMPipeCloseInputFile(pipe);
    __YMPipeCloseOutputFile(pipe);
    
    YMRelease(pipe->name);
}

YMFILE YMPipeGetInputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
    ymassert(pipe->inFd!=NULL_FILE,"input file is closed");
    return pipe->inFd;
}
    
YMFILE YMPipeGetOutputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
    ymassert(pipe->outFd!=NULL_FILE,"output file is closed");
    return pipe->outFd;
}
    
void __YMPipeCloseOutputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
    
    YMSelfLock(pipe);
    __YMPipeCloseFile(pipe, &pipe->outFd);
    YMSelfUnlock(pipe);
}

void _YMPipeCloseInputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
    
    // todo: not sure we need to be guarding here, but erring on the side of not inadvertently closing newly recycled fds due to a race
    YMSelfLock(pipe);
    __YMPipeCloseFile(pipe, &pipe->inFd);
    YMSelfUnlock(pipe);
}

void __YMPipeCloseFile(__YMPipeRef pipe, YMFILE *fdPtr)
{
    YMFILE fd = *fdPtr;
    *fdPtr = NULL_FILE;
    if ( fd != NULL_FILE )
    {
        int result, error = 0;
        const char *errorStr = NULL;
        
        ymlog("   pipe[%s]: closing f%d",YMSTR(pipe->name),fd);
		YM_CLOSE_FILE(fd);

        if ( result != 0 )
        {
            ymerr("   pipe[%s]: close on f%d failed: %d (%s)",YMSTR(pipe->name), fd, result, errorStr);
            //abort(); plexer
        }
    }
}

YM_EXTERN_C_POP
