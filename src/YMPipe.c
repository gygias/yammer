//
//  YMPipe.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPipe.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogPipe
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

#ifndef WIN32
#define CLOSED_FILE -1
#else
#define CLOSED_FILE NULL
#endif

typedef struct __ym_pipe
{
    _YMType _type;
    
    YMStringRef name;
	YMFILE inFd;
	YMFILE outFd;
} ___ym_pipe;
typedef struct __ym_pipe __YMPipe;
typedef __YMPipe *__YMPipeRef;

void __YMPipeCloseInputFile(YMPipeRef pipe_);
void __YMPipeCloseOutputFile(YMPipeRef pipe_);

YMPipeRef YMPipeCreate(YMStringRef name)
{
    __YMPipeRef aPipe = (__YMPipeRef)_YMAlloc(_YMPipeTypeID,sizeof(__YMPipe));
    
    aPipe->name = name ? YMRetain(name) : YMSTRC("*");
    aPipe->inFd = CLOSED_FILE;
    aPipe->outFd = CLOSED_FILE;
    
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
  
    // todo this number is based on mac test cases, if open files rises above 200
    // something isn't closing/releasing their streams (in practice doesn't seem to go above 100)
#define TOO_MANY_FILES 200
#ifdef DEBUG
    if ( fds[0] > TOO_MANY_FILES || fds[1] > TOO_MANY_FILES )
        abort();
#endif
    
    aPipe->outFd = fds[0];
    aPipe->inFd = fds[1];
    
    return aPipe;
}

void _YMPipeFree(YMTypeRef object)
{
    __YMPipeRef pipe = (__YMPipeRef)object;
    
    __YMPipeCloseInputFile(pipe);
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

void _YMPipeCloseInputFile(YMPipeRef pipe)
{
    __YMPipeCloseInputFile(pipe);
}

void __YMPipeCloseFile(__YMPipeRef pipe, YMFILE *fdPtr)
{
    YMFILE fd = *fdPtr;
    *fdPtr = CLOSED_FILE;
    if ( fd != CLOSED_FILE )
    {
        ymlog("   pipe[%s]: closing %d",YMSTR(pipe->name),fd);
		int aClose = 0;
#ifndef WIN32
        aClose = close(fd);
#else
		BOOL okay = CloseHandle(fd);
		if ( ! okay )
			aClose = -1;
#endif

        if ( aClose != 0 )
        {
            ymerr("   pipe[%s,i%d]: close failed: %d (%s)",YMSTR(pipe->name), fd, aClose, ( aClose == -2 ) ? "already closed" : strerror(errno));
            //abort(); plexer
        }
    }
    
}

void __YMPipeCloseInputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
    
    // todo: not sure we need to be guarding here, but erring on the side of not inadvertently closing newly recycled fds due to a race
    YMSelfLock(pipe);
    __YMPipeCloseFile(pipe, &pipe->inFd);
    YMSelfUnlock(pipe);
}

void __YMPipeCloseOutputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
    
    YMSelfLock(pipe);
    __YMPipeCloseFile(pipe, &pipe->outFd);
    YMSelfUnlock(pipe);
}
