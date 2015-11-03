//
//  YMBase.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMBase_h
#define YMBase_h

#include <unistd.h>
#include <stdbool.h>

typedef ssize_t (*ym_read_func)(int,void*,size_t);
typedef ssize_t (*ym_write_func)(int,void*,size_t);

#endif /* YMBase_h */
