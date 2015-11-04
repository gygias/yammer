//
//  YMConnection.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMConnection_h
#define YMConnection_h

#include "YMBase.h"

typedef struct __YMConnection
{
    YMTypeID _typeID;
} _YMConnection;
typedef struct _YMConnection *YMConnectionRef;

YMConnectionRef YMConnectionCreate();

#endif /* YMConnection_h */
