//
//  YMSecurityProvider.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSecurityProvider_h
#define YMSecurityProvider_h

YM_EXTERN_C_PUSH

typedef const struct __ym_security_provider * YMSecurityProviderRef;

YMSecurityProviderRef YMAPI YMSecurityProviderCreate(YMFILE inFile, YMFILE outFile);

bool YMAPI YMSecurityProviderInit(YMSecurityProviderRef provider);
bool YMAPI YMSecurityProviderRead(YMSecurityProviderRef provider, uint8_t *buffer, size_t bytes);
bool YMAPI YMSecurityProviderWrite(YMSecurityProviderRef provider, const uint8_t *buffer, size_t bytes);
bool YMAPI YMSecurityProviderClose(YMSecurityProviderRef provider);

YM_EXTERN_C_POP

#endif /* YMSecurityProvider_h */
