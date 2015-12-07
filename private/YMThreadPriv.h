//
//  YMThreadPriv.h
//  yammer
//
//  Created by david on 11/5/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMThreadPriv_h
#define YMThreadPriv_h

#include "YMThread.h"

YM_EXTERN_C_PUSH

uint64_t _YMThreadGetThreadNumber(YMThreadRef thread_);
uint64_t _YMThreadGetCurrentThreadNumber();

YM_EXTERN_C_POP

#endif /* YMThreadPriv_h */
