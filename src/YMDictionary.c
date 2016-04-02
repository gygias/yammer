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
# define CHECK_CONSISTENCY { if ( ( dict->head == NULL ) ^ ( dict->count == 0 ) ) { abort(); } }
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

typedef struct __ym_dictionary_t
{
    _YMType _type;
    
    _YMDictionaryItemRef head;
    size_t count;
    bool ymtypeKeys;
    bool ymtypeValues;
} __ym_dictionary_t;
typedef struct __ym_dictionary_t *__YMDictionaryRef;

_YMDictionaryItemRef _YMDictionaryFindItemWithIdentifier(_YMDictionaryItemRef head, YMDictionaryKey key, bool ymtypeKey, _YMDictionaryItemRef *outPreviousItem);
_YMDictionaryItemRef _YMDictionaryCopyItem(_YMDictionaryItemRef item);

YMDictionaryRef __YMDictionaryCreate(bool ymtypeKeys, bool ymtypeValues)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)_YMAlloc(_YMDictionaryTypeID, sizeof(struct __ym_dictionary_t));
    
    dict->head = NULL;
    dict->count = 0;
    dict->ymtypeKeys = ymtypeKeys;
    dict->ymtypeValues = ymtypeValues;
    
    return (YMDictionaryRef)dict;
}

YMDictionaryRef YMDictionaryCreate()
{
    return __YMDictionaryCreate(false, false);
}

YMDictionaryRef YMAPI YMDictionaryCreate2(bool ymtypeKeys, bool ymtypeValues)
{
    return __YMDictionaryCreate(ymtypeKeys, ymtypeValues);
}

void _YMDictionaryFree(YMTypeRef object)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)object;
    // we don't assume ownership of dict members
    _YMDictionaryItemRef itemIter = dict->head;
    
    while (itemIter) {
        _YMDictionaryItemRef thisItem = itemIter;
        if ( dict->ymtypeKeys )
            YMRelease((YMTypeRef)itemIter->key);
        if ( dict->ymtypeValues )
            YMRelease((YMTypeRef)itemIter->value);
        itemIter = itemIter->next;
        free(thisItem);
    }
}

void YMDictionaryAdd(YMDictionaryRef dict_, YMDictionaryKey key, YMDictionaryValue value)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)dict_;
    CHECK_CONSISTENCY
    
    YM_WPPUSH
#if defined(YMAPPLE)
    bool full = ( dict->count == MAX_OF(typeof(dict->count)) );
#else
	bool full = ( dict->count == ULONG_MAX );
#endif
    YM_WPOP
    
    if ( full )
        ymabort("YMDictionary is full");
    
    if ( _YMDictionaryFindItemWithIdentifier(dict->head, key, dict->ymtypeKeys, NULL) )
        ymabort("YMDictionary already contains item for key %p",key);

    _YMDictionaryItemRef newItem = (_YMDictionaryItemRef)YMALLOC(sizeof(struct __YMDictionaryItem));
    newItem->key = dict->ymtypeKeys ? (YMDictionaryKey)YMRetain((YMTypeRef)key) : key;
    newItem->value = dict->ymtypeValues ? (YMDictionaryValue)YMRetain((YMTypeRef)value) : value;
    newItem->next = dict->head; // nulls or prepends
    dict->head = newItem;
    
    dict->count++;
}

bool YMDictionaryContains(YMDictionaryRef dict_, YMDictionaryKey key)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)dict_;
    CHECK_CONSISTENCY
    if ( dict->head == NULL )
        return false;
    return ( NULL != _YMDictionaryFindItemWithIdentifier(dict->head, key, dict->ymtypeKeys, NULL) );
}

YMDictionaryKey YMDictionaryGetRandomKey(YMDictionaryRef dict_)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)dict_;
    CHECK_CONSISTENCY
    if ( dict->count == 0 || dict->head == NULL )
        ymabort("YMDictionary is empty and has no keys");
    
    uint32_t chosenIdx = arc4random_uniform((uint32_t)dict->count), countdown = chosenIdx; // unsure of portability
    _YMDictionaryItemRef iter = dict->head;
    while (countdown-- != 0)
        iter = iter->next;
    return iter->key;
}

YMDictionaryValue YMDictionaryGetItem(YMDictionaryRef dict_, YMDictionaryKey key)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)dict_;
    CHECK_CONSISTENCY
    _YMDictionaryItemRef foundItem = _YMDictionaryFindItemWithIdentifier(dict->head, key, dict->ymtypeKeys, NULL);
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

YMDictionaryValue YMDictionaryRemove(YMDictionaryRef dict_, YMDictionaryKey key)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)dict_;
    CHECK_CONSISTENCY
    
    if ( dict->count == 0 || dict->head == NULL ) {
        ymabort("YMDictionary is empty");
    }
    
    YMDictionaryValue outValue = NULL;
    _YMDictionaryItemRef previousItem = NULL;
    _YMDictionaryItemRef theItem = _YMDictionaryFindItemWithIdentifier(dict->head, key, dict->ymtypeKeys, &previousItem);
    if ( ! theItem ) {
        ymabort("key does not exist to remove");
    } else {
        if ( previousItem )
            previousItem->next = theItem->next;
        else // removed item is head
            dict->head = theItem->next;
        dict->count--;
        
        outValue = theItem->value;
    }
    
    CHECK_CONSISTENCY
    
    if ( dict->ymtypeKeys )
        YMRelease((YMTypeRef)theItem->key);
    if ( dict->ymtypeValues )
        YMRelease((YMTypeRef)theItem->value);
    
    free(theItem);
    return outValue;
}

size_t YMDictionaryGetCount(YMDictionaryRef dict_)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)dict_;
    CHECK_CONSISTENCY
    return dict->count;
}

YMDictionaryEnumRef YMDictionaryEnumeratorBegin(YMDictionaryRef dict_)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)dict_;
    CHECK_CONSISTENCY
    
    if ( ! dict->head )
        return NULL;
    
#ifdef YM_DICT_MAYHAPS_SAFE_ENUM
    
    CHECK_CONSISTENCY
    return (YMDictionaryEnumRef)_YMDictionaryCopyItem(dict->head);
#else
    CHECK_CONSISTENCY
    return (YMDictionaryEnumRef)dict->head;
#endif
}

YMDictionaryEnumRef YMDictionaryEnumeratorGetNext(YMDictionaryEnumRef aEnum)
{
#ifdef YM_DICT_MAYHAPS_SAFE_ENUM
    _YMDictionaryItemRef item = (_YMDictionaryItemRef)aEnum; // overlapping
    _YMDictionaryItemRef next = item->next;
    
    free(item);
    
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
    free(aEnum);
#endif
}

bool __Broken_YMDictionaryPopKeyValue(YMDictionaryRef dict, bool last, YMDictionaryKey *outKey, YMDictionaryValue *outValue);
bool __Broken_YMDictionaryPopKeyValue(YMDictionaryRef dict_, bool last, YMDictionaryKey *outKey, YMDictionaryValue *outValue)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)dict_;
    CHECK_CONSISTENCY
    
    _YMDictionaryItemRef outItem = dict->head,
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
        if ( outItem == dict->head )
            dict->head = NULL;
        else
            previous->next = NULL;
    }
    else
        dict->head = outItem->next;
    
    if ( dict->count == 0 )
        ymabort("ymdictionary is broken");
    
    dict->count--;
    
    free(outItem);
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

void _YMDictionaryShift(YMDictionaryRef dict_, int64_t baseIdx, bool inc)
{
    __YMDictionaryRef dict = (__YMDictionaryRef)dict_;
    CHECK_CONSISTENCY
    
    _YMDictionaryItemRef iter = dict->head;
    
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
