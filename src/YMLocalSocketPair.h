//
//  YMLocalSocketPair.h
//  yammer
//
//  Created by david on 11/11/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMLocalSocketPair_h
#define YMLocalSocketPair_h

typedef struct __YMLocalSocketPair *YMLocalSocketPairRef;

YMLocalSocketPairRef YMLocalSocketPairCreate(const char *name);

int YMLocalSocketPairGetA(YMLocalSocketPairRef pair);
int YMLocalSocketPairGetB(YMLocalSocketPairRef pair);

#endif /* YMLocalSocketPair_h */
