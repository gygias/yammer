//
//  YMPipePriv.h
//  yammer
//
//  Created by david on 11/6/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMPipePriv_h
#define YMPipePriv_h

typedef struct __YMPipeDebug
{
    YMTypeID _typeID;
    
    char *name;
    int inFd;
    int outFd;
} _YMPipeDebug;

#endif /* YMPipePriv_h */
