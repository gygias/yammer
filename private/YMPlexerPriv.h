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

// shouldn't really exist outside of YMPlexer.c, but for now during development let others 'see into' the stream id
typedef uint32_t YMPlexerStreamID;
#define YMPlexerStreamIDMax UINT32_MAX

typedef struct ym_plexer_stream_user_info_def
{
    const char *name;
    YMPlexerStreamID streamID;
} _ym_plexer_stream_user_info_def;
typedef struct ym_plexer_stream_user_info_def ym_plexer_stream_user_info;
typedef ym_plexer_stream_user_info * ym_plexer_stream_user_info_ref;

#endif /* YMPlexerPriv_h */
