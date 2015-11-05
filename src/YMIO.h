//
//  YMIO.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMIO_h
#define YMIO_h

#include "YMBase.h"

bool YMReadFull(int fd, uint8_t *buffer, size_t bytes);
bool YMWriteFull(int fd, const uint8_t *buffer, size_t bytes);

#endif /* YMIO_h */
