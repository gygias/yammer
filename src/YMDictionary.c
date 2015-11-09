//
//  YMDictionary.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMDictionary.h"

#include "YMPrivate.h"
#include "YMUtilities.h"

#undef ymlogType
#define ymlogType YMLogDefault
#include "YMLog.h"

typedef struct __YMDictionaryItem
{
    uint64_t key;
    const void *value;
    struct __YMDictionaryItem *next;
} _YMDictionaryItem;
typedef _YMDictionaryItem *_YMDictionaryItemRef;

typedef struct __YMDictionary
{
    YMTypeID _typeID;
    
    bool isYMTypeDict;
    _YMDictionaryItemRef head;
    size_t count;
} _YMDictionary;

_YMDictionaryItemRef _YMDictionaryFindItemWithIdentifier(_YMDictionaryItemRef head, YMDictionaryKey key, _YMDictionaryItemRef *outPreviousItem);
_YMDictionaryItemRef _YMDictionaryCopyItem(_YMDictionaryItemRef item);

YMDictionaryRef YMDictionaryCreate()
{
    YMDictionaryRef dict = (YMDictionaryRef)YMMALLOC(sizeof(struct __YMDictionary));
    dict->_typeID = _YMDictionaryTypeID;
    
    dict->head = NULL;
    dict->count = 0;
    
    return (YMDictionaryRef)dict;
}

void _YMDictionaryFree(YMTypeRef object)
{
    YMDictionaryRef dict = (YMDictionaryRef)object;
    // we don't assume ownership of dict members
    _YMDictionaryItemRef itemIter = dict->head;
    
    while (itemIter)
    {
        _YMDictionaryItemRef thisItem = itemIter;
        itemIter = itemIter->next;
        free(thisItem);
    }
    
    free(dict);
}

void YMDictionaryAdd(YMDictionaryRef dict, YMDictionaryKey key, YMDictionaryValue value)
{
    if ( dict->count == MAX_OF(typeof(dict->count)) )
    {
        ymerr("error: YMDictionary is full");
        abort();
    }
    
    if ( _YMDictionaryFindItemWithIdentifier(dict->head, key, NULL) )
    {
        ymerr("error: YMDictionary already contains item for key %llu",key);
        abort();
    }
    _YMDictionaryItemRef newItem = (_YMDictionaryItemRef)YMMALLOC(sizeof(struct __YMDictionaryItem));
    newItem->key = key;
    newItem->value = value;
    newItem->next = dict->head; // nulls or prepends
    dict->head = newItem;
    
    dict->count++;
}

bool YMDictionaryContains(YMDictionaryRef dict, YMDictionaryKey key)
{
    return ( NULL != _YMDictionaryFindItemWithIdentifier(dict->head, key, NULL) );
}

YMDictionaryKey YMDictionaryRandomKey(YMDictionaryRef dict)
{
    if ( dict->count == 0 || dict->head == NULL )
    {
        ymerr("error: YMDictionary is empty and has no keys");
        abort();
    }
    
    uint32_t chosenIdx = arc4random_uniform((uint32_t)dict->count), countdown = chosenIdx; // unsure of portability
    _YMDictionaryItemRef iter = dict->head;
    while (countdown-- != 0)
        iter = iter->next;
    return iter->key;
}

YMDictionaryValue YMDictionaryGetItem(YMDictionaryRef dict, YMDictionaryKey key)
{
    _YMDictionaryItemRef foundItem = _YMDictionaryFindItemWithIdentifier(dict->head, key, NULL);
    if ( foundItem )
        return foundItem->value;
    return NULL;
}

_YMDictionaryItemRef _YMDictionaryFindItemWithIdentifier(_YMDictionaryItemRef head, YMDictionaryKey key, _YMDictionaryItemRef *outPreviousItem)
{
    _YMDictionaryItemRef itemIter = head,
        previousItem = NULL;
    
    while (itemIter)
    {
        if ( itemIter->key == key )
            break;
        
        previousItem = itemIter;
        itemIter = (_YMDictionaryItemRef)itemIter->next;
    }
    
    if ( outPreviousItem )
        *outPreviousItem = previousItem;
    
    return itemIter;
}

YMDictionaryValue YMDictionaryRemove(YMDictionaryRef dict, YMDictionaryKey key)
{
    if ( dict->count == 0 || dict->head == NULL )
    {
        ymerr("error: YMDictionary is empty");
        abort();
    }
    
    YMDictionaryValue outValue = NULL;
    _YMDictionaryItemRef previousItem = NULL;
    _YMDictionaryItemRef theItem = _YMDictionaryFindItemWithIdentifier(dict->head, key, &previousItem);
    if ( ! theItem )
    {
        ymerr("error: key does not exist to remove");
        abort();
    }
    else
    {
        if ( previousItem )
            previousItem->next = theItem->next;
        else // removed item is head
            dict->head = theItem->next;
        dict->count--;
        
        outValue = theItem->value;
        free(theItem);
    }
    
    if ( dict->count == 0 ^ dict->head == NULL )
        abort();
    
    return outValue;
}

size_t YMDictionaryGetCount(YMDictionaryRef dict)
{
    _YMDictionary *_dict = (_YMDictionary *)dict;
    return _dict->count;
}

YMDictionaryEnumRef YMDictionaryEnumeratorBegin(YMDictionaryRef dict)
{
#ifdef YM_DICT_MAYBE_SAFE_ENUM
    if ( ! dict->head )
        return NULL;
    
    return (YMDictionaryEnumRef)_YMDictionaryCopyItem(dict->head);
#else
    return (YMDictionaryEnumRef)dict->head;
#endif
}

YMDictionaryEnumRef YMDictionaryEnumeratorGetNext(YMDictionaryEnumRef aEnum)
{
#ifdef YM_DICT_MAYBE_SAFE_ENUM
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
#ifdef YM_DICT_MAYBE_SAFE_ENUM
    free(aEnum);
#endif
}

bool __Broken_YMDictionaryPopKeyValue(YMDictionaryRef dict, bool last, YMDictionaryKey *outKey, YMDictionaryValue *outValue);
bool __Broken_YMDictionaryPopKeyValue(YMDictionaryRef dict, bool last, YMDictionaryKey *outKey, YMDictionaryValue *outValue)
{    
    _YMDictionaryItemRef outItem = dict->head,
                            previous = NULL;
    
    if ( ! outItem )
        return false;
    
    if ( last )
    {
        while ( outItem->next )
        {
            previous = outItem;
            outItem = outItem->next;
        }
    }
    
    if ( outKey )
        *outKey = outItem->key;
    if ( outValue )
        *outValue = outItem->value;
    
    if ( last )
    {
        if ( outItem == dict->head )
            dict->head = NULL;
        else
            previous->next = NULL;
    }
    else
        dict->head = outItem->next;
    
    if ( dict->count == 0 )
    {
        ymlog("ymdictionary is broken");
        abort();
    }
    
    dict->count--;
    
    free(outItem);
    return true;
}

_YMDictionaryItemRef _YMDictionaryCopyItem(_YMDictionaryItemRef item)
{
    _YMDictionaryItemRef itemCopy = (_YMDictionaryItemRef)YMMALLOC(sizeof(struct __YMDictionaryItem));
    itemCopy->key = item->key;
    itemCopy->value = item->value;
    itemCopy->next = item->next;
    return itemCopy;
}
