//
//  YMDictionary.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMDictionary_h
#define YMDictionary_h

#include "YMBase.h"

typedef struct __YMDictionary *YMDictionaryRef;

typedef uint64_t YMDictionaryKey;
typedef const void *YMDictionaryValue;

YMDictionaryRef YMDictionaryCreate();

void YMDictionaryAdd(YMDictionaryRef, YMDictionaryKey key, YMDictionaryValue item);
bool YMDictionaryContains(YMDictionaryRef, YMDictionaryKey key);
YMDictionaryValue YMDictionaryGetItem(YMDictionaryRef, YMDictionaryKey key);
YMDictionaryValue YMDictionaryRemove(YMDictionaryRef, YMDictionaryKey key);

size_t YMDictionaryGetCount(YMDictionaryRef);

typedef struct __YMDictionaryEnum
{
    YMDictionaryKey key;
    YMDictionaryValue value;
} _YMDictionaryEnum;
typedef struct __YMDictionaryEnum *YMDictionaryEnumRef;

// returns NULL if dictionary is empty
YMDictionaryEnumRef YMDictionaryEnumeratorBegin(YMDictionaryRef);
// returns NULL if enumeration is complete
YMDictionaryEnumRef YMDictionaryEnumeratorGetNext(YMDictionaryEnumRef aEnum);
// call only if cancelling incomplete enumeration, if GetNext returns NULL there is nothing more for the caller to do.
void YMDictionaryEnumeratorEnd(YMDictionaryEnumRef aEnum);

bool YMDictionaryPopKeyValue(YMDictionaryRef, bool last, YMDictionaryKey *outKey, YMDictionaryValue *outValue);

#endif /* YMDictionary_h */
