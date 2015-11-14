//
//  YMLinkedList.c
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLinkedList.h"

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

typedef struct __ym_linked_list
{
    _YMType _type;
    
    bool isYMTypeList;
    YMLinkedListItem *head;
    size_t count;
} ___ym_linked_list;
typedef struct __ym_linked_list __YMLinkedList;
typedef __YMLinkedList *__YMLinkedListRef;

YMLinkedListItem *__YMLinkedListFindItemWithIdentifier(YMLinkedListItem *head, bool (*identifierFunc)(void *), YMLinkedListItem **outPreviousItem);

YMLinkedListRef YMLinkedListCreate()
{
    __YMLinkedListRef list = (__YMLinkedListRef)_YMAlloc(_YMLinkedListTypeID,sizeof(__YMLinkedList));
    
    list->head = NULL;
    list->count = 0;
    
    return (YMLinkedListRef)list;
}

void _YMLinkedListFree(YMTypeRef object)
{
    __YMLinkedListRef list = (__YMLinkedListRef)object;
    // we don't assume ownership of list members
    YMLinkedListItem *itemIter = list->head;
    
    while (itemIter)
    {
        YMLinkedListItem *thisItem = itemIter;
        itemIter = (YMLinkedListItem *)itemIter->next;
        free(thisItem);
    }
}

void YMLinkedListAdd(YMLinkedListRef list_, void *item)
{
    __YMLinkedListRef list = (__YMLinkedListRef)list_;
    
    YMLinkedListItem *newItem = (YMLinkedListItem *)YMALLOC(sizeof(YMLinkedListItem));
    newItem->representedItem = item;
    newItem->next = (struct _YMLinkedListItem *)list->head;
    list->head = newItem;
}

bool YMLinkedListContains(YMLinkedListRef list_, bool (*identifierFunc)(void *))
{
    __YMLinkedListRef list = (__YMLinkedListRef)list_;
    return ( NULL != __YMLinkedListFindItemWithIdentifier(list->head, identifierFunc, NULL) );
}

void *YMLinkedListGetItem(YMLinkedListRef list_, bool (*identifierFunc)(void *))
{
    __YMLinkedListRef list = (__YMLinkedListRef)list_;
    return __YMLinkedListFindItemWithIdentifier(list->head, identifierFunc, NULL);
}

YMLinkedListItem *__YMLinkedListFindItemWithIdentifier(YMLinkedListItem *head, bool (*identifierFunc)(void *), YMLinkedListItem **outPreviousItem)
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

void * YMLinkedListRemove(YMLinkedListRef list_, bool (*identifierFunc)(void *))
{
    __YMLinkedListRef list = (__YMLinkedListRef)list_;
    
    YMLinkedListItem *previousItem = NULL;
    YMLinkedListItem *theItem = __YMLinkedListFindItemWithIdentifier(list->head, identifierFunc, &previousItem);
    if ( theItem )
    {
        if ( previousItem )
            previousItem->next = theItem->next;
        else
            list->head = NULL;
    }
    
    return theItem;
}

size_t YMLinkedListGetCount(YMLinkedListRef list_)
{
    __YMLinkedListRef list = (__YMLinkedListRef)list_;
    return list->count;
}
