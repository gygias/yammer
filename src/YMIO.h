//
//  YMIO.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMIO_h
#define YMIO_h

#include "YMBase.h"

bool YMRead(int fd, const void *buffer, size_t bytes);
bool YMWrite(int fd, const void *buffer, size_t bytes);

#endif /* YMIO_h */
