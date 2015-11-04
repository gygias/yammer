//
//  YMmDNS.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMmDNS_h
#define YMmDNS_h

#include "YMBase.h"

typedef struct _YMmDNSTxtRecordKeyPair
{
    const char *key;
    const void *value;
    uint8_t valueLen; // length of key + value can't exceed 255 (allowing for '=')
} YMmDNSTxtRecordKeyPair;

typedef struct _YMmDNSServiceRecord
{
    const char *type;
    const char *name;
    const char *domain;
    
    // values below aren't known until the service is resolved
    bool resolved;
    uint16_t port;
    YMmDNSTxtRecordKeyPair **txtRecordKeyPairs;
    size_t txtRecordKeyPairsSize;
} YMmDNSServiceRecord;

typedef struct _YMmDNSServiceList
{
    YMmDNSServiceRecord *service;
    struct _YMmDNSServiceList *next;
} YMmDNSServiceList;

YMmDNSServiceRecord *_YMmDNSCreateServiceRecord(const char *name, const char*type, const char *domain, bool resolved, uint16_t port, const unsigned char *txtRecord);
YMmDNSTxtRecordKeyPair **__YMmDNSCreateTxtKeyPairs(const unsigned char *txtRecord, size_t *outSize);
void _YMmDNSServiceListFree(YMmDNSServiceList *serviceList); // xxx
void _YMmDNSServiceRecordFree(YMmDNSServiceRecord *service);
void _YMmDNSTxtRecordKeyPairsFree(YMmDNSTxtRecordKeyPair **keyPairList, size_t size);

#endif /* YMmDNS_h */
