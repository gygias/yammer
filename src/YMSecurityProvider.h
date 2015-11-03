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

typedef struct __YMSecurityProvider *YMSecurityProviderRef;

YMSecurityProviderRef YMSecurityProviderCreate(int fd);
void YMSecurityProviderFree();

void YMSecurityProviderRead(YMSecurityProviderRef provider, const void *buffer, size_t bytes);
void YMSecurityProviderWrite(YMSecurityProviderRef provider, const void *buffer, size_t byte);

void YMSecurityProviderClose(YMSecurityProviderRef provider);

#endif /* YMSecurityProvider_h */
