//
//  YMString.h
//  yammer
//
//  Created by david on 11/13/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMString_h
#define YMString_h

YM_EXTERN_C_PUSH

#include <libyammer/YMBase.h>

typedef const struct __ym_string * YMStringRef;

YMStringRef YMAPI YMStringCreate(void);
YMStringRef YMAPI YMStringCreateWithCString(const char *cString);
YMStringRef YMAPI YMStringCreateWithFormat(const char *format,...) YM_VARGS_SENTINEL_REQUIRED __printflike(1, 2);
YMStringRef YMAPI YMStringCreateByAppendingString(YMStringRef base, YMStringRef append);
// todo, can you "forward" a va_args to another actual function? not as far as i can find
//YMStringRef YMStringCreateByAppendingFormat(YMStringRef base, const char *format, ...) YM_VARGS_SENTINEL_REQUIRED __printflike(2,3);
#define YMStringCreateByAppendingFormat(base, format, ...) \
    { \
        YMStringRef baseFormat = YMStringCreateByAppendingString((base), YMStringCreateWithCString(format)); \
        YMStringRef formattedAppend = YMStringCreateWithFormat((format), ##__VA_ARGS__); \
        YMRelease(baseFormat); \
        return formatAppended; \
    }

size_t YMAPI YMStringGetLength(YMStringRef string);
const YMAPI char * YMStringGetCString(YMStringRef string);
bool YMAPI YMStringEquals(YMStringRef stringA, YMStringRef stringB);
bool YMAPI YMStringHasPrefix(YMStringRef string, YMStringRef prefix);
bool YMAPI YMStringHasPrefix2(YMStringRef string, const char *prefix);

#define YMSTR(x) YMStringGetCString(x)
#define YMSTRC(x) YMStringCreateWithCString(x)
YM_WPPUSH
#define YMSTRCF(x,...) YMStringCreateWithFormat((x),##__VA_ARGS__,NULL)
YM_WPOP
#define YMLEN(x) YMStringGetLength(x)

YM_EXTERN_C_POP

#endif /* YMString_h */
