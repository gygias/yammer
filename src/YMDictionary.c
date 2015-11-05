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
    const void *representedItem;
    struct _YMDictionaryItem *next;
} _YMDictionaryItem;
typedef _YMDictionaryItem *YMDictionaryItemRef;

typedef struct __YMDictionary
{
    YMTypeID _typeID;
    
    bool isYMTypeDict;
    YMDictionaryItemRef head;
    size_t count;
} _YMDictionary;

YMDictionaryItemRef _YMDictionaryFindItemWithIdentifier(YMDictionaryItemRef head, YMDictionaryKey key, YMDictionaryItemRef *outPreviousItem);
YMDictionaryItemRef _YMDictionaryCopyItem(YMDictionaryItemRef item);

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
    YMDictionaryItemRef itemIter = dict->head;
    
    while (itemIter)
    {
        YMDictionaryItemRef thisItem = itemIter;
        itemIter = (YMDictionaryItemRef)itemIter->next;
        free(thisItem);
    }
    
    free(dict);
}

void YMDictionaryAdd(YMDictionaryRef dict, YMDictionaryKey key, YMDictionaryValue item)
{
    if ( _YMDictionaryFindItemWithIdentifier(dict->head, key, NULL) )
    {
        YMLog("error: YMDictionary already contains item for key %d",key);
        abort();
    }
    YMDictionaryItemRef newItem = (YMDictionaryItemRef)malloc(sizeof(struct __YMDictionaryItem));
    newItem->representedItem = item;
    newItem->next = (struct _YMDictionaryItem *)dict->head; // nulls or prepends
    if ( ! dict->head )
        dict->head = newItem;    
    
    dict->count++;
}

bool YMDictionaryContains(YMDictionaryRef dict, YMDictionaryKey key)
{
    return ( NULL != _YMDictionaryFindItemWithIdentifier(dict->head, key, NULL) );
}

void *YMDictionaryGetItem(YMDictionaryRef dict, YMDictionaryKey key)
{
    return _YMDictionaryFindItemWithIdentifier(dict->head, key, NULL);
}

YMDictionaryItemRef _YMDictionaryFindItemWithIdentifier(YMDictionaryItemRef head, YMDictionaryKey key, YMDictionaryItemRef *outPreviousItem)
{
    YMDictionaryItemRef itemIter = head,
        previousItem = NULL;
    
    while (itemIter)
    {
        YMDictionaryItemRef thisItem = itemIter;
        if ( thisItem->key == key )
            return thisItem;
        previousItem = itemIter;
        itemIter = (YMDictionaryItemRef)itemIter->next;
    }
    
    if ( outPreviousItem )
        *outPreviousItem = previousItem;
    
    return NULL;
}

void * YMDictionaryRemove(YMDictionaryRef dict, YMDictionaryKey key)
{
    YMDictionaryItemRef previousItem = NULL;
    YMDictionaryItemRef theItem = _YMDictionaryFindItemWithIdentifier(dict->head, key, &previousItem);
    if ( theItem )
    {
        if ( previousItem )
            previousItem->next = theItem->next;
        else
            dict->head = NULL;
        dict->count--;
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
    YMDictionaryItemRef item = (YMDictionaryItemRef)aEnum;
    YMDictionaryItemRef next = (YMDictionaryItemRef)item->next;
    
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

YMDictionaryItemRef _YMDictionaryCopyItem(YMDictionaryItemRef item)
{
    YMDictionaryItemRef itemCopy = (YMDictionaryItemRef)malloc(sizeof(struct __YMDictionaryItem));
    itemCopy->key = item->key;
    itemCopy->representedItem = item->representedItem;
    itemCopy->next = item->next;
    return itemCopy;
}
