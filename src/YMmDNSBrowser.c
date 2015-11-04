//
//  YMmDNSBrowser.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNSBrowser.h"
#include "YMPrivate.h"

#include "YMThreads.h"

#include <errno.h>

typedef struct __YMmDNSBrowser
{
    YMTypeID _typeID;
    
    char *type;
    YMmDNSServiceList *serviceList;
    
#ifdef YMmDNS_ENUMERATION
    bool enumerating;
    DNSServiceRef *enumerateServiceRef;
    YMThreadRef enumerateEventThread;
#endif
    
    bool browsing;
    DNSServiceRef *browseServiceRef;
    YMThreadRef browseEventThread;
    
    bool resolving;
    DNSServiceRef *resolveServiceRef;
    ym_mdns_service_resolved_func resolveFunc;
    
    ym_mdns_service_appeared_func serviceAppeared;
    ym_mdns_service_removed_func serviceRemoved;
    ym_mdns_service_updated_func serviceUpdated;
    ym_mdns_service_resolved_func serviceResolved;
} _YMmDNSBrowser;

#pragma mark callback/event goo
#ifdef YMmDNS_ENUMERATION
void DNSSD_API _YMmDNSEnumerateReply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *replyDomain, void *context);
#endif
static void DNSSD_API _YMmDNSBrowseCallback(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName,
                                            const char *reg_type, const char *reply_domain, void *context);
void DNSSD_API _YMmDNSResolveCallback(DNSServiceRef serviceRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *name,
                                      const char *host, uint16_t port, uint16_t txtLen, const unsigned char *txtRecord, void *context );

void *_YMmDNSBrowserEventThread(void *context);

#pragma mark private
void _YMmDNSBrowserAddService(_YMmDNSBrowser *browser, YMmDNSServiceRecord *record);
void _YMmDNSBrowserUpdateService(_YMmDNSBrowser *browser, YMmDNSServiceRecord *record);
void _YMmDNSBrowserRemoveServiceNamed(_YMmDNSBrowser *browser, const char *name);

YMmDNSServiceRecord *_YMmDNSBrowserGetServiceWithName(_YMmDNSBrowser *browser, const char *name, bool remove);

YMmDNSBrowserRef YMmDNSBrowserCreate(char *type, ym_mdns_service_appeared_func serviceAppeared, ym_mdns_service_removed_func serviceRemoved)
{
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)calloc(1,sizeof(_YMmDNSBrowser));
    _browser->_typeID = _YMmDNSBrowserTypeID;
    
    _browser->type = strdup(type);
    _browser->serviceList = NULL;
    YMmDNSBrowserRef browser = (YMmDNSBrowserRef)_browser;
    YMmDNSBrowserSetServiceAppearedFunc(browser, serviceAppeared);
    YMmDNSBrowserSetServiceRemovedFunc(browser, serviceRemoved);
    return browser;
}

void _YMmDNSBrowserFree(YMTypeRef object)
{
    _YMmDNSBrowser *browser = (_YMmDNSBrowser *)object;
    if ( browser->type )
        free(browser->type);
    
#ifdef YMmDNS_ENUMERATION
    if ( browser->enumerateServiceRef )
        free(browser->enumerateServiceRef);
    if ( browser->enumerateEventThread )
        YMFree(browser->enumerateEventThread)
#endif
        
    if ( browser->browseServiceRef )
        free(browser->browseServiceRef);
    if ( browser->browseEventThread )
        YMFree(browser->browseEventThread);
    
    if ( browser->resolveServiceRef )
        free(browser->resolveServiceRef);
    
    if ( browser->serviceList )
        _YMmDNSServiceListFree((YMmDNSServiceList *)(browser->serviceList));
}

void YMmDNSBrowserSetServiceAppearedFunc(YMmDNSBrowserRef browser, ym_mdns_service_appeared_func func)
{
    ((_YMmDNSBrowser *)browser)->serviceAppeared = func;
}

void YMmDNSBrowserSetServiceRemovedFunc(YMmDNSBrowserRef browser, ym_mdns_service_removed_func func)
{
    ((_YMmDNSBrowser *)browser)->serviceRemoved = func;
}

void YMmDNSBrowserSetServiceUpdatedFunc(YMmDNSBrowserRef browser, ym_mdns_service_updated_func func)
{
    ((_YMmDNSBrowser *)browser)->serviceUpdated = func;
}

void YMmDNSBrowserSetServiceResolvedFunc(YMmDNSBrowserRef browser, ym_mdns_service_resolved_func func)
{
    ((_YMmDNSBrowser *)browser)->serviceResolved = func;
}

#ifdef YMmDNS_ENUMERATION
bool YMmDNSBrowserStartEnumerating(YMmDNSBrowserRef browser)
{
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)browser;
    
    if ( _browser->enumerating )
        return false;
    _browser->enumerating = true;
    
    if ( _browser->enumerateServiceRef )
        free(_browser->enumerateServiceRef);
    _browser->enumerateServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    
    DNSServiceErrorType result = DNSServiceEnumerateDomains(_browser->enumerateServiceRef, // DNSServiceRef
                                                            kDNSServiceFlagsBrowseDomains, // DNSServiceFlags
                                                            0, // interfaceIndex
                                                            _YMmDNSEnumerateReply, // DNSServiceDomainEnumReply
                                                            browser ); // context
    if( result != kDNSServiceErr_NoError )
    {
        YMLog("YMmDNSBrowserStartEnumerating: %ld ", result);
        return false;
    }
    
    if ( _browser->enumerateEventThread )
        YMFree(_browser->enumerateEventThread);
    _browser->enumerateEventThread = YMThreadCreate(_YMmDNSBrowserThread, browser);
    bool okay = YMThreadStart(_browser->enumerateEventThread);
    
    return okay;
}
#endif

bool YMmDNSBrowserStartEnumerating(YMmDNSBrowserRef browser)
{
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)browser;
    
    if ( _browser->browsing )
        return false;
    _browser->browsing = true;
    
    if ( _browser->browseServiceRef )
        free( _browser->browseServiceRef );
    _browser->browseServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    
    DNSServiceErrorType result = DNSServiceBrowse(_browser->browseServiceRef, // DNSServiceRef
                                                  0, // DNSServiceFlags
                                                  0, // interfaceIndex
                                                  _browser->type, // type
                                                  NULL, // domain
                                                  _YMmDNSBrowseCallback, // callback
                                                  browser); // context
    
    if( result != kDNSServiceErr_NoError )
    {
        YMLog("YMmDNSBrowserStartEnumerating: %d", result);
        return false;
    }
    
    _browser->browseEventThread = YMThreadCreate(_YMmDNSBrowserEventThread, browser);
    bool okay = YMThreadStart(_browser->browseEventThread);
    
    return okay;
}

bool YMmDNSBrowserResolve(YMmDNSBrowserRef browser, const char *serviceName, ym_mdns_service_resolved_func resolveFunc)
{
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)browser;
    _browser->resolveServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    
    // unsure if error checking below and resolution could race each other, and it shouldn't matter if we play it safe this way
    _browser->resolveFunc = resolveFunc;
    
    DNSServiceErrorType result = DNSServiceResolve ( _browser->resolveServiceRef, // DNSServiceRef
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex
                                                    serviceName,
                                                    _browser->type, // type
                                                    0, // domain
                                                    _YMmDNSResolveCallback,
                                                    browser );
    if ( result != kDNSServiceErr_NoError )
        return false;
    
    result = DNSServiceProcessResult( *(_browser->resolveServiceRef) );
    if ( result != kDNSServiceErr_NoError )
        return false;
    
    return true;
}

void _YMmDNSBrowserAddService(_YMmDNSBrowser *browser, YMmDNSServiceRecord *record)
{
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)browser;
    YMmDNSServiceRecord *_record = (YMmDNSServiceRecord *)record;
    YMmDNSServiceList *aService = (YMmDNSServiceList *)_browser->serviceList,
                        *previousService = (YMmDNSServiceList *)_browser->serviceList;
    while ( aService )
    {
        if ( 0 == strcmp(_record->name,((YMmDNSServiceRecord *)aService->service)->name) )
            YMLog("warning: adding service '%s' to %s browser when it already exists");
        previousService = aService;
        aService = aService->next;
    }
}

void _YMmDNSBrowserUpdateService(_YMmDNSBrowser *browser, YMmDNSServiceRecord *record)
{
    YMmDNSServiceList *aService = (YMmDNSServiceList *)browser->serviceList,
                        *previousService = (YMmDNSServiceList *)browser->serviceList;
    while ( aService )
    {
        if ( 0 == strcmp(record->name,((YMmDNSServiceRecord *)aService->service)->name) )
        {
            previousService = aService;
            aService = aService->next;
        }
    }
}

#define _YMmDNSServiceFoundAndRemovedHack ((YMmDNSServiceRecord *)0xEA75F00D)
YMmDNSServiceRecord *_YMmDNSBrowserGetServiceWithName(_YMmDNSBrowser *browser, const char *name, bool remove)
{
    YMmDNSServiceRecord *matchedRecord = NULL;
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)browser;
    YMmDNSServiceList *aService = (YMmDNSServiceList *)_browser->serviceList,
                        *previousService = (YMmDNSServiceList *)_browser->serviceList;
    while ( aService )
    {
        YMmDNSServiceRecord *aRecord = (YMmDNSServiceRecord *)aService;
        if ( 0 == strcmp(aRecord->name, name) )
        {
            if ( remove )
            {
                previousService->next = aService->next; // nil or not
                _YMmDNSServiceRecordFree(aRecord);
                matchedRecord = _YMmDNSServiceFoundAndRemovedHack;
            }
            else
                matchedRecord = aRecord;
            break;
        }
        
        previousService = aService;
        aService = aService->next;
    }
    
    return matchedRecord;
}

YMmDNSServiceRecord *YMmDNSBrowserGetServiceWithName(YMmDNSBrowserRef browser, const char *name)
{
    return _YMmDNSBrowserGetServiceWithName((_YMmDNSBrowser *)browser, name, false);
}

void _YMmDNSBrowserRemoveServiceNamed(_YMmDNSBrowser *browser, const char *name)
{
    _YMmDNSBrowserGetServiceWithName(browser, name, true);
}

#ifdef YMmDNS_ENUMERATION
void DNSSD_API _YMmDNSEnumerateReply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *replyDomain, void *context)
{
    YMLog("YMmDNSEnumerateReply: flags: %04x", flags);
}
#endif

static void DNSSD_API _YMmDNSBrowseCallback(DNSServiceRef serviceRef, DNSServiceFlags flags, uint32_t ifIdx, DNSServiceErrorType result, const char *name, const char *type,
                                            const char *domain, void *context)
{
    YMLog("_YMmDNSBrowseCallback: %s/%s:?: if: %u flags: %04x ", type, name, ifIdx, result);
    _YMmDNSBrowser *browser = (_YMmDNSBrowser *)context;
    
    // "An enumeration callback with the "Add" flag NOT set indicates a "Remove", i.e. the domain is no longer valid.
    bool remove = (flags & kDNSServiceFlagsAdd) == 0;
    
    if ( remove )
        _YMmDNSBrowserRemoveServiceNamed(browser, name);
    else
    {
        YMmDNSServiceRecord *record = _YMmDNSCreateServiceRecord(name, type, domain, false, 0, NULL);
        _YMmDNSBrowserAddService(browser, record);
    }
    
}

void DNSSD_API _YMmDNSResolveCallback(DNSServiceRef serviceRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType result, const char *name,
                                      const char *host, uint16_t port, uint16_t txtLen, const unsigned char *txtRecord, void *context )
{
    _YMmDNSBrowser *browser = (_YMmDNSBrowser *)context;
    uint16_t hostPort = ntohs(port);
    
    bool okay = ( result == kDNSServiceErr_NoError );
    
    YMmDNSServiceRecord *record = NULL;
    if ( okay )
    {
        record = _YMmDNSCreateServiceRecord(name, browser->type,
#ifdef YMmDNS_ENUMERATION
#error fixme
#else
                                            NULL,
#endif
                                            true, hostPort, txtRecord); // could be optimized
        _YMmDNSBrowserUpdateService(browser, record);
    }
    
    if ( browser->resolveFunc )
        browser->resolveFunc((YMmDNSBrowserRef)browser, record, true);
        
    DNSServiceRefDeallocate( serviceRef );
    free( serviceRef );
    browser->resolveServiceRef = NULL;
}

void *_YMmDNSBrowserEventThread( void *context )
{
    _YMmDNSBrowser *browser = (_YMmDNSBrowser *)context;
    bool keepGoing = true;
    while ( keepGoing )
    {
        int fd  = DNSServiceRefSockFD(
#ifdef YMmDNS_ENUMERATION
#error fixme
#else
                                      *(browser->browseServiceRef)
#endif
                                      );
        int nfds = fd + 1;
        fd_set readfds;
        fd_set* nullFd = (fd_set*) NULL;
        struct timeval tv;
        int result;
        
        // 1. Set up the fd_set as usual here.
        FD_ZERO(&readfds);
        
        // 2. Add the fd to the fd_set
        FD_SET(fd , &readfds);
        
        // 3. Set up the timeout.
        tv.tv_sec = 0; // wakes up every .5 sec if no socket activity occurs
        tv.tv_usec = 500000;
        
        // wait for pending data or .5 secs to elapse:
        result = select(nfds, &readfds, nullFd, nullFd, &tv);
        if (result > 0)
        {
            DNSServiceErrorType err = kDNSServiceErr_NoError;
            if (FD_ISSET(fd , &readfds))
            {
                err = DNSServiceProcessResult(
#ifdef YMmDNS_ENUMERATION
#error fixme
#else
                                              *(browser->browseServiceRef)
#endif
                                              );
            }
            if (err != kDNSServiceErr_NoError)
            {
                fprintf(stderr,
                        "DNSServiceProcessResult returned %d\n", err);
                keepGoing = false;
            }
        }
        else if (result == 0)
        {
            // timeout elapsed but no fd-s were signalled.
        }
        else
        {
            YMLog("mDNS browser event select returned: %d: %d (%s)",result,errno,strerror(errno));
            keepGoing = false;
        }
    }
    
    YMLog("mDNS event thread exiting");
    return 0;
}