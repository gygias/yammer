//
//  YMDictionary.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMDictionary.h"

#include "YMPrivate.h"

typedef struct _YMDictionaryItem
{
    int key;
    void *representedItem;
    struct _YMDictionaryItem *next;
} YMDictionaryItem;

typedef struct __YMDictionary
{
    YMTypeID _typeID;
    
    bool isYMTypeDict;
    YMDictionaryItem *head;
    size_t count;
} _YMDictionary;

YMDictionaryItem *_YMDictionaryFindItemWithIdentifier(YMDictionaryItem *head, int key, YMDictionaryItem **outPreviousItem);

YMDictionaryRef YMDictionaryCreate()
{
    _YMDictionary *dict = (_YMDictionary *)malloc(sizeof(_YMDictionary));
    dict->_typeID = _YMDictionaryTypeID;
    
    dict->head = NULL;
    dict->count = 0;
    
    return (YMDictionaryRef)dict;
}

void _YMDictionaryFree(YMTypeRef object)
{
    _YMDictionary *dict = (_YMDictionary *)object;
    // we don't assume ownership of dict members
    YMDictionaryItem *itemIter = dict->head;
    
    while (itemIter)
    {
        YMDictionaryItem *thisItem = itemIter;
        itemIter = (YMDictionaryItem *)itemIter->next;
        free(thisItem);
    }
    
    free(dict);
}

void YMDictionaryAdd(YMDictionaryRef dict, int key, void *item)
{
    _YMDictionary *_dict = (_YMDictionary *)dict;
    
    YMDictionaryItem *newItem = (YMDictionaryItem *)malloc(sizeof(YMDictionaryItem));
    newItem->representedItem = item;
    newItem->next = (struct _YMDictionaryItem *)_dict->head;
}

bool YMDictionaryContains(YMDictionaryRef dict, int key)
{
    return ( NULL != _YMDictionaryFindItemWithIdentifier(((_YMDictionary *)dict)->head, key, NULL) );
}

void *YMDictionaryGetItem(YMDictionaryRef dict, int key)
{
    return _YMDictionaryFindItemWithIdentifier(((_YMDictionary *)dict)->head, key, NULL);
}

YMDictionaryItem *_YMDictionaryFindItemWithIdentifier(YMDictionaryItem *head, int key, YMDictionaryItem **outPreviousItem)
{
    YMDictionaryItem *itemIter = head,
        *previousItem = NULL;
    
    while (itemIter)
    {
        YMDictionaryItem *thisItem = itemIter;
        if ( thisItem->key == key )
            return thisItem;
        previousItem = itemIter;
        itemIter = (YMDictionaryItem *)itemIter->next;
    }
    
    if ( outPreviousItem )
        *outPreviousItem = previousItem;
    
    return NULL;
}

void * YMDictionaryRemove(YMDictionaryRef dict, int key)
{
    _YMDictionary *_dict = (_YMDictionary *)dict;
    YMDictionaryItem *previousItem = NULL;
    YMDictionaryItem *theItem = _YMDictionaryFindItemWithIdentifier(_dict->head, key, &previousItem);
    if ( theItem )
    {
        if ( previousItem )
            previousItem->next = theItem->next;
        else
            _dict->head = NULL;
    }
    
    return theItem;
}

size_t YMDictionaryGetCount(YMDictionaryRef dict)
{
    _YMDictionary *_dict = (_YMDictionary *)dict;
    return _dict->count;
}
