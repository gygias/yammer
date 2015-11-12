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

typedef struct __YMAddress *YMAddressRef;

typedef enum
{
    YMAddressIPV4,
    YMAddressIPV6
} YMAddressType;

YMAddressRef YMAddressCreate(void* addressData, uint32_t length);
YMAddressRef YMAddressCreateLocalHostIPV4(uint16_t port);

const void *YMAddressGetAddressData(YMAddressRef address);
int YMAddressGetLength(YMAddressRef address);
const char *YMAddressGetDescription(YMAddressRef address);

int YMAddressGetDomain(YMAddressRef address);
int YMAddressGetAddressFamily(YMAddressRef address);
int YMAddressGetDefaultProtocolForAddressFamily(int addressFamily);

#endif /* YMAddress_h */
