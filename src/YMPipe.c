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

#define ymlog_pre "pipe[%s]: "
#define ymlog_args YMSTR(p->name)
#define ymlog_type YMLogIO
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_pipe
{
    _YMType _common;
    
    YMStringRef name;
	YMFILE inFd;
	YMFILE outFd;
} __ym_pipe;
typedef struct __ym_pipe __ym_pipe_t;

void __YMPipeCloseOutputFile(__ym_pipe_t *);
void __YMPipeCloseFile(__ym_pipe_t *, YMFILE *);

YMPipeRef YMPipeCreate(YMStringRef name)
{
    YM_IO_BOILERPLATE
    
    uint64_t iter = 1;
	YMFILE fds[2];
    YM_CREATE_PIPE(fds);
    
    while ( result != 0 ) {
#if !defined(YMWIN32)
        if ( errno == EFAULT ) {
            ymerrg("pipe[%s]: invalid address space",YMSTR(name));
            return NULL;
        }
#endif
		sleep(1);

        if ( iter ) {
            iter++;
            if ( iter > 100 )
                ymerrg("pipe[%s]: new files unavailable for pipe()",YMSTR(name));
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

	__ym_pipe_t *p = (__ym_pipe_t *)_YMAlloc(_YMPipeTypeID, sizeof(__ym_pipe_t));

	p->name = name ? YMRetain(name) : YMSTRC("*");

    p->outFd = fds[0];
    p->inFd = fds[1];
    
    return p;
}

void _YMPipeFree(YMTypeRef o_)
{
    YMPipeRef p = (YMPipeRef)o_;
    
    _YMPipeCloseInputFile(p);
    __YMPipeCloseOutputFile((__ym_pipe_t *)p);
    
    YMRelease(p->name);
}

YMFILE YMPipeGetInputFile(YMPipeRef p)
{
    ymassert(p->inFd!=NULL_FILE,"input file is closed");
    return p->inFd;
}
    
YMFILE YMPipeGetOutputFile(YMPipeRef p)
{
    ymassert(p->outFd!=NULL_FILE,"output file is closed");
    return p->outFd;
}
    
void __YMPipeCloseOutputFile(__ym_pipe_t * p)
{
    YMSelfLock(p);
    __YMPipeCloseFile(p, &p->outFd);
    YMSelfUnlock(p);
}

void _YMPipeCloseInputFile(YMPipeRef p)
{
    // todo: not sure we need to be guarding here, but erring on the side of not inadvertently closing newly recycled fds due to a race
    YMSelfLock(p);
    __YMPipeCloseFile((__ym_pipe_t *)p, &((__ym_pipe_t *)p)->inFd);
    YMSelfUnlock(p);
}

void YMAPI _YMPipeSetClosed(YMPipeRef p_)
{
    __ym_pipe_t *p = (__ym_pipe_t *)p_;
    p->inFd = NULL_FILE;
    p->outFd = NULL_FILE;
}

void __YMPipeCloseFile(__ym_pipe_t *p, YMFILE *fdPtr)
{
    YM_IO_BOILERPLATE

    YMFILE fd = *fdPtr;
    *fdPtr = NULL_FILE;
    if ( fd != NULL_FILE ) {
        
        ymlog("closing f%d",fd);
		YM_CLOSE_FILE(fd);

        if ( result != 0 ) {
            ymerr("close on f%d failed: %zd (%s)", fd, result, errorStr);
            //abort(); bubble up to plexer
        }
    }
}

YM_EXTERN_C_POP
