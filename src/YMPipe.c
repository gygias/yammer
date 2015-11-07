//
//  YMPipe.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMPipe.h"
#include "YMPrivate.h"

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
            YMLog("pipe[%s]: error: invalid address space",name);
            free(ymPipe->name);
            free(ymPipe);
            return NULL;
        }
        usleep(10000);
        if ( iter )
        {
            iter++;
            if ( iter > 100 )
                YMLog("pipe[%s]: warning: new files unavailable for pipe()",name);
        }
    }
    
    ymPipe->outFd = fds[0];
    ymPipe->inFd = fds[1];
    
    return (YMPipeRef)ymPipe;
}

#pragma message "stream manages closure of files, but that should probably be actually done at this level"
void _YMPipeFree(YMTypeRef object)
{
    _YMPipe *pipe = (_YMPipe *)object;
    if ( pipe->name )
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
