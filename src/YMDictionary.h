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

YMDictionaryRef YMDictionaryCreate();

void YMDictionaryAdd(YMDictionaryRef dict, YMDictionaryKey key, const void *item);
bool YMDictionaryContains(YMDictionaryRef dict, YMDictionaryKey key);
void *YMDictionaryGetItem(YMDictionaryRef dict, YMDictionaryKey key);
void *YMDictionaryRemove(YMDictionaryRef dict, YMDictionaryKey key);

size_t YMDictionaryGetCount(YMDictionaryRef dict);

#endif /* YMDictionary_h */
