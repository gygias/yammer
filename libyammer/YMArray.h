//
//  YMArray.h
//  yammer
//
//  Created by david on 12/8/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMArray_h
#define YMArray_h

#include "YMBase.h"

YM_EXTERN_C_PUSH

typedef const struct __ym_array_t *YMArrayRef;

YMArrayRef YMAPI YMArrayCreate();

void YMAPI YMArrayAdd(YMArrayRef, const void *);
void YMAPI YMArrayInsert(YMArrayRef, int64_t, const void *);
void YMAPI YMArrayReplace(YMArrayRef, int64_t, const void *);
bool YMAPI YMArrayContains(YMArrayRef, const void *);
int64_t YMAPI YMArrayIndexOf(YMArrayRef, const void *);
const YMAPI void *YMArrayGet(YMArrayRef, int64_t);
int64_t YMAPI YMArrayGetCount(YMArrayRef);
void YMAPI YMArrayRemove(YMArrayRef, int64_t);
void YMAPI YMArrayRemoveObject(YMArrayRef, const void *);

void YMAPI _YMArrayRemoveAll(YMArrayRef array_, bool ymRelease, bool free);

YM_EXTERN_C_POP


#endif /* YMArray_h */
