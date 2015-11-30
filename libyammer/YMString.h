//
//  YMString.h
//  yammer
//
//  Created by david on 11/13/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMString_h
#define YMString_h

#ifdef __cplusplus
extern "C" {
#endif

#include <libyammer/YMBase.h>

typedef const struct __ym_string *YMStringRef;

YMAPI YMStringRef YMStringCreate();
YMAPI YMStringRef YMStringCreateWithCString(const char *cString);
YMAPI YMStringRef YMStringCreateWithFormat(const char *format,...) YM_VARGS_SENTINEL_REQUIRED __printflike(1, 2);
YMAPI YMStringRef YMStringCreateByAppendingString(YMStringRef base, YMStringRef append);
// todo, can you "forward" a va_args to another actual function? not as far as i can find
//YMStringRef YMStringCreateByAppendingFormat(YMStringRef base, const char *format, ...) YM_VARGS_SENTINEL_REQUIRED __printflike(2,3);
#define YMStringCreateByAppendingFormat(base, format, ...) \
    { \
        YMStringRef baseFormat = YMStringCreateByAppendingString((base), YMStringCreateWithCString(format)); \
        YMStringRef formattedAppend = YMStringCreateWithFormat((format), ##__VA_ARGS__); \
        YMRelease(baseFormat); \
        return formatAppended; \
    }

YMAPI size_t YMStringGetLength(YMStringRef string);
YMAPI const char *YMStringGetCString(YMStringRef string);
YMAPI bool YMStringEquals(YMStringRef stringA, YMStringRef stringB);

#define YMSTR(x) YMStringGetCString(x)
#define YMSTRC(x) YMStringCreateWithCString(x)
#define YMSTRCF(x,...) YMStringCreateWithFormat((x),##__VA_ARGS__,NULL)
#define YMLEN(x) YMStringGetLength(x)

#ifdef __cplusplus
}
#endif

#endif /* YMString_h */
