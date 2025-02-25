//
//  YMDictionary.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMDictionary.h"

#include "YMUtilities.h"

#include "YMLog.h"

YM_EXTERN_C_PUSH

#ifdef YMDEBUG
# define CHECK_CONSISTENCY { if ( ( d->head == NULL ) ^ ( d->count == 0 ) ) { ymabort("ymdict consistency check failed."); } }
#else
# define CHECK_CONSISTENCY
#endif

typedef struct __YMDictionaryItem
{
    YMDictionaryKey key;
    YMDictionaryValue value;
    struct __YMDictionaryItem *next;
} _YMDictionaryItem;
typedef _YMDictionaryItem *_YMDictionaryItemRef;

typedef struct __ym_dictionary
{
    _YMType _common;
    
    _YMDictionaryItemRef head;
    size_t count;
    bool ymtypeKeys;
    bool ymtypeValues;
} __ym_dictionary;
typedef struct __ym_dictionary __ym_dictionary_t;

_YMDictionaryItemRef _YMDictionaryFindItemWithIdentifier(_YMDictionaryItemRef head, YMDictionaryKey key, bool ymtypeKey, _YMDictionaryItemRef *outPreviousItem);
_YMDictionaryItemRef _YMDictionaryCopyItem(_YMDictionaryItemRef item);

YMDictionaryRef __YMDictionaryCreate(bool ymtypeKeys, bool ymtypeValues)
{
    __ym_dictionary_t *d = (__ym_dictionary_t *)_YMAlloc(_YMDictionaryTypeID, sizeof(__ym_dictionary_t));
    
    d->head = NULL;
    d->count = 0;
    d->ymtypeKeys = ymtypeKeys;
    d->ymtypeValues = ymtypeValues;
    
    return d;
}

YMDictionaryRef YMDictionaryCreate(void)
{
    return __YMDictionaryCreate(false, false);
}

YMDictionaryRef YMAPI YMDictionaryCreate2(bool ymtypeKeys, bool ymtypeValues)
{
    return __YMDictionaryCreate(ymtypeKeys, ymtypeValues);
}

void _YMDictionaryFree(YMTypeRef d_)
{
    __ym_dictionary_t *d = (__ym_dictionary_t *)d_;
    // we don't assume ownership of dict members
    _YMDictionaryItemRef itemIter = d->head;
    
    while (itemIter) {
        _YMDictionaryItemRef thisItem = itemIter;
        if ( d->ymtypeKeys )
            YMRelease((YMTypeRef)itemIter->key);
        if ( d->ymtypeValues )
            YMRelease((YMTypeRef)itemIter->value);
        itemIter = itemIter->next;
        YMFREE(thisItem);
    }
}

void YMDictionaryAdd(YMDictionaryRef d, YMDictionaryKey key, YMDictionaryValue value)
{
    CHECK_CONSISTENCY
    
    YM_WPPUSH
#if defined(YMAPPLE)
    bool full = ( d->count == MAX_OF(typeof(d->count)) );
#else
	bool full = ( d->count == ULONG_MAX );
#endif
    YM_WPOP
    
    if ( full )
        ymabort("YMDictionary is full");
    
    if ( _YMDictionaryFindItemWithIdentifier(d->head, key, d->ymtypeKeys, NULL) )
        ymabort("YMDictionary already contains item for key %p",key);

    _YMDictionaryItemRef newItem = (_YMDictionaryItemRef)YMALLOC(sizeof(struct __YMDictionaryItem));
    newItem->key = d->ymtypeKeys ? (YMDictionaryKey)YMRetain((YMTypeRef)key) : key;
    newItem->value = d->ymtypeValues ? (YMDictionaryValue)YMRetain((YMTypeRef)value) : value;
    newItem->next = d->head; // nulls or prepends
    ((__ym_dictionary_t *)d)->head = newItem;
    
    ((__ym_dictionary_t *)d)->count++;
}

bool YMDictionaryContains(YMDictionaryRef d, YMDictionaryKey key)
{
    CHECK_CONSISTENCY
    if ( d->head == NULL )
        return false;
    return ( NULL != _YMDictionaryFindItemWithIdentifier(d->head, key, d->ymtypeKeys, NULL) );
}

YMDictionaryKey YMDictionaryGetRandomKey(YMDictionaryRef d)
{
    CHECK_CONSISTENCY
    if ( d->count == 0 || d->head == NULL )
        ymabort("YMDictionary is empty and has no keys");
    
    uint32_t chosenIdx = arc4random_uniform((uint32_t)d->count), countdown = chosenIdx; // unsure of portability
    _YMDictionaryItemRef iter = d->head;
    while (countdown-- != 0)
        iter = iter->next;
    return iter->key;
}

YMDictionaryValue YMDictionaryGetItem(YMDictionaryRef d, YMDictionaryKey key)
{
    CHECK_CONSISTENCY
    _YMDictionaryItemRef foundItem = _YMDictionaryFindItemWithIdentifier(d->head, key, d->ymtypeKeys, NULL);
    if ( foundItem )
        return foundItem->value;
    return NULL;
}

_YMDictionaryItemRef _YMDictionaryFindItemWithIdentifier(_YMDictionaryItemRef head, YMDictionaryKey key, bool ymtypeKeys, _YMDictionaryItemRef *outPreviousItem)
{
    _YMDictionaryItemRef itemIter = head,
        previousItem = NULL;
    
    while (itemIter) {
        if ( ymtypeKeys && YMIsEqual((YMTypeRef)key, (YMTypeRef)itemIter->key) )
            break;
        else if ( itemIter->key == key )
            break;
        
        previousItem = itemIter;
        itemIter = (_YMDictionaryItemRef)itemIter->next;
    }
    
    if ( outPreviousItem )
        *outPreviousItem = previousItem;
    
    return itemIter;
}

YMDictionaryValue YMDictionaryRemove(YMDictionaryRef d, YMDictionaryKey key)
{
    CHECK_CONSISTENCY
    
    if ( d->count == 0 || d->head == NULL ) {
        ymabort("YMDictionary is empty");
    }
    
    YMDictionaryValue outValue = NULL;
    _YMDictionaryItemRef previousItem = NULL;
    _YMDictionaryItemRef theItem = _YMDictionaryFindItemWithIdentifier(d->head, key, d->ymtypeKeys, &previousItem);
    if ( ! theItem ) {
        ymabort("key does not exist to remove");
    } else {
        if ( previousItem )
            previousItem->next = theItem->next;
        else // removed item is head
            ((__ym_dictionary_t *)d)->head = theItem->next;
        ((__ym_dictionary_t *)d)->count--;
        
        outValue = theItem->value;
    }
    
    CHECK_CONSISTENCY
    
    if ( d->ymtypeKeys )
        YMRelease((YMTypeRef)theItem->key);
    if ( d->ymtypeValues )
        YMRelease((YMTypeRef)theItem->value);
    
    YMFREE(theItem);
    return outValue;
}

size_t YMDictionaryGetCount(YMDictionaryRef d)
{
    CHECK_CONSISTENCY
    return d->count;
}

YMDictionaryEnumRef YMDictionaryEnumeratorBegin(YMDictionaryRef d)
{
    CHECK_CONSISTENCY
    
    if ( ! d->head )
        return NULL;
    
#ifdef YM_DICT_MAYHAPS_SAFE_ENUM
    
    CHECK_CONSISTENCY
    return (YMDictionaryEnumRef)_YMDictionaryCopyItem(d->head);
#else
    CHECK_CONSISTENCY
    return (YMDictionaryEnumRef)d->head;
#endif
}

YMDictionaryEnumRef YMDictionaryEnumeratorGetNext(YMDictionaryEnumRef aEnum)
{
#ifdef YM_DICT_MAYHAPS_SAFE_ENUM
    _YMDictionaryItemRef item = (_YMDictionaryItemRef)aEnum; // overlapping
    _YMDictionaryItemRef next = item->next;
    
    YMFREE(item);
    
    if ( ! next )
        return NULL;
    
    return (YMDictionaryEnumRef)_YMDictionaryCopyItem(next);
#else
    return (YMDictionaryEnumRef)((_YMDictionaryItemRef)aEnum)->next;
#endif
}

void YMDictionaryEnumeratorEnd(__unused YMDictionaryEnumRef aEnum) // xxx
{
#ifdef YM_DICT_MAYHAPS_SAFE_ENUM
    YMFREE(aEnum);
#endif
}

bool __Broken_YMDictionaryPopKeyValue(YMDictionaryRef d, bool last, YMDictionaryKey *outKey, YMDictionaryValue *outValue);
bool __Broken_YMDictionaryPopKeyValue(YMDictionaryRef d, bool last, YMDictionaryKey *outKey, YMDictionaryValue *outValue)
{
    CHECK_CONSISTENCY
    
    _YMDictionaryItemRef outItem = d->head,
                            previous = NULL;
    
    if ( ! outItem )
        return false;
    
    if ( last ) {
        while ( outItem->next ) {
            previous = outItem;
            outItem = outItem->next;
        }
    }
    
    if ( outKey )
        *outKey = outItem->key;
    if ( outValue )
        *outValue = outItem->value;
    
    if ( last ) {
        if ( outItem == d->head )
            ((__ym_dictionary_t *)d)->head = NULL;
        else
            previous->next = NULL;
    }
    else
        ((__ym_dictionary_t *)d)->head = outItem->next;
    
    if ( d->count == 0 )
        ymabort("ymdictionary is broken");
    
    ((__ym_dictionary_t *)d)->count--;
    
    YMFREE(outItem);
    return true;
}

_YMDictionaryItemRef _YMDictionaryCopyItem(_YMDictionaryItemRef item)
{
    _YMDictionaryItemRef itemCopy = (_YMDictionaryItemRef)YMALLOC(sizeof(struct __YMDictionaryItem));
    itemCopy->key = item->key;
    itemCopy->value = item->value;
    itemCopy->next = item->next;
    return itemCopy;
}

void _YMDictionaryShift(YMDictionaryRef d, int64_t baseIdx, bool inc)
{
    CHECK_CONSISTENCY
    
    _YMDictionaryItemRef iter = d->head;
    
    while ( iter ) {
		int64_t litKey = (int64_t)iter->key;
        if ( litKey >= baseIdx ) { //xxx
			litKey += inc ? 1 : -1;
			iter->key = (YMDictionaryKey)litKey;
        }
        
        iter = iter->next;
    }
}

YM_EXTERN_C_POP
