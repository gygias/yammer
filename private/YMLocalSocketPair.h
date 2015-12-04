//
//  YMLocalSocketPair.h
//  yammer
//
//  Created by david on 11/11/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMLocalSocketPair_h
#define YMLocalSocketPair_h

#include "YMBase.h"

YM_EXTERN_C_PUSH

typedef const struct __ym_local_socket_pair_t *YMLocalSocketPairRef;

YMLocalSocketPairRef YMAPI YMLocalSocketPairCreate(YMStringRef name, bool moreComing);

// if previous calls to YMLocalSocketPairCreate said 'moreComing', this function can be used to
// free associated resources. it is not thread-safe (with itself or with YMLocalSocketPairCreate).
void YMAPI YMLocalSocketPairStop();

YMSOCKET YMAPI YMLocalSocketPairGetA(YMLocalSocketPairRef pair);
YMSOCKET YMAPI YMLocalSocketPairGetB(YMLocalSocketPairRef pair);

YM_EXTERN_C_POP

#endif /* YMLocalSocketPair_h */
