//
//  YMSecurityProvider.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSecurityProvider_h
#define YMSecurityProvider_h

#ifdef WIN32
#include <winsock2.h>
#endif

typedef const struct __ym_security_provider_t *YMSecurityProviderRef;

YMSecurityProviderRef YMSecurityProviderCreateWithSocket(YMSOCKET fd);
YMSecurityProviderRef YMSecurityProviderCreate(YMFILE inFd, YMFILE outFd);

bool YMSecurityProviderInit(YMSecurityProviderRef provider);
bool YMSecurityProviderRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes);
bool YMSecurityProviderWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t byte);
bool YMSecurityProviderClose(YMSecurityProviderRef provider);

#endif /* YMSecurityProvider_h */
