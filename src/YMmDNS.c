//
//  YMmDNS.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNS.h"

#include <netdb.h>

//#include <dns_sd.h>

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

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogmDNS
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

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
        YMRelease(record->name);
    if ( record->type )
        YMRelease(record->type);
    if ( record->domain )
        YMRelease(record->domain);
    //if ( record->hostNames )  // todo: i can't find any mention of ownership of this struct
    //    free( (char *)record->hostNames );
    if ( record->txtRecordKeyPairs )
        _YMmDNSTxtRecordKeyPairsFree( (YMmDNSTxtRecordKeyPair **)record->txtRecordKeyPairs, record->txtRecordKeyPairsSize );
}

YMmDNSServiceRecord *_YMmDNSCreateServiceRecord(const char *name, const char*type, const char *domain, bool resolved, const char *hostname,
                                                uint16_t port, const unsigned char *txtRecord, uint16_t txtLength)
{
    YMmDNSServiceRecord *record = (YMmDNSServiceRecord *)YMALLOC(sizeof(struct _YMmDNSServiceRecord));
    if ( name )
        record->name = YMSTRC(name);
    else
        record->name = NULL;
    
    if ( type )
        record->type = YMSTRC(type);
    else
        record->type = NULL;
    
    if ( domain )
        record->domain = YMSTRC(domain);
    else
        record->domain = NULL;
    
    record->resolved = resolved;
    
    if ( hostname )
    {
        struct hostent *hostNames = gethostbyname(hostname);
        if ( ! hostNames )
            ymerr("mdns: warning: gethostbyname failed for %s",hostname);
        record->hostNames = hostNames;
    }
    
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
    YMmDNSTxtRecordKeyPair **keyPairList = (YMmDNSTxtRecordKeyPair **)YMALLOC(allocatedListSize * sizeof(YMmDNSTxtRecordKeyPair*));
    
    size_t currentPairOffset = 0;
    uint8_t aPairLength = txtRecord[currentPairOffset];
    while ( currentPairOffset < txtLength - 1 ) // -1 to handle 0 pairs case
    {
        const unsigned char* aPairWalker = txtRecord + currentPairOffset + 1;
        __unused const char* debugThisKey = (char *)aPairWalker;
        
        if ( listSize == allocatedListSize )
        {
            allocatedListSize *= 2;
            keyPairList = (YMmDNSTxtRecordKeyPair **)realloc(keyPairList, allocatedListSize * sizeof(YMmDNSTxtRecordKeyPair*)); // could be optimized
        }
        
        char *equalsPtr = strstr((const char *)aPairWalker,"=");
        if ( equalsPtr == NULL )
        {
            ymlog("mdns: warning: failed to parse %ub txt record",txtLength);
            _YMmDNSTxtRecordKeyPairsFree(keyPairList, listSize);
            return NULL;
        }
        
        size_t keyLength = (size_t)(equalsPtr - (char *)aPairWalker);
        char keyStr[keyLength + 1];
        memcpy(keyStr,aPairWalker,keyLength);
        keyStr[keyLength] = '\0';
        aPairWalker += keyLength + 1; // skip past '='
        
        size_t valueLength = aPairLength - keyLength - 1;
        uint8_t *valueBuf = YMALLOC(valueLength);
        memcpy(valueBuf, aPairWalker, valueLength);
        
        YMmDNSTxtRecordKeyPair *aKeyPair = (YMmDNSTxtRecordKeyPair *)YMALLOC(sizeof(YMmDNSTxtRecordKeyPair));
        aKeyPair->key = YMSTRC(keyStr);
        aKeyPair->value = valueBuf;
        aKeyPair->valueLen = (uint8_t)valueLength;
        keyPairList[listSize] = aKeyPair;
        
        if ( currentPairOffset == txtLength )
            break;
        
        currentPairOffset += aPairLength + 1;
        aPairLength = txtRecord[currentPairOffset];
        
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
                YMRelease(aPair->key);
            if ( aPair->value )
                free((void*)aPair->value);
            free(aPair);
        }
    }
    free(keyPairList);
}
