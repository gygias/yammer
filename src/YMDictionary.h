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

typedef struct _YMDictionary *YMDictionaryRef;

YMDictionaryRef YMDictionaryCreate();

void YMDictionaryAdd(YMDictionaryRef dict, int key, void *item);
bool YMDictionaryContains(YMDictionaryRef dict, int key);
void *YMDictionaryGetItem(YMDictionaryRef dict, int key);
void *YMDictionaryRemove(YMDictionaryRef dict, int key);

size_t YMDictionaryGetCount(YMDictionaryRef dict);

#endif /* YMDictionary_h */
