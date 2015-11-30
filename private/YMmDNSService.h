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

typedef const struct __ym_mdns_service_t *YMmDNSServiceRef;

YMmDNSServiceRef YMmDNSServiceCreate(YMStringRef type, YMStringRef name, uint16_t port);

// copies the keys/values
bool YMmDNSServiceSetTXTRecord( YMmDNSServiceRef service, YMmDNSTxtRecordKeyPair *keyPairs[], size_t nPairs );
bool YMmDNSServiceStart( YMmDNSServiceRef service );
bool YMmDNSServiceStop( YMmDNSServiceRef service, bool synchronous );

#endif /* YMmDNSService_h */
