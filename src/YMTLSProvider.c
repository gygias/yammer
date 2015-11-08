//
//  YMTLSProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMTLSProvider.h"

#include <openssl/err.h>

#undef ymLogType
#define ymLogType YMLogTypeSecurity

bool YMTLSProviderInit(YMSecurityProviderRef provider);
