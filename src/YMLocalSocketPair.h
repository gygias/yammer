//
//  YMLocalSocketPair.h
//  yammer
//
//  Created by david on 11/11/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMLocalSocketPair_h
#define YMLocalSocketPair_h

typedef YMTypeRef YMLocalSocketPairRef;

YMLocalSocketPairRef YMLocalSocketPairCreate(YMStringRef name);

int YMLocalSocketPairGetA(YMLocalSocketPairRef pair);
int YMLocalSocketPairGetB(YMLocalSocketPairRef pair);

#endif /* YMLocalSocketPair_h */
