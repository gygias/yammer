//
//  YMSecurityProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#include "YMSecurityProvider.h"

typedef const struct _YMSecurityProvider
{
    ym_security_init_func   initFunc;
    ym_read_func            readFunc;
    ym_write_func           writeFunc;
} YMSecurityProvider;
