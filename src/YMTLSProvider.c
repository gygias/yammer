//
//  YMTLSProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMTLSProvider.h"

#include <openssl/err.h>

#include "YMLog.h"
#undef ymlogType
#define ymlogType YMLogSecurity
#if ( ymlogType >= ymLogTarget )
#undef ymlog
#define ymlog(x,...)
#endif

bool YMTLSProviderInit(YMSecurityProviderRef provider);
