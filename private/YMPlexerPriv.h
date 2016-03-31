//
//  YMPlexerPriv.h
//  yammer
//
//  Created by david on 11/13/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPlexerPriv_h
#define YMPlexerPriv_h

#include "YMLock.h"

YM_EXTERN_C_PUSH

// shouldn't really exist outside of YMPlexer.c, but for now during development let others 'see into' the stream id
typedef uint64_t YMPlexerStreamID;
#define YMPlexerStreamIDMax UINT64_MAX

typedef struct ym_plexer_stream_user_info_t
{
    const char *name;
    YMPlexerStreamID streamID;
} ym_plexer_stream_user_info_t;
typedef struct ym_plexer_stream_user_info_t * ym_plexer_stream_user_info_ref;

YM_EXTERN_C_POP

#endif /* YMPlexerPriv_h */
