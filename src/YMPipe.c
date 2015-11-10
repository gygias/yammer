//
//  YMPipe.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPipe.h"
#include "YMPrivate.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogPipe
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

typedef struct __YMPipe
{
    YMTypeID _typeID;
    
    char *name;
    int inFd;
    int outFd;
} _YMPipe;

YMPipeRef YMPipeCreate(char *name)
{
    _YMPipe *ymPipe = (_YMPipe *)calloc(1,sizeof(_YMPipe));
    ymPipe->_typeID = _YMPipeTypeID;
    
    ymPipe->name = strdup(name?name:"unnamed");
    
    uint64_t iter = 1;
    int fds[2];
    while ( pipe(fds) == -1 )
    {
        if ( errno == EFAULT )
        {
            ymerr("pipe[%s]: error: invalid address space",name);
            free(ymPipe->name);
            free(ymPipe);
            return NULL;
        }
        usleep(10000);
        if ( iter )
        {
            iter++;
            if ( iter > 100 )
                ymerr("pipe[%s]: warning: new files unavailable for pipe()",name);
        }
    }
    
    ymPipe->outFd = fds[0];
    ymPipe->inFd = fds[1];
    
    return (YMPipeRef)ymPipe;
}

void _YMPipeFree(YMTypeRef object)
{
    _YMPipe *pipe = (_YMPipe *)object;
    
    int aClose;
    aClose = close(pipe->inFd);
    if ( aClose != 0 )
    {
        ymerr("   pipe[%s,i%d] fatal: close failed: %d (%s)",pipe->name, pipe->inFd, errno, strerror(errno));
        abort();
    }
    
    // todo add 'data remaining' check or warning here?
    
    aClose = close(pipe->outFd);
    if ( aClose != 0 )
    {
        ymerr("   pipe[%s,i%d] fatal: close failed: %d (%s)",pipe->name, pipe->outFd, errno, strerror(errno));
        abort();
    }
    
    free(pipe->name);
    free(pipe);
    
}

int YMPipeGetInputFile(YMPipeRef pipe)
{
    return ((_YMPipe *)pipe)->inFd;
}

int YMPipeGetOutputFile(YMPipeRef pipe)
{
    return ((_YMPipe *)pipe)->outFd;
}
