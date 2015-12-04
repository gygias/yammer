//
//  YMPipe.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPipe_h
#define YMPipe_h

typedef const struct __ym_pipe_t *YMPipeRef;

YMPipeRef YMAPI YMPipeCreate(YMStringRef name);

YMFILE YMAPI YMPipeGetInputFile(YMPipeRef pipe);
YMFILE YMAPI YMPipeGetOutputFile(YMPipeRef pipe);

#endif /* YMPipe_h */
