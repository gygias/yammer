//
//  YMPipe.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPipe_h
#define YMPipe_h

typedef struct _YMPipe *YMPipeRef;

YMPipeRef YMPipeCreate(char *name);

int YMPipeGetInputFile(YMPipeRef pipe);
int YMPipeGetOutputFile(YMPipeRef pipe);

#endif /* YMPipe_h */
