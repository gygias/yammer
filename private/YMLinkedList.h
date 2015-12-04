//
//  YMLinkedList.h
//  yammer
//
//  Created by david on 11/4/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMLinkedList_h
#define YMLinkedList_h

YM_EXTERN_C_PUSH

typedef const struct __ym_linked_list_t *YMLinkedListRef;

YMLinkedListRef YMLinkedListCreate();

typedef bool (*ym_linked_list_identifier_func)(void *);
void YMLinkedListAdd(YMLinkedListRef list, void *item);
bool YMLinkedListContains(YMLinkedListRef list, ym_linked_list_identifier_func);
void *YMLinkedListGetItem(YMLinkedListRef list, ym_linked_list_identifier_func);
void *YMLinkedListRemove(YMLinkedListRef list, ym_linked_list_identifier_func);

size_t YMLinkedListGetCount(YMLinkedListRef list);

YM_EXTERN_C_POP

#endif /* YMLinkedList_h */
