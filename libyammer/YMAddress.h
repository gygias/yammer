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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __YMAddressRef *YMAddressRef;

typedef enum
{
    YMAddressIPV4,
    YMAddressIPV6
} YMAddressType;

YMAPI YMAddressRef YMAddressCreate(const void *addressData, uint32_t length);
YMAPI YMAddressRef YMAddressCreateLocalHostIPV4(uint16_t port);
YMAPI YMAddressRef YMAddressCreateWithIPStringAndPort(YMStringRef ipString, uint16_t port);

YMAPI const void *YMAddressGetAddressData(YMAddressRef address);
YMAPI int YMAddressGetLength(YMAddressRef address);
YMAPI YMStringRef YMAddressGetDescription(YMAddressRef address);

YMAPI int YMAddressGetDomain(YMAddressRef address);
YMAPI int YMAddressGetAddressFamily(YMAddressRef address);
YMAPI int YMAddressGetDefaultProtocolForAddressFamily(int addressFamily);

#ifdef __cplusplus
}
#endif

#endif /* YMAddress_h */
