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

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogIO
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

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
    __YMPipeRef aPipe = (__YMPipeRef)_YMAlloc(_YMPipeTypeID,sizeof(struct __ym_pipe_t));
    
    aPipe->name = name ? YMRetain(name) : YMSTRC("*");
    aPipe->inFd = NULL_FILE;
    aPipe->outFd = NULL_FILE;
    
    uint64_t iter = 1;
	YMFILE fds[2];
#ifndef WIN32
    while ( pipe(fds) == -1 )
    {
        if ( errno == EFAULT )
        {
            ymerr("pipe[%s]: error: invalid address space",YMSTR(name));
            YMRelease(aPipe);
            return NULL;
        }
		usleep(10000);
#else
	while ( 0 == CreatePipe(&fds[0], &fds[1], NULL, 0) )
	{
		Sleep(100);
#endif
        if ( iter )
        {
            iter++;
            if ( iter > 100 )
                ymerr("pipe[%s]: warning: new files unavailable for pipe()",YMSTR(name));
        }
    }
  
#ifdef YMDEBUG
    // todo this number is based on mac test cases, if open files rises above 200
    // something isn't closing/releasing their streams (in practice doesn't seem to go above 100)
#define TOO_MANY_FILES 200
    int openFiles = YMGetNumberOfOpenFilesForCurrentProcess();
	ymsoftassert(openFiles<TOO_MANY_FILES, "too many open files");
#endif
    
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
    return pipe->inFd;
}
    
YMFILE YMPipeGetOutputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
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
        char *errorStr = NULL;
        
        ymlog("   pipe[%s]: closing f%d",YMSTR(pipe->name),fd);
		YM_CLOSE_FILE(fd);

        if ( result != 0 )
        {
            ymerr("   pipe[%s]: close on f%d failed: %d (%s)",YMSTR(pipe->name), fd, result, errorStr);
            //abort(); plexer
        }
    }
    
}
