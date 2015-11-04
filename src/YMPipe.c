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
    
    int inFd;
    int outFd;
    char *name;
} _YMPipe;

YMPipeRef YMPipeCreate(char *name, int inFd, int outFd)
{
    _YMPipe *pipe = (_YMPipe *)calloc(1,sizeof(_YMPipe));
    pipe->_typeID = _YMPipeTypeID;
    
    pipe->name = strdup(name);
    pipe->inFd = inFd;
    pipe->outFd = outFd;
    
    return (YMPipeRef)pipe;
}

void _YMPipeFree(YMTypeRef object)
{
    _YMPipe *pipe = (_YMPipe *)object;
    if ( pipe->name )
        free(pipe->name);
    free(pipe);
    
}