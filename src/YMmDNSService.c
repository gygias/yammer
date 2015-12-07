//
//  YMmDNSService.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNSService.h"

#include "YMUtilities.h"
#include "YMThread.h"

#ifndef WIN32
#include <sys/socket.h>
# if defined(RPI)
# include <netinet/in.h>
# endif
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <dns_sd.h>

#define ymlog_type YMLogmDNS
#include "YMLog.h"

YM_EXTERN_C_PUSH

typedef struct __ym_mdns_service_t
{
    _YMType _type;
    
    YMStringRef type;
    YMStringRef name;
    uint16_t port;
    
    // volatile stuff
    uint8_t *txtRecord;
    uint16_t txtRecordLen;
    DNSServiceRef *dnsService;
    bool advertising;
    YMThreadRef eventThread;
} __ym_mdns_service_t;
typedef struct __ym_mdns_service_t *__YMmDNSServiceRef;

void __ymmdns_register_callback(__unused DNSServiceRef sdRef,
                                __unused DNSServiceFlags flags,
                                __unused DNSServiceErrorType errorCode,
                                __unused const char *name,
                                __unused const char *regtype,
                                __unused const char *domain,
                                void *context )
{
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)context;
    ymsoftassert(0==strcmp(regtype,YMSTR(service->type)),"register type: %s",regtype);
    ymsoftassert(0==strcmp(name,YMSTR(service->name)),"register name: %s",name);
    ymlog("mdns: %s/%s:%u: %d", YMSTR(service->type), YMSTR(service->name), service->port, errorCode);
}

//void __ym_mdns_service_event_thread(void *);

YMmDNSServiceRef YMmDNSServiceCreate(YMStringRef type, YMStringRef name, uint16_t port)
{
    // DNSServiceRegister will truncate this automatically, but keep the client well informed i suppose
    if ( ! name
        || YMLEN(name) >= mDNS_SERVICE_NAME_LENGTH_MAX
        || YMLEN(name) < mDNS_SERVICE_NAME_LENGTH_MIN )
    {
        ymlog("mdns: invalid service name");
        return NULL;
    }

	YMNetworkingInit();
    
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)_YMAlloc(_YMmDNSServiceTypeID,sizeof(struct __ym_mdns_service_t));
    
    service->type = YMRetain(type);
    service->name = YMRetain(name);
    service->port = port;
    service->advertising = false;
    service->txtRecord = NULL;
    service->txtRecordLen = 0;
    return (YMmDNSServiceRef)service;
}

void _YMmDNSServiceFree(YMTypeRef object)
{
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)object;
    if ( service->type )
        YMRelease(service->type);
    if ( service->name )
        YMRelease(service->name);
    if ( service->txtRecord )
    {
        TXTRecordDeallocate((TXTRecordRef *)service->txtRecord);
        free(service->txtRecord);
    }
}

bool YMmDNSServiceSetTXTRecord( YMmDNSServiceRef service_, YMmDNSTxtRecordKeyPair *keyPairs[], size_t nPairs )
{
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)service_;
    
    if ( keyPairs == NULL || nPairs == 0 )
        return false;
    
    size_t idx;
    
    TXTRecordRef *txtRecord = (TXTRecordRef *)YMALLOC(sizeof(TXTRecordRef));
    TXTRecordCreate(txtRecord, 0, NULL);
    for ( idx = 0; idx < nPairs; idx++ )
    {
        YMmDNSTxtRecordKeyPair **_keyPairs = (YMmDNSTxtRecordKeyPair **)keyPairs;
        const char *key = YMSTR(_keyPairs[idx]->key);
        const uint8_t *value = _keyPairs[idx]->value;
        uint8_t valueLen = _keyPairs[idx]->valueLen;
        
        TXTRecordSetValue(txtRecord, key, valueLen, value);
    }
    
    service->txtRecord = (uint8_t *)txtRecord;
    
    return true;
}

bool YMmDNSServiceStart( YMmDNSServiceRef service_ )
{
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)service_;
    
    DNSServiceRef *serviceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    uint16_t netPort = htons(service->port);
    bool txtExists = (service->txtRecord != NULL);
    uint16_t txtLength = txtExists ? TXTRecordGetLength((TXTRecordRef *)service->txtRecord) : 0;
    const void *txt = txtExists ? TXTRecordGetBytesPtr((TXTRecordRef *)service->txtRecord) : NULL;
    DNSServiceErrorType result = DNSServiceRegister(serviceRef,
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex (0=all)
                                                    YMSTR(service->name),
                                                    YMSTR(service->type),
                                                    NULL, // domain
                                                    NULL, // host
                                                    netPort,
                                                    txtLength,
                                                    txt,
                                                    __ymmdns_register_callback, // DNSServiceRegisterReply
                                                    service); // context
    
    if( result != kDNSServiceErr_NoError )
    {
        // on error "the callback is never invoked and the DNSServiceRef is not initialized"
        // leading me to think we free instead of DNSServiceRefDeallocate
        free(serviceRef);
        ymlog("mdns: DNSServiceRegister failed: %s/%s:%u: %d",YMSTR(service->type),YMSTR(service->name),(unsigned)service->port,result);
        return false;
    }
    
    service->dnsService = serviceRef;
    service->advertising = true;

    ymlog("mdns: published %s/%s:%u",YMSTR(service->type),YMSTR(service->name),(unsigned)service->port);
    return true;
}

bool YMmDNSServiceStop( YMmDNSServiceRef service_, bool synchronous )
{
    __YMmDNSServiceRef service = (__YMmDNSServiceRef)service_;
    
    if ( ! service->advertising )
        return false;
    
    service->advertising = false; // let event thread fall out
    
    bool okay = true;
    
    DNSServiceRefDeallocate(*(service->dnsService));
    free(service->dnsService);
    service->dnsService = NULL;
    
    if ( synchronous )
        okay = YMThreadJoin(service->eventThread);
    
    ymlog("mdns: browser stopping");
    return okay;
}

YM_EXTERN_C_POP
