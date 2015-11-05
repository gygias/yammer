//
//  YMUtilities.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMUtilities_h
#define YMUtilities_h

#include "YMBase.h"

typedef enum
{
    LessThan = -1,
    EqualTo = 0,
    GreaterThan = 1
} ComparisonResult;
ComparisonResult YMTimevalCompare(struct timeval *a, struct timeval *b);
// these functions modifies time
void YMSetTheBeginningOfPosixTimeForCurrentPlatform(struct timeval *time);
void YMSetTheEndOfPosixTimeForCurrentPlatform(struct timeval *time);
#warning todo add inlines

bool YMReadFull(int fd, uint8_t *buffer, size_t bytes);
bool YMWriteFull(int fd, const uint8_t *buffer, size_t bytes);

#endif /* YMUtilities_h */
