//
//  YMPrivate.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMPrivate_h
#define YMPrivate_h

#include "YMBase.h"

extern YMTypeID _YMPipeType;
extern YMTypeID _YMStreamTypeID;
extern YMTypeID _YMConnectionTypeID;
extern YMTypeID _YMSecurityProviderType;
extern YMTypeID _YMPlexerTypeID;
extern YMTypeID _YMSessionTypeID;

typedef struct __YMTypeRef
{
    YMTypeID type;
} _YMTypeRef;

#endif /* YMPrivate_h */
