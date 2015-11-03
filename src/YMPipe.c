//
//  YMPipe.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMPipe.h"
#include "YMPrivate.h"

typedef const struct _YMPipe
{
    int inFd;
    int outFd;
    char *name;
} YMPipe;