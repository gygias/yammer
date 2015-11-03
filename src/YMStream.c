//
//  YMStream.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMStream.h"

#include "YMPipe.h"

typedef const struct _YMStream
{
    YMPipeRef upStream;
    YMPipeRef downStream;
} YMStream;