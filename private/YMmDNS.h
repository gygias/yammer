//
//  YMmDNS.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMmDNS_h
#define YMmDNS_h

#include "YMArray.h"

#define mDNS_SERVICE_NAME_LENGTH_MAX 63
#define mDNS_SERVICE_NAME_LENGTH_MIN 1

YM_EXTERN_C_PUSH

typedef struct _YMmDNSTxtRecordKeyPair
{
    YMStringRef key;
    const void *value;
    uint8_t valueLen; // length of key + value can't exceed 255 (allowing for '=')
} YMmDNSTxtRecordKeyPair;

#if !defined(YMWIN32)
#define YM_ADDRINFO struct addrinfo
#else
#define YM_ADDRINFO ADDRINFOA
#endif

typedef struct _YMmDNSServiceRecord
{
    YMStringRef type;
    YMStringRef name;
    YMStringRef domain;
    
    void *addrinfoSdref;
    YMArrayRef sockaddrList;
    uint16_t port;
    YMmDNSTxtRecordKeyPair **txtRecordKeyPairs;
    size_t txtRecordKeyPairsSize;
} YMmDNSServiceRecord;

typedef struct _YMmDNSServiceList
{
    YMmDNSServiceRecord *service;
    struct _YMmDNSServiceList *next;
} YMmDNSServiceList;

YMmDNSServiceRecord *_YMmDNSServiceRecordCreate(const char *name, const char*type, const char *domain);
void _YMmDNSServiceRecordSetPort(YMmDNSServiceRecord *record, uint16_t port);
void _YMmDNSServiceRecordSetTxtRecord(YMmDNSServiceRecord *record, const unsigned char *txtRecord, uint16_t txtLength);
void _YMmDNSServiceRecordAppendSockaddr(YMmDNSServiceRecord *record, const void *mdnsPortlessSockaddr);
void _YMmDNSServiceRecordFree(YMmDNSServiceRecord *record, bool floatResolvedInfo);

// exported for test cases
YMAPI YMmDNSTxtRecordKeyPair **_YMmDNSTxtKeyPairsCreate(const unsigned char *txtRecord, uint16_t txtLength, size_t *outSize);
void YMAPI _YMmDNSTxtKeyPairsFree(YMmDNSTxtRecordKeyPair **keyPairList, size_t size);
YMAPI unsigned char  *_YMmDNSTxtBlobCreate(YMmDNSTxtRecordKeyPair **keyPairList, uint16_t *inSizeOutLength);

void YMAPI _YMmDNSServiceListFree(YMmDNSServiceList *serviceList); // xxx

YM_EXTERN_C_POP

#endif /* YMmDNS_h */
