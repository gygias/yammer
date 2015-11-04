//
//  YMSession.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSession_h
#define YMSession_h

#include "YMBase.h"

typedef struct __YMSession
{
    YMTypeID _typeID;
} _YMSession;
typedef struct _YMSession *YMSessionRef;

YMSessionRef YMSessionCreate();

#endif /* YMSession_h */
