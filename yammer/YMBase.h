//
//  YMBase.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMBase_h
#define YMBase_h

#ifndef WIN32
#define YM_VARGS_SENTINEL_REQUIRED __attribute__((sentinel(0,1)))
#define YM_WPPUSH \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wpedantic\"")
#define YM_WPOP \
    _Pragma("GCC diagnostic pop")
#else
#define YM_VARGS_SENTINEL_REQUIRED
#define __printflike(x,y)
#define YM_WPPUSH
#define YM_WPOP
#endif

typedef const void *YMTypeRef;
typedef char YMTypeID;

YMTypeRef YMRetain(YMTypeRef object);
YMTypeRef YMAutorelease(YMTypeRef object);
void YMRelease(YMTypeRef object);

typedef enum
{
    YMIOSuccess = 1,
    YMIOEOF = 0,
    YMIOError = -1
} YMIOResult;

#endif /* YMBase_h */
