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

YM_EXTERN_C_PUSH

typedef const struct __ym_mdns_service * YMmDNSServiceRef;

YMmDNSServiceRef YMAPI YMmDNSServiceCreate(YMStringRef type, YMStringRef name, uint16_t port);

// copies the keys/values
bool YMAPI YMmDNSServiceSetTXTRecord( YMmDNSServiceRef service, YMmDNSTxtRecordKeyPair *keyPairs[], size_t nPairs );
bool YMAPI YMmDNSServiceStart( YMmDNSServiceRef service );
bool YMAPI YMmDNSServiceStop( YMmDNSServiceRef service, bool synchronous );

YM_EXTERN_C_POP

#endif /* YMmDNSService_h */
