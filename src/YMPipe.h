//
//  YMPipe.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMPipe_h
#define YMPipe_h

#include "YMBase.h"

typedef struct _YMPipe *YMPipeRef;

YMPipeRef YMPipeCreate(char *name, int inFd, int outFd);

#endif /* YMPipe_h */
