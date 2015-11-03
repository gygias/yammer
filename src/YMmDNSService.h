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

typedef struct
{
    char *key;
    void *value;
    uint8_t valueLen; // length of key + value can't exceed 255 (allowing for '=')
} YMmDNSTxtRecordKeyPair;

YMmDNSServiceRef YMmDNSServiceCreate(char *type, char *name);

void YMmDNSServiceStart();
void YMmDNSServiceStop();

#endif /* YMmDNSService_h */
