//
//  YMObject.h
//  yammer
//
//  Created by david on 4/8/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#ifndef YMObject_h
#define YMObject_h

YM_EXTERN_C_PUSH

typedef const struct __ym_object_t * YMObjectRef;

YMObjectRef YMObjectCreate();

YM_EXTERN_C_POP

#endif /* YMObject_h */
