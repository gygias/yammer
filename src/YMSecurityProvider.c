//
//  YMSecurityProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMSecurityProvider.h"
#include "YMSecurityProviderInternal.h"

#include "YMUtilities.h"

#define ymlog_type YMLogSecurity
#include "YMLog.h"

YM_EXTERN_C_PUSH

bool YMNoSecurityInit(__ym_security_provider_t *);
bool YMNoSecurityRead(__ym_security_provider_t *,uint8_t*,size_t);
bool YMNoSecurityWrite(__ym_security_provider_t *,const uint8_t*,size_t);
bool YMNoSecurityClose(__ym_security_provider_t *);

YMSecurityProviderRef YMSecurityProviderCreateWithSocket(YMSOCKET socket)
{
    __ym_security_provider_t *p = (__ym_security_provider_t *)_YMAlloc(_YMSecurityProviderTypeID,sizeof(__ym_security_provider_t));
    
    p->socket = socket;
    p->initFunc = YMNoSecurityInit;
    p->readFunc = YMNoSecurityRead;
    p->writeFunc = YMNoSecurityWrite;
    p->closeFunc = YMNoSecurityClose;
    
    return p;
}

void _YMSecurityProviderFree(YMSecurityProviderRef p_)
{
    __unused __ym_security_provider_t *p = (__ym_security_provider_t *)p_;
}

void YMSecurityProviderSetInitFunc(YMSecurityProviderRef p, ym_security_init_func func)
{
    ((__ym_security_provider_t *)p)->initFunc = func;
}

bool YMSecurityProviderInit(YMSecurityProviderRef p)
{
    return p->initFunc((__ym_security_provider_t *)p);
}

bool YMSecurityProviderRead(YMSecurityProviderRef p, uint8_t *buffer, size_t bytes)
{
    return p->readFunc((__ym_security_provider_t *)p, buffer, bytes);
}

bool YMSecurityProviderWrite(YMSecurityProviderRef p, const uint8_t *buffer, size_t bytes)
{
    return p->writeFunc((__ym_security_provider_t *)p, buffer, bytes);
}

bool YMSecurityProviderClose(YMSecurityProviderRef p)
{
    return p->closeFunc((__ym_security_provider_t *)p);
}

// passthrough
bool YMNoSecurityInit(__unused __ym_security_provider_t *p)
{
    return true;
}

bool YMNoSecurityRead(__ym_security_provider_t *p, uint8_t *buffer, size_t bytes)
{
	YM_IO_BOILERPLATE
    YM_READ_SOCKET(p->socket, buffer, bytes);
    return ( (size_t)aRead == bytes );
}

bool YMNoSecurityWrite(__ym_security_provider_t *p, const uint8_t *buffer, size_t bytes)
{
    YM_IO_BOILERPLATE
    YM_WRITE_SOCKET(p->socket, buffer, bytes);
    return ( (size_t)aWrite == bytes );
}

bool YMNoSecurityClose(__unused __ym_security_provider_t *p)
{
    return true;
}

YM_EXTERN_C_POP
