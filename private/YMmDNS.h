//
//  YMmDNS.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMmDNS_h
#define YMmDNS_h

#ifdef WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS // todo, gethostbyname
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define mDNS_SERVICE_NAME_LENGTH_MAX 63
#define mDNS_SERVICE_NAME_LENGTH_MIN 1

typedef struct _YMmDNSTxtRecordKeyPair
{
    YMStringRef key;
    const void *value;
    uint8_t valueLen; // length of key + value can't exceed 255 (allowing for '=')
} YMmDNSTxtRecordKeyPair;

#ifndef WIN32
#define YM_ADDRINFO struct addrinfo
#else
#define YM_ADDRINFO ADDRINFOA
#endif

typedef struct _YMmDNSServiceRecord
{
    YMStringRef type;
    YMStringRef name;
    YMStringRef domain;
    
    // values below aren't known until the service is resolved
    bool resolved;
	YM_ADDRINFO *addrinfo;
    uint16_t port;
    YMmDNSTxtRecordKeyPair **txtRecordKeyPairs;
    size_t txtRecordKeyPairsSize;
} YMmDNSServiceRecord;

typedef struct _YMmDNSServiceList
{
    YMmDNSServiceRecord *service;
    struct _YMmDNSServiceList *next;
} YMmDNSServiceList;

YMmDNSServiceRecord *_YMmDNSServiceRecordCreate(const char *name, const char*type, const char *domain, bool resolved, const char *hostname,
                                                uint16_t port, const unsigned char *txtRecord, uint16_t txtLength);
void _YMmDNSServiceRecordFree(YMmDNSServiceRecord *record, bool floatResolvedInfo);

YMmDNSTxtRecordKeyPair **_YMmDNSTxtKeyPairsCreate(const unsigned char *txtRecord, uint16_t txtLength, size_t *outSize);
void _YMmDNSTxtKeyPairsFree(YMmDNSTxtRecordKeyPair **keyPairList, size_t size);

unsigned char  *_YMmDNSTxtBlobCreate(YMmDNSTxtRecordKeyPair **keyPairList, uint16_t *inSizeOutLength);
void _YMmDNSServiceListFree(YMmDNSServiceList *serviceList); // xxx

#endif /* YMmDNS_h */
