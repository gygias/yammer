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

#define OPEN_FILE_MIN 0
#define CLOSED_FILE -1

typedef struct __ym_pipe
{
    _YMType _type;
    
    YMStringRef name;
    int inFd;
    int outFd;
} ___ym_pipe;
typedef struct __ym_pipe __YMPipe;
typedef __YMPipe *__YMPipeRef;

void __YMPipeCloseInputFile(YMPipeRef pipe_);
void __YMPipeCloseOutputFile(YMPipeRef pipe_);

YMPipeRef YMPipeCreate(YMStringRef name)
{
    __YMPipeRef aPipe = (__YMPipeRef)_YMAlloc(_YMPipeTypeID,sizeof(__YMPipe));
    
    aPipe->name = name ? YMRetain(name) : YMSTRC("unnamed");
    aPipe->inFd = CLOSED_FILE;
    aPipe->outFd = CLOSED_FILE;
    
    uint64_t iter = 1;
    int fds[2];
    while ( pipe(fds) == -1 )
    {
        if ( errno == EFAULT )
        {
            ymerr("pipe[%s]: error: invalid address space",YMSTR(name));
            YMRelease(aPipe);
            return NULL;
        }
        usleep(10000);
        if ( iter )
        {
            iter++;
            if ( iter > 100 )
                ymerr("pipe[%s]: warning: new files unavailable for pipe()",YMSTR(name));
        }
    }
  
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

int YMPipeGetInputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
    return pipe->inFd;
}

int YMPipeGetOutputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
    return pipe->outFd;
}

void _YMPipeCloseInputFile(YMPipeRef pipe)
{
    __YMPipeCloseInputFile(pipe);
}

void __YMPipeCloseInputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
    
    int aClose = -2;
    int inFd = pipe->inFd; // don't think we need to be defensive here, but ok
    pipe->inFd = CLOSED_FILE;
    if ( inFd >= OPEN_FILE_MIN )
    {
        ymlog("   pipe[%s]: closing %d",YMSTR(pipe->name),inFd);
        aClose = close(inFd);
        if ( aClose != 0 )
        {
            ymerr("   pipe[%s,i%d] fatal: close failed: %d (%s)",YMSTR(pipe->name), inFd, aClose, ( aClose == -2 ) ? "already closed" : strerror(errno));
            abort();
        }
    }
}

void __YMPipeCloseOutputFile(YMPipeRef pipe_)
{
    __YMPipeRef pipe = (__YMPipeRef)pipe_;
    
    int aClose = -2;
    int outFd = pipe->outFd; // don't think we need to be defensive here, but ok
    pipe->outFd = CLOSED_FILE;
    if ( outFd >= OPEN_FILE_MIN )
    {
        ymlog("   pipe[%s]: closing %d",YMSTR(pipe->name),outFd);
        aClose = close(outFd);
        if ( aClose != 0 )
        {
            ymerr("   pipe[%s,o%d] fatal: close failed: %d (%s)",YMSTR(pipe->name), outFd, aClose, ( aClose == -2 ) ? "already closed" : strerror(errno));
            abort();
        }
    }
}
