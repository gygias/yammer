//
//  YMStream.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMStream.h"
#include "YMPrivate.h"

#include "YMPipe.h"

typedef struct __YMStream
{
    YMPipeRef upStream;
    YMPipeRef downStream;
} _YMStream;

void _YMStreamFree(YMTypeRef object)
{
    _YMStream *stream = (_YMStream *)object;
    if ( stream->upStream )
        _YMStreamFree(stream->upStream);
    if ( stream->downStream )
        _YMStreamFree(stream->downStream);
    free(stream);
}