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

typedef struct __ym_pipe
{
    _YMType _type;
    
    YMStringRef name;
    int inFd;
    int outFd;
} ___ym_pipe;
typedef struct __ym_pipe __YMPipe;
typedef __YMPipe *__YMPipeRef;

YMPipeRef YMPipeCreate(YMStringRef name)
{
    __YMPipeRef aPipe = (__YMPipeRef)_YMAlloc(_YMPipeTypeID,sizeof(__YMPipe));
    
    aPipe->name = name ? YMRetain(name) : YMSTRC("unnamed");
    aPipe->inFd = -1;
    aPipe->outFd = -1;
    
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
    
    aPipe->outFd = fds[0];
    aPipe->inFd = fds[1];
    
    return aPipe;
}

void _YMPipeFree(YMTypeRef object)
{
    __YMPipeRef pipe = (__YMPipeRef)object;
    
    int aClose;
    if ( pipe->inFd >= 0 )
    {
        aClose = close(pipe->inFd);
        if ( aClose != 0 )
        {
            ymerr("   pipe[%s,i%d] fatal: close failed: %d (%s)",YMSTR(pipe->name), pipe->inFd, errno, strerror(errno));
            abort();
        }
    }
    
    // todo add 'data remaining' check or warning here?
    
    if ( pipe->outFd >= 0 )
    {
        aClose = close(pipe->outFd);
        if ( aClose != 0 )
        {
            ymerr("   pipe[%s,i%d] fatal: close failed: %d (%s)",YMSTR(pipe->name), pipe->outFd, errno, strerror(errno));
            abort();
        }
    }
    
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
