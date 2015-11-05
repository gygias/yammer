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
    int key;
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

void YMDictionaryAdd(YMDictionaryRef dict, YMDictionaryKey key, const void *item)
{
    if ( _YMDictionaryFindItemWithIdentifier(dict->head, key, NULL) )
    {
        YMLog("error: YMDictionary already contains item for key %d",key);
        abort();
    }
    YMDictionaryItemRef newItem = (YMDictionaryItemRef)malloc(sizeof(struct __YMDictionaryItem));
    newItem->representedItem = item;
    newItem->next = (struct _YMDictionaryItem *)dict->head;
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
