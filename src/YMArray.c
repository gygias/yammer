//
//  YMArray.c
//  yammer
//
//  Created by david on 12/8/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMArray.h"

#include "YMDictionaryPriv.h"

#define ymlog_type YMLogDefault
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_array_t
{
    _YMType _type;
    
    YMDictionaryRef dict;
    int64_t count;
} __ym_array_t;
typedef struct __ym_array_t *__YMArrayRef;

#define NULL_INDEX (-1)

int64_t __YMArrayFind(__YMArrayRef array, const void *value);

YMArrayRef YMAPI YMArrayCreate()
{
    __YMArrayRef array = (__YMArrayRef)_YMAlloc(_YMArrayTypeID, sizeof(struct __ym_array_t));
    array->dict = YMDictionaryCreate();
    array->count = 0;
    return array;
}

void _YMArrayFree(YMArrayRef array_)
{
    __YMArrayRef array = (__YMArrayRef)array_;
    YMRelease(array->dict);
}

void YMAPI YMArrayAdd(YMArrayRef array_, const void *value)
{
    __YMArrayRef array = (__YMArrayRef)array_;
    YMDictionaryAdd(array->dict, array->count, (YMDictionaryValue)value);
    array->count++;
}

void YMAPI YMArrayInsert(YMArrayRef array_, int64_t idx, const void *value)
{
    __YMArrayRef array = (__YMArrayRef)array_;
    ymassert(idx<=array->count,"cannot insert at %llu in array with count %llu",idx,array->count);
    
    _YMDictionaryShift(array->dict, idx);
    YMDictionaryAdd(array->dict, idx, (YMDictionaryValue)value);
    array->count++;
}

void YMArrayReplace(YMArrayRef array_, int64_t idx, const void * value)
{
    __YMArrayRef array = (__YMArrayRef)array_;
    YMDictionaryRemove(array->dict, idx);
    YMDictionaryAdd(array->dict, idx, (YMDictionaryValue)value);
}

int64_t __YMArrayFind(__YMArrayRef array, const void *value)
{
    int64_t idx = NULL_INDEX;
    YMDictionaryEnumRef dEnum = YMDictionaryEnumeratorBegin(array->dict);
    while ( dEnum ) {
        if ( dEnum->value == value ) {
            idx = dEnum->key;
            break;
        }
        dEnum = YMDictionaryEnumeratorGetNext(dEnum);
    }
    YMDictionaryEnumeratorEnd(dEnum);
    
    return idx;
}

bool YMAPI YMArrayContains(YMArrayRef array_, const void *value)
{
    __YMArrayRef array = (__YMArrayRef)array_;
    
    return ( __YMArrayFind(array, value) != NULL_INDEX );
}


int64_t YMAPI YMArrayIndexOf(YMArrayRef array_, const void *value)
{
    __YMArrayRef array = (__YMArrayRef)array_;
    return __YMArrayFind(array, value);
}


const YMAPI void *YMArrayGet(YMArrayRef array_, int64_t idx)
{
    __YMArrayRef array = (__YMArrayRef)array_;
    return YMDictionaryGetItem(array->dict, idx);
}

int64_t YMAPI YMArrayGetCount(YMArrayRef array_)
{
    __YMArrayRef array = (__YMArrayRef)array_;
    return array->count;
}

void YMArrayRemove(YMArrayRef array_, int64_t idx)
{
    __YMArrayRef array = (__YMArrayRef)array_;
    YMDictionaryRemove(array->dict, idx);
}

void YMAPI YMArrayRemoveObject(YMArrayRef array_, const void *value)
{
    __YMArrayRef array = (__YMArrayRef)array_;
    
    int64_t idx = __YMArrayFind(array, value);
    ymassert(idx!=NULL_INDEX,"array does not contain object %p",value);
    YMDictionaryRemove(array->dict, idx);
}

YM_EXTERN_C_POP