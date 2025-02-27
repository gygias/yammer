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

typedef struct __ym_array
{
    _YMType _common;
    
    YMDictionaryRef dict;
    int64_t count;
} __ym_array;
typedef struct __ym_array __ym_array_t;

#define NULL_INDEX (-1)

int64_t __YMArrayFind(__ym_array_t *array, const void *value);

YMArrayRef YMArrayCreate(void)
{
    return YMArrayCreate2(false);
}

YMArrayRef YMArrayCreate2(bool ymtypes)
{
    __ym_array_t *a = (__ym_array_t *)_YMAlloc(_YMArrayTypeID, sizeof(__ym_array_t));
    a->dict = YMDictionaryCreate2(false,ymtypes);
    a->count = 0;
    return a;
}

void _YMArrayFree(YMArrayRef a)
{
    YMRelease(a->dict);
}

void YMArrayAdd(YMArrayRef a, const void *value)
{
    YMDictionaryAdd(a->dict, (YMDictionaryKey)a->count, (YMDictionaryValue)value);
    ((__ym_array_t *)a)->count++;
}

void YMArrayInsert(YMArrayRef a_, int64_t idx, const void *value)
{
    __ym_array_t *a = (__ym_array_t *)a_;
    ymassert(idx<=a->count,"cannot insert at %"PRIu64" in array with count %"PRIu64,idx,a->count);
    
    _YMDictionaryShift(a->dict, idx, true);
    YMDictionaryAdd(a->dict, (YMDictionaryKey)idx, (YMDictionaryValue)value);
    a->count++;
}

void YMArrayReplace(YMArrayRef a_, int64_t idx, const void * value)
{
    __ym_array_t *a = (__ym_array_t *)a_;
    YMDictionaryRemove(a->dict, (YMDictionaryKey)idx);
    YMDictionaryAdd(a->dict, (YMDictionaryKey)idx, (YMDictionaryValue)value);
}

int64_t __YMArrayFind(__ym_array_t *a, const void *value)
{
    int64_t idx = NULL_INDEX;
    YMDictionaryEnumRef dEnum = YMDictionaryEnumeratorBegin(a->dict);
    while ( dEnum ) {
        if ( dEnum->value == value ) {
            idx = (int64_t)dEnum->key;
            break;
        }
        dEnum = YMDictionaryEnumeratorGetNext(dEnum);
    }
    YMDictionaryEnumeratorEnd(dEnum);
    
    return idx;
}

bool YMArrayContains(YMArrayRef a_, const void *value)
{
    __ym_array_t *a = (__ym_array_t *)a_;
    return ( __YMArrayFind(a, value) != NULL_INDEX );
}


int64_t YMArrayIndexOf(YMArrayRef a_, const void *value)
{
    __ym_array_t *a = (__ym_array_t *)a_;
    return __YMArrayFind(a, value);
}


const void *YMArrayGet(YMArrayRef a_, int64_t idx)
{
    __ym_array_t *a = (__ym_array_t *)a_;
    return YMDictionaryGetItem(a->dict, (YMDictionaryKey)idx);
}

int64_t YMArrayGetCount(YMArrayRef a_)
{
    __ym_array_t *a = (__ym_array_t *)a_;
    return a->count;
}

void YMArrayRemove(YMArrayRef a_, int64_t idx)
{
    __ym_array_t *a = (__ym_array_t *)a_;
    YMDictionaryRemove(a->dict, (YMDictionaryKey)idx);
    _YMDictionaryShift(a->dict, idx, false);
    a->count--;
}

void YMArrayRemoveObject(YMArrayRef a_, const void *value)
{
    __ym_array_t *a = (__ym_array_t *)a_;
    
    int64_t idx = __YMArrayFind(a, value);
    ymassert(idx!=NULL_INDEX,"array does not contain object %p",value);
    YMDictionaryRemove(a->dict, (YMDictionaryKey)idx);
    _YMDictionaryShift(a->dict, idx, false);
    a->count--;
}

void _YMArrayRemoveAll(YMArrayRef a_, bool ymRelease, bool free_)
{
    __ym_array_t *a = (__ym_array_t *)a_;
    
    while ( a->count ) {
        int64_t aKey = (int64_t)YMDictionaryGetRandomKey(a->dict);
        const void *object = YMDictionaryGetItem(a->dict, (YMDictionaryKey)aKey);
        if ( ymRelease ) YMRelease(object);
        else if ( free_ ) YMFREE((void *)object);
        YMDictionaryRemove(a->dict, (YMDictionaryKey)aKey);
        a->count--;
    }
}

YM_EXTERN_C_POP
