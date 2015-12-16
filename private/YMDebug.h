//
//  YMPrivate.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMPrivate_h
#define YMPrivate_h

YM_EXTERN_C_PUSH

#if defined(YMDEBUG)

extern YMAPI bool gYMFirstConnectionFakeSampleUse;
extern YMAPI int64_t gYMFirstConnectionFakeSample;
#define YM_DEBUG_SAMPLE { if ( gYMFirstConnectionFakeSampleUse ) connection->sample = gYMFirstConnectionFakeSample; ymerr("debug: sample %lldb",gYMFirstConnectionFakeSample); }

#else

#define YM_DEBUG_SAMPLE

#endif

YM_EXTERN_C_POP

#endif /* YMPrivate_h */
