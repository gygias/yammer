//
//  YMPipe.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPipe_h
#define YMPipe_h

typedef YMTypeRef YMPipeRef;

YMPipeRef YMPipeCreate(YMStringRef name);

YMFILE YMPipeGetInputFile(YMPipeRef pipe);
YMFILE YMPipeGetOutputFile(YMPipeRef pipe);

#endif /* YMPipe_h */
