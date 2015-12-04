//
//  YMAddress.h
//  yammer
//
//  Created by david on 11/11/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

// goal: abstraction and convenience for working with sockaddrs, human readable and non-cryptic

#ifndef YMAddress_h
#define YMAddress_h

#include <libyammer/YMBase.h>

YM_EXTERN_C_PUSH

typedef struct __YMAddressRef *YMAddressRef;

typedef enum
{
    YMAddressIPV4,
    YMAddressIPV6
} YMAddressType;

YMAddressRef YMAPI YMAddressCreate(const void *addressData, uint32_t length);
YMAddressRef YMAPI YMAddressCreateLocalHostIPV4(uint16_t port);
YMAddressRef YMAPI YMAddressCreateWithIPStringAndPort(YMStringRef ipString, uint16_t port);

const YMAPI void * YMAddressGetAddressData(YMAddressRef address);
int YMAPI YMAddressGetLength(YMAddressRef address);
YMStringRef YMAPI YMAddressGetDescription(YMAddressRef address);

int YMAPI YMAddressGetDomain(YMAddressRef address);
int YMAPI YMAddressGetAddressFamily(YMAddressRef address);
int YMAPI YMAddressGetDefaultProtocolForAddressFamily(int addressFamily);

YM_EXTERN_C_POP

#endif /* YMAddress_h */
