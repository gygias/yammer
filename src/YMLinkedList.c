//
//  YMLinkedList.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLinkedList.h"

#include "YMPrivate.h"

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogDefault
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

typedef struct _YMLinkedListItem
{
    void *representedItem;
    struct _YMLinkedListItem *next;
} YMLinkedListItem;

typedef struct __YMLinkedList
{
    YMTypeID _typeID;
    
    bool isYMTypeList;
    YMLinkedListItem *head;
    size_t count;
} _YMLinkedList;

YMLinkedListItem *_YMLinkedListFindItemWithIdentifier(YMLinkedListItem *head, bool (*identifierFunc)(void *), YMLinkedListItem **outPreviousItem);

YMLinkedListRef YMLinkedListCreate()
{
    _YMLinkedList *list = (_YMLinkedList *)YMALLOC(sizeof(_YMLinkedList));
    list->_typeID = _YMLinkedListTypeID;
    
    list->head = NULL;
    list->count = 0;
    
    return (YMLinkedListRef)list;
}

void _YMLinkedListFree(YMTypeRef object)
{
    _YMLinkedList *list = (_YMLinkedList *)object;
    // we don't assume ownership of list members
    YMLinkedListItem *itemIter = list->head;
    
    while (itemIter)
    {
        YMLinkedListItem *thisItem = itemIter;
        itemIter = (YMLinkedListItem *)itemIter->next;
        free(thisItem);
    }
    
    free(list);
}

void YMLinkedListAdd(YMLinkedListRef list, void *item)
{
    _YMLinkedList *_list = (_YMLinkedList *)list;
    
    YMLinkedListItem *newItem = (YMLinkedListItem *)YMALLOC(sizeof(YMLinkedListItem));
    newItem->representedItem = item;
    newItem->next = (struct _YMLinkedListItem *)_list->head;
    _list->head = newItem;
}

bool YMLinkedListContains(YMLinkedListRef list, bool (*identifierFunc)(void *))
{
    return ( NULL != _YMLinkedListFindItemWithIdentifier(((_YMLinkedList *)list)->head, identifierFunc, NULL) );
}

void *YMLinkedListGetItem(YMLinkedListRef list, bool (*identifierFunc)(void *))
{
    return _YMLinkedListFindItemWithIdentifier(((_YMLinkedList *)list)->head, identifierFunc, NULL);
}

YMLinkedListItem *_YMLinkedListFindItemWithIdentifier(YMLinkedListItem *head, bool (*identifierFunc)(void *), YMLinkedListItem **outPreviousItem)
{
    YMLinkedListItem *itemIter = head,
        *previousItem = NULL;
    
    while (itemIter)
    {
        YMLinkedListItem *thisItem = itemIter;
        if ( (*identifierFunc)(thisItem->representedItem) )
            return thisItem;
        previousItem = itemIter;
        itemIter = (YMLinkedListItem *)itemIter->next;
    }
    
    if ( outPreviousItem )
        *outPreviousItem = previousItem;
    
    return NULL;
}

void * YMLinkedListRemove(YMLinkedListRef list, bool (*identifierFunc)(void *))
{
    _YMLinkedList *_list = (_YMLinkedList *)list;
    YMLinkedListItem *previousItem = NULL;
    YMLinkedListItem *theItem = _YMLinkedListFindItemWithIdentifier(_list->head, identifierFunc, &previousItem);
    if ( theItem )
    {
        if ( previousItem )
            previousItem->next = theItem->next;
        else
            _list->head = NULL;
    }
    
    return theItem;
}

size_t YMLinkedListGetCount(YMLinkedListRef list)
{
    _YMLinkedList *_list = (_YMLinkedList *)list;
    return _list->count;
}
