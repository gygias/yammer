//
//  YMmDNS.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMmDNS_h
#define YMmDNS_h

#define mDNS_SERVICE_NAME_LENGTH_MAX 63
#define mDNS_SERVICE_NAME_LENGTH_MIN 1

typedef struct _YMmDNSTxtRecordKeyPair
{
    YMStringRef key;
    const void *value;
    uint8_t valueLen; // length of key + value can't exceed 255 (allowing for '=')
} YMmDNSTxtRecordKeyPair;

typedef struct _YMmDNSServiceRecord
{
    YMStringRef type;
    YMStringRef name;
    YMStringRef domain;
    
    // values below aren't known until the service is resolved
    bool resolved;
    struct hostent *hostNames;
    uint16_t port;
    YMmDNSTxtRecordKeyPair **txtRecordKeyPairs;
    size_t txtRecordKeyPairsSize;
} YMmDNSServiceRecord;

typedef struct _YMmDNSServiceList
{
    YMmDNSServiceRecord *service;
    struct _YMmDNSServiceList *next;
} YMmDNSServiceList;

YMmDNSServiceRecord *_YMmDNSCreateServiceRecord(const char *name, const char*type, const char *domain, bool resolved, const char *hostname,
                                                uint16_t port, const unsigned char *txtRecord, uint16_t txtLength);
YMmDNSTxtRecordKeyPair **__YMmDNSCreateTxtKeyPairs(const unsigned char *txtRecord, uint16_t txtLength, size_t *outSize);
void _YMmDNSServiceListFree(YMmDNSServiceList *serviceList); // xxx
void _YMmDNSServiceRecordFree(YMmDNSServiceRecord *service);
void _YMmDNSTxtRecordKeyPairsFree(YMmDNSTxtRecordKeyPair **keyPairList, size_t size);

#endif /* YMmDNS_h */
