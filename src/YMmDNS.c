//
//  YMmDNS.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNS.h"

#include <dns_sd.h>

// mDNS rules:
//
// service type:
// The service type must be an underscore, followed
// *                  by 1-15 characters, which may be letters, digits, or hyphens.
// *                  The transport protocol must be "_tcp" or "_udp".
//
// service name:
// If a name is specified, it must be 1-63 bytes of UTF-8 text.
// *                  If the name is longer than 63 bytes it will be automatically truncated
// *                  to a legal length, unless the NoAutoRename flag is set,
// *                  in which case kDNSServiceErr_BadParam will be returned.
//
// txt record:
// http://www.zeroconf.org/rendezvous/txtrecords.html
// The characters of "Name" MUST be printable US-ASCII values (0x20-0x7E), excluding '=' (0x3D).
// Passing NULL for the txtRecord is allowed as a synonym for txtLen=1, txtRecord="",
// *                  i.e. it creates a TXT record of length one containing a single empty string.
// *                  RFC 1035 doesn't allow a TXT record to contain *zero* strings, so a single empty
// *                  string is the smallest legal DNS TXT record.

void _YMmDNSServiceListFree(YMmDNSServiceList *serviceList)
{
    YMmDNSServiceList *aListItem = serviceList;
    while ( aListItem )
    {
        struct _YMmDNSServiceRecord *service = (struct _YMmDNSServiceRecord *)aListItem->service;
        if ( service )
            _YMmDNSServiceRecordFree(service);
    }
}

void _YMmDNSServiceRecordFree(YMmDNSServiceRecord *record)
{
    if ( record->name )
        free( (char *)record->name );
    if ( record->type )
        free( (char *)record->type );
    if ( record->domain )
        free( (char *)record->domain );
    if ( record->address )
        free( (char *)record->address );
    if ( record->txtRecordKeyPairs )
        _YMmDNSTxtRecordKeyPairsFree( (YMmDNSTxtRecordKeyPair **)record->txtRecordKeyPairs, record->txtRecordKeyPairsSize );
}

YMmDNSServiceRecord *_YMmDNSCreateServiceRecord(const char *name, const char*type, const char *domain, bool resolved, const char *address,
                                                uint16_t port, const unsigned char *txtRecord, uint16_t txtLength)
{
    YMmDNSServiceRecord *record = (YMmDNSServiceRecord *)malloc(sizeof(struct _YMmDNSServiceRecord));
    if ( name )
        record->name = strdup(name);
    else
        record->name = NULL;
    
    if ( type )
        record->type = strdup(type);
    else
        record->type = NULL;
    
    if ( domain )
        record->domain = strdup(domain);
    else
        record->domain = NULL;
    
    record->resolved = resolved;
    
    if ( address )
        record->address = strdup(address);
    else
        record->address = NULL;
    
    record->port = port;
    
    if ( txtRecord )
    {
        size_t txtSize = 0;
        record->txtRecordKeyPairs = __YMmDNSCreateTxtKeyPairs(txtRecord, txtLength, &txtSize);
        record->txtRecordKeyPairsSize = txtSize;
    }
    else
    {
        record->txtRecordKeyPairs = NULL;
        record->txtRecordKeyPairsSize = 0;
    }
    
    return record;
}

YMmDNSTxtRecordKeyPair **__YMmDNSCreateTxtKeyPairs(const unsigned char *txtRecord, uint16_t txtLength, size_t *outSize)
{
    size_t  allocatedListSize = 20,
            listSize = 0;
    YMmDNSTxtRecordKeyPair **keyPairList = (YMmDNSTxtRecordKeyPair **)malloc(allocatedListSize * sizeof(YMmDNSTxtRecordKeyPair*));
    
    size_t currentPairOffset = 0;
    uint8_t aPairLength = txtRecord[currentPairOffset];
    while ( currentPairOffset < txtLength - 1 )
    {
        const unsigned char* aPairWalker = txtRecord + currentPairOffset + 1;
        __unused const char* debugThisKey = (char *)aPairWalker;
        
        if ( listSize == allocatedListSize )
        {
            allocatedListSize *= 2;
            keyPairList = (YMmDNSTxtRecordKeyPair **)realloc(keyPairList, allocatedListSize * sizeof(struct YMmDNSTxtRecordKeyPair*)); // could be optimized
        }
        
        char *equalsPtr = strstr((const char *)aPairWalker,"=");
        if ( equalsPtr == NULL )
        {
            YMLog("__YMmDNSCreateTxtKeyPairs failed to parse txt record");
            _YMmDNSTxtRecordKeyPairsFree(keyPairList, listSize);
            return NULL;
        }
        
        size_t keyLength = equalsPtr - (char *)aPairWalker;
        char *keyStr = (char *)malloc(keyLength + 1);
        memcpy(keyStr,aPairWalker,keyLength);
        keyStr[keyLength] = '\0';
        aPairWalker += keyLength + 1; // skip past '='
        YMLog("DECODE[%u]: key[%u]: %s",listSize,keyLength, keyStr);
        
        size_t valueLength = aPairLength - keyLength - 1;
        uint8_t *valueBuf = (uint8_t *)malloc(valueLength);
        memcpy(valueBuf, aPairWalker, valueLength);
        aPairWalker += valueLength;
        YMLog("DECODE[%u]: value[%u]",listSize,valueLength);
        
        YMmDNSTxtRecordKeyPair *aKeyPair = (YMmDNSTxtRecordKeyPair *)malloc(sizeof(YMmDNSTxtRecordKeyPair));
        aKeyPair->key = keyStr;
        aKeyPair->value = valueBuf;
        aKeyPair->valueLen = valueLength;
        keyPairList[listSize] = aKeyPair;
        
        currentPairOffset += aPairLength + 1;
        aPairLength = txtRecord[currentPairOffset];
        
        if ( txtRecord + currentPairOffset != aPairWalker )
            YMLog("WTF");
        
        listSize++;
    }
    
    if ( outSize )
        *outSize = listSize;
    
    return keyPairList;
}

void _YMmDNSTxtRecordKeyPairsFree(YMmDNSTxtRecordKeyPair **keyPairList, size_t size)
{
    size_t idx;
    for ( idx = 0; idx < size; idx++ )
    {
        YMmDNSTxtRecordKeyPair *aPair = keyPairList[idx];
        if ( aPair )
        {
            if ( aPair->key )
                free((char *)aPair->key);
            if ( aPair->value )
                free((void *)aPair->value);
            free(aPair);
        }
    }
    free(keyPairList);
}