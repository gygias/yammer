//
//  YMmDNSService.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMmDNSService_h
#define YMmDNSService_h

#include "YMBase.h"
#include "YMmDNS.h"

typedef struct __YMmDNSService *YMmDNSServiceRef;

YMmDNSServiceRef YMmDNSServiceCreate(const char *type, const char *name, uint16_t port);

// copies the keys/values
ymbool YMmDNSServiceSetTXTRecord( YMmDNSServiceRef service, YMmDNSTxtRecordKeyPair *keyPairs[], size_t nPairs );
ymbool YMmDNSServiceStart( YMmDNSServiceRef service );
ymbool YMmDNSServiceStop( YMmDNSServiceRef service, bool synchronous );

#endif /* YMmDNSService_h */
