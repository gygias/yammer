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

YMArrayRef YMArrayCreate();

void YMArrayAdd(YMArrayRef, const void *);
void YMArrayInsert(YMArrayRef, int64_t, const void *);
void YMArrayReplace(YMArrayRef, int64_t, const void *);
bool YMArrayContains(YMArrayRef, const void *);
int64_t YMArrayIndexOf(YMArrayRef, const void *);
const void *YMArrayGet(YMArrayRef, int64_t);
int64_t YMArrayGetCount(YMArrayRef);
void YMArrayRemove(YMArrayRef, int64_t);
void YMArrayRemoveObject(YMArrayRef, const void *);

YM_EXTERN_C_POP


#endif /* YMArray_h */
