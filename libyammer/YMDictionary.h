//
//  YMDictionary.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMDictionary_h
#define YMDictionary_h

YM_EXTERN_C_PUSH

#include <libyammer/YMBase.h>

typedef const struct __ym_dictionary_t *YMDictionaryRef;

typedef uint64_t YMDictionaryKey;
typedef void *YMDictionaryValue;

YMDictionaryRef YMAPI YMDictionaryCreate();

void YMAPI YMDictionaryAdd(YMDictionaryRef, YMDictionaryKey key, YMDictionaryValue item);
bool YMAPI YMDictionaryContains(YMDictionaryRef, YMDictionaryKey key);
YMDictionaryKey YMAPI YMDictionaryGetRandomKey(YMDictionaryRef);
YMDictionaryValue YMAPI YMDictionaryGetItem(YMDictionaryRef, YMDictionaryKey key);
YMDictionaryValue YMAPI YMDictionaryRemove(YMDictionaryRef, YMDictionaryKey key);

size_t YMAPI YMDictionaryGetCount(YMDictionaryRef);

typedef struct __YMDictionaryEnum
{
    YMDictionaryKey key;
    YMDictionaryValue value;
} _YMDictionaryEnum;
typedef struct __YMDictionaryEnum *YMDictionaryEnumRef;

// returns NULL if dictionary is empty
YMDictionaryEnumRef YMAPI YMDictionaryEnumeratorBegin(YMDictionaryRef);
// returns NULL if enumeration is complete
YMDictionaryEnumRef YMAPI YMDictionaryEnumeratorGetNext(YMDictionaryEnumRef aEnum);
// call only if cancelling incomplete enumeration, if GetNext returns NULL there is nothing more for the caller to do.
void YMAPI YMDictionaryEnumeratorEnd(YMDictionaryEnumRef aEnum);

YM_EXTERN_C_POP

#endif /* YMDictionary_h */
