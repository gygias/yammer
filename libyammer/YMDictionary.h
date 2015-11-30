//
//  YMDictionary.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMDictionary_h
#define YMDictionary_h

#ifdef __cplusplus
extern "C" {
#endif

#include <libyammer/YMBase.h>

typedef const struct __ym_dictionary_t *YMDictionaryRef;

typedef uint64_t YMDictionaryKey;
typedef const void *YMDictionaryValue;

YMAPI YMDictionaryRef YMDictionaryCreate();

YMAPI void YMDictionaryAdd(YMDictionaryRef, YMDictionaryKey key, YMDictionaryValue item);
YMAPI bool YMDictionaryContains(YMDictionaryRef, YMDictionaryKey key);
YMAPI YMDictionaryKey YMDictionaryGetRandomKey(YMDictionaryRef);
YMAPI YMDictionaryValue YMDictionaryGetItem(YMDictionaryRef, YMDictionaryKey key);
YMAPI YMDictionaryValue YMDictionaryRemove(YMDictionaryRef, YMDictionaryKey key);

YMAPI size_t YMDictionaryGetCount(YMDictionaryRef);

typedef struct __YMDictionaryEnum
{
    YMDictionaryKey key;
    YMDictionaryValue value;
} _YMDictionaryEnum;
typedef struct __YMDictionaryEnum *YMDictionaryEnumRef;

// returns NULL if dictionary is empty
YMAPI YMDictionaryEnumRef YMDictionaryEnumeratorBegin(YMDictionaryRef);
// returns NULL if enumeration is complete
YMAPI YMDictionaryEnumRef YMDictionaryEnumeratorGetNext(YMDictionaryEnumRef aEnum);
// call only if cancelling incomplete enumeration, if GetNext returns NULL there is nothing more for the caller to do.
YMAPI void YMDictionaryEnumeratorEnd(YMDictionaryEnumRef aEnum);

#ifdef __cplusplus
}
#endif

#endif /* YMDictionary_h */
