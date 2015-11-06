//
//  YMDictionary.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMDictionary.h"

#include "YMPrivate.h"

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
    YMDictionaryRef dict = (YMDictionaryRef)malloc(sizeof(struct __YMDictionary));
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

void YMDictionaryAdd(YMDictionaryRef dict, YMDictionaryKey key, YMDictionaryValue item)
{
    if ( _YMDictionaryFindItemWithIdentifier(dict->head, key, NULL) )
    {
        YMLog("error: YMDictionary already contains item for key %llu",key);
        abort();
    }
    _YMDictionaryItemRef newItem = (_YMDictionaryItemRef)malloc(sizeof(struct __YMDictionaryItem));
    newItem->value = item;
    newItem->next = dict->head; // nulls or prepends
    if ( ! dict->head )
        dict->head = newItem;    
    
    dict->count++;
}

bool YMDictionaryContains(YMDictionaryRef dict, YMDictionaryKey key)
{
    return ( NULL != _YMDictionaryFindItemWithIdentifier(dict->head, key, NULL) );
}

YMDictionaryValue YMDictionaryGetItem(YMDictionaryRef dict, YMDictionaryKey key)
{
    return _YMDictionaryFindItemWithIdentifier(dict->head, key, NULL);
}

_YMDictionaryItemRef _YMDictionaryFindItemWithIdentifier(_YMDictionaryItemRef head, YMDictionaryKey key, _YMDictionaryItemRef *outPreviousItem)
{
    _YMDictionaryItemRef itemIter = head,
        previousItem = NULL;
    
    while (itemIter)
    {
        _YMDictionaryItemRef thisItem = itemIter;
        if ( thisItem->key == key )
            return thisItem;
        previousItem = itemIter;
        itemIter = (_YMDictionaryItemRef)itemIter->next;
    }
    
    if ( outPreviousItem )
        *outPreviousItem = previousItem;
    
    return NULL;
}

YMDictionaryValue YMDictionaryRemove(YMDictionaryRef dict, YMDictionaryKey key)
{
    _YMDictionaryItemRef previousItem = NULL;
    _YMDictionaryItemRef theItem = _YMDictionaryFindItemWithIdentifier(dict->head, key, &previousItem);
    YMDictionaryValue outValue = NULL;
    if ( theItem )
    {
        if ( previousItem )
            previousItem->next = theItem->next;
        else
            dict->head = NULL;
        dict->count--;
        
        outValue = theItem->value;
        free(theItem);
    }
    
    return theItem;
}

size_t YMDictionaryGetCount(YMDictionaryRef dict)
{
    _YMDictionary *_dict = (_YMDictionary *)dict;
    return _dict->count;
}

YMDictionaryEnumRef YMDictionaryEnumeratorBegin(YMDictionaryRef dict)
{
    if ( ! dict->head )
        return NULL;
    
    return (YMDictionaryEnumRef)_YMDictionaryCopyItem(dict->head);
}

YMDictionaryEnumRef YMDictionaryEnumeratorGetNext(YMDictionaryRef dict, YMDictionaryEnumRef aEnum)
{
    _YMDictionaryItemRef item = (_YMDictionaryItemRef)aEnum; // overlapping
    _YMDictionaryItemRef next = item->next;
    
#warning todo why bother reallocating our enum-cum-listitem each time?
    free(item);
    
    if ( ! next )
        return NULL;
    
    return (YMDictionaryEnumRef)_YMDictionaryCopyItem(next);
}

void YMDictionaryEnumeratorEnd(YMDictionaryRef dict, YMDictionaryEnumRef aEnum)
{
    free(aEnum);
}

bool YMDictionaryPopKeyValue(YMDictionaryRef dict, bool last, YMDictionaryKey *outKey, YMDictionaryValue *outValue)
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
        previous->next = NULL;
    else
        dict->head = outItem->next;
    
    free(outItem);
    return true;
}

_YMDictionaryItemRef _YMDictionaryCopyItem(_YMDictionaryItemRef item)
{
    _YMDictionaryItemRef itemCopy = (_YMDictionaryItemRef)malloc(sizeof(struct __YMDictionaryItem));
    itemCopy->key = item->key;
    itemCopy->value = item->value;
    itemCopy->next = item->next;
    return itemCopy;
}
