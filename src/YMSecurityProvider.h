//
//  YMSecurityProvider.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMSecurityProvider_h
#define YMSecurityProvider_h

#include "YMBase.h"

#include <stdio.h>

typedef bool (*ym_security_init_func)(int,size_t*);

typedef const struct _YMSecurityProvider *YMSecurityProviderRef;

#endif /* YMSecurityProvider_h */
