//
//  YMSecurityProvider.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSecurityProvider_h
#define YMSecurityProvider_h

typedef YMTypeRef YMSecurityProviderRef;

YMSecurityProviderRef YMSecurityProviderCreateWithFullDuplexFile(int fd);
YMSecurityProviderRef YMSecurityProviderCreate(int inFd, int outFd);

bool YMSecurityProviderInit(YMSecurityProviderRef provider);
bool YMSecurityProviderRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes);
bool YMSecurityProviderWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t byte);
bool YMSecurityProviderClose(YMSecurityProviderRef provider);

#endif /* YMSecurityProvider_h */
