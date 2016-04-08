//
//  YMObject.c
//  yammer
//
//  Created by david on 4/8/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#include "YMObject.h"

YM_EXTERN_C_PUSH

typedef struct __ym_object
{
    _YMType _type;
} __ym_object;
typedef struct __ym_object __ym_object_t;

YMObjectRef YMObjectCreate()
{
    return NULL;
}

void _YMObjectFree(YMObjectRef object)
{
    object = NULL;
}

YM_EXTERN_C_POP
