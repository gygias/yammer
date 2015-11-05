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
    
    ymPipe->name = strdup(name);
    
    int fds[2];
    while ( pipe(fds) == -1 )
    {
    }
    
    ymPipe->outFd = fds[0];
    ymPipe->inFd = fds[1];
    
    return (YMPipeRef)ymPipe;
}

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
