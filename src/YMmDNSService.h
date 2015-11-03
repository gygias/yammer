//
//  YMmDNSService.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMmDNSService_h
#define YMmDNSService_h

#include "YMBase.h"

typedef struct __YMmDNSService *YMmDNSServiceRef;

YMmDNSServiceRef YMmDNSServiceCreate(char *type, char *name);

void YMmDNSServiceStart();
void YMmDNSServiceStop();

#endif /* YMmDNSService_h */
