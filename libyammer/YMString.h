//
//  YMString.h
//  yammer
//
//  Created by david on 11/13/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMString_h
#define YMString_h

typedef const struct __ym_string *YMStringRef;

YMStringRef YMStringCreate();
YMStringRef YMStringCreateWithCString(const char *cString);
YMStringRef YMStringCreateWithFormat(const char *format,...) YM_VARGS_SENTINEL_REQUIRED __printflike(1, 2);
YMStringRef YMStringCreateByAppendingString(YMStringRef base, YMStringRef append);
// todo, can you "forward" a va_args to another actual function? not as far as i can find
//YMStringRef YMStringCreateByAppendingFormat(YMStringRef base, const char *format, ...) YM_VARGS_SENTINEL_REQUIRED __printflike(2,3);
#define YMStringCreateByAppendingFormat(base, format, ...) \
    { \
        YMStringRef baseFormat = YMStringCreateByAppendingString((base), YMStringCreateWithCString(format)); \
        YMStringRef formattedAppend = YMStringCreateWithFormat((format), ##__VA_ARGS__); \
        YMRelease(baseFormat); \
        return formatAppended; \
    }

size_t YMStringGetLength(YMStringRef string);
const char *YMStringGetCString(YMStringRef string);
bool YMStringEquals(YMStringRef stringA, YMStringRef stringB);

#define YMSTR(x) YMStringGetCString(x)
#define YMSTRC(x) YMStringCreateWithCString(x)
// Token pasting of ',' and __VA_ARGS__ is a GNU extension
YM_WPPUSH
#define YMSTRCF(x,...) YMStringCreateWithFormat((x),##__VA_ARGS__)
YM_WPOP
#define YMLEN(x) YMStringGetLength(x)

#endif /* YMString_h */
