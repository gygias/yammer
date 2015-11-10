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
#undef ymlog_type
#define ymlog_type YMLogSecurity
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

bool YMTLSProviderInit(YMSecurityProviderRef provider);
