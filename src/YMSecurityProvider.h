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

extern YMTypeID YMSecurityProviderType;

typedef bool (*ym_security_init_func)();

typedef struct __YMSecurityProvider *YMSecurityProviderRef;

YMSecurityProviderRef YMSecurityProviderCreate(int fd);
void YMSecurityProviderFree();

void YMSecurityProviderSetInitFunc(YMSecurityProviderRef provider, ym_security_init_func func);

void YMSecurityProviderRead(YMSecurityProviderRef provider, const void *buffer, size_t bytes);
void YMSecurityProviderWrite(YMSecurityProviderRef provider, const void *buffer, size_t byte);

#endif /* YMSecurityProvider_h */
