//
//  YMSecurityProvider.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSecurityProvider_h
#define YMSecurityProvider_h

typedef const struct __ym_security_provider_t *YMSecurityProviderRef;

YMSecurityProviderRef YMAPI YMSecurityProviderCreateWithSocket(YMSOCKET fd);
YMSecurityProviderRef YMAPI YMSecurityProviderCreate(YMFILE inFd, YMFILE outFd);

bool YMAPI YMSecurityProviderInit(YMSecurityProviderRef provider);
bool YMAPI YMSecurityProviderRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes);
bool YMAPI YMSecurityProviderWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t byte);
bool YMAPI YMSecurityProviderClose(YMSecurityProviderRef provider);

#endif /* YMSecurityProvider_h */
