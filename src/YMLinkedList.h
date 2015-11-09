//
//  YMLinkedList.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMLinkedList_h
#define YMLinkedList_h

typedef struct _YMLinkedList *YMLinkedListRef;

YMLinkedListRef YMLinkedListCreate();

typedef bool (*ym_linked_list_identifier_func)(void *);
void YMLinkedListAdd(YMLinkedListRef list, void *item);
bool YMLinkedListContains(YMLinkedListRef list, ym_linked_list_identifier_func);
void *YMLinkedListGetItem(YMLinkedListRef list, ym_linked_list_identifier_func);
void *YMLinkedListRemove(YMLinkedListRef list, ym_linked_list_identifier_func);

size_t YMLinkedListGetCount(YMLinkedListRef list);

#endif /* YMLinkedList_h */
