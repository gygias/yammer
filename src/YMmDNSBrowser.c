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
    
    ym_mdns_service_appeared_func serviceAppeared;
    ym_mdns_service_removed_func serviceRemoved;
    ym_mdns_service_updated_func serviceUpdated;
    ym_mdns_service_resolved_func serviceResolved;
    void *callbackContext;
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
void _YMmDNSBrowserAddOrUpdateService(_YMmDNSBrowser *browser, YMmDNSServiceRecord *record);
void _YMmDNSBrowserRemoveServiceNamed(_YMmDNSBrowser *browser, const char *name);

YMmDNSServiceRecord *_YMmDNSBrowserGetServiceWithName(_YMmDNSBrowser *browser, const char *name, bool remove);

YMmDNSBrowserRef YMmDNSBrowserCreate(char *type)
{
    return YMmDNSBrowserCreateWithCallbacks(type, NULL, NULL, NULL, NULL, NULL);
}

YMmDNSBrowserRef YMmDNSBrowserCreateWithCallbacks(char *type,
                                                  ym_mdns_service_appeared_func serviceAppeared,
                                                  ym_mdns_service_updated_func serviceUpdated,
                                                  ym_mdns_service_resolved_func serviceResolved,
                                                  ym_mdns_service_removed_func serviceRemoved,
                                                  void *context)
{
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)calloc(1,sizeof(_YMmDNSBrowser));
    _browser->_typeID = _YMmDNSBrowserTypeID;
    
    _browser->type = strdup(type);
    _browser->serviceList = NULL;
    YMmDNSBrowserRef browser = (YMmDNSBrowserRef)_browser;
    YMmDNSBrowserSetServiceAppearedFunc(browser, serviceAppeared);
    YMmDNSBrowserSetServiceUpdatedFunc(browser, serviceUpdated);
    YMmDNSBrowserSetServiceResolvedFunc(browser, serviceResolved);
    YMmDNSBrowserSetServiceRemovedFunc(browser, serviceRemoved);
    YMmDNSBrowserSetCallbackContext(browser, context);
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

void YMmDNSBrowserSetCallbackContext(YMmDNSBrowserRef browser, void *context)
{
    ((_YMmDNSBrowser *)browser)->callbackContext = context;
}

#ifdef YMmDNS_ENUMERATION
bool YMmDNSBrowserEnumeratingStart(YMmDNSBrowserRef browser)
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

bool YMmDNSBrowserEnumeratingStop(YMmDNSBrowserRef browser)
{
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)browser;
    if ( _browser->enumerateServiceRef )
    {
        DNSServiceRefDeallocate(_browser->enumerateServiceRef);
        free(_browser->enumerateServiceRef); // not sure this is right
        _browser->enumerateServiceRef = NULL;
    }
    return true;
}
#endif

bool YMmDNSBrowserStart(YMmDNSBrowserRef browser)
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

bool YMmDNSBrowserStop(YMmDNSBrowserRef browser)
{
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)browser;
    if ( _browser->browseServiceRef )
    {
        DNSServiceRefDeallocate(*(_browser->browseServiceRef));
        free(_browser->browseServiceRef); // not sure this is right
        _browser->browseServiceRef = NULL;
    }
    return true;
}

bool YMmDNSBrowserResolve(YMmDNSBrowserRef browser, const char *serviceName)
{
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)browser;
    
    YMmDNSServiceRecord *theService = _YMmDNSBrowserGetServiceWithName(_browser, serviceName, false);
    if ( ! theService )
    {
        YMLog("YMmDNS asked to resolve '%s', but it doesn't have a record of this service",serviceName);
        return false;
    }
    
    _browser->resolveServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    DNSServiceErrorType result = DNSServiceResolve ( _browser->resolveServiceRef, // DNSServiceRef
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex
                                                    serviceName,
                                                    _browser->type, // type
                                                    theService->domain, // domain
                                                    _YMmDNSResolveCallback,
                                                    browser );
    if ( result != kDNSServiceErr_NoError )
    {
        YMLog("DNSServiceResolve failed: %d",result);
        // on error "the callback is never invoked and the DNSServiceRef is not initialized"
        // leading me to believe we free not DNSServiceRefDeallocate here
        free(_browser->resolveServiceRef);
        _browser->resolveServiceRef = NULL;
        return false;
    }
    
    result = DNSServiceProcessResult( *(_browser->resolveServiceRef) );
    if ( result != kDNSServiceErr_NoError )
    {
        YMLog("DNSServiceProcessResult failed: %d",result);
        free(_browser->resolveServiceRef);
        _browser->resolveServiceRef = NULL;
        return false;
    }
    
    return true;
}

#warning filter or otherwise flag local services
void _YMmDNSBrowserAddOrUpdateService(_YMmDNSBrowser *browser, YMmDNSServiceRecord *record)
{
    YMmDNSServiceList *aListItem = (YMmDNSServiceList *)browser->serviceList;
    // update?
    while ( aListItem )
    {
        if ( 0 == strcmp(record->name,((YMmDNSServiceRecord *)aListItem->service)->name) )
        {
            YMmDNSServiceRecord *oldRecord = aListItem->service;
            aListItem->service = record;
            _YMmDNSServiceRecordFree(oldRecord);
            // keep the list item for now
            if ( browser->serviceUpdated )
                browser->serviceUpdated((YMmDNSBrowserRef)browser, record, browser->callbackContext);
            return;
        }
        aListItem = aListItem->next;
    }
    
    // add
    aListItem = browser->serviceList;
    YMmDNSServiceList *newItem = (YMmDNSServiceList *)malloc(sizeof(YMmDNSServiceList));
    newItem->service = record;
    newItem->next = aListItem;
    browser->serviceList = newItem;
    
    if ( browser->serviceAppeared )
        browser->serviceAppeared((YMmDNSBrowserRef)browser, record, browser->callbackContext);
}

#define _YMmDNSServiceFoundAndRemovedHack ((YMmDNSServiceRecord *)0xEA75F00D)
YMmDNSServiceRecord *_YMmDNSBrowserGetServiceWithName(_YMmDNSBrowser *browser, const char *name, bool remove)
{
    YMmDNSServiceRecord *matchedRecord = NULL;
    _YMmDNSBrowser *_browser = (_YMmDNSBrowser *)browser;
    YMmDNSServiceList *aListItem = (YMmDNSServiceList *)_browser->serviceList,
                        *previousListItem = (YMmDNSServiceList *)_browser->serviceList;
    while ( aListItem )
    {
        YMmDNSServiceRecord *aRecord = aListItem->service;
        if ( 0 == strcmp(aRecord->name, name) )
        {
            if ( remove )
            {
                previousListItem->next = aListItem->next; // nil or not
                _YMmDNSServiceRecordFree(aRecord);
                free(aListItem);
                matchedRecord = _YMmDNSServiceFoundAndRemovedHack;
            }
            else
                matchedRecord = aRecord;
            break;
        }
        
        previousListItem = aListItem;
        aListItem = aListItem->next;
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
    if ( browser->serviceRemoved )
        browser->serviceRemoved((YMmDNSBrowserRef)browser, name, browser->callbackContext);
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
    //YMLog("_YMmDNSBrowseCallback: %s/%s:?: if: %u flags: %04x", type, name, ifIdx, result);
    _YMmDNSBrowser *browser = (_YMmDNSBrowser *)context;
    
    // "An enumeration callback with the "Add" flag NOT set indicates a "Remove", i.e. the domain is no longer valid.
    bool remove = (flags & kDNSServiceFlagsAdd) == 0;
    
    if ( remove )
        _YMmDNSBrowserRemoveServiceNamed(browser, name);
    else
    {
        YMmDNSServiceRecord *record = _YMmDNSCreateServiceRecord(name, type, domain, false, NULL, 0, NULL, 0);
        _YMmDNSBrowserAddOrUpdateService(browser, record);
    }    
}

void DNSSD_API _YMmDNSResolveCallback(DNSServiceRef serviceRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType result, const char *fullname,
                                      const char *host, uint16_t port, uint16_t txtLength, const unsigned char *txtRecord, void *context )
{
    _YMmDNSBrowser *browser = (_YMmDNSBrowser *)context;
    //YMLog("_YMmDNSResolveCallback: %s/%s -> %s:%u",browser->type,fullname,host,(unsigned)port);
    uint16_t hostPort = ntohs(port);
    
    bool okay = ( result == kDNSServiceErr_NoError );
    
    YMmDNSServiceRecord *record = NULL;
    if ( okay )
    {
        // fullname:        The full service domain name, in the form <servicename>.<protocol>.<domain>.
        char *name = strdup(fullname);
        char *firstDotPtr = strstr(name, ".");
        if ( ! firstDotPtr )
        {
            YMLog("_YMmDNSResolveCallback doesn't know how to parse name '%s'",fullname);
            okay = false;
            goto catch_callback_and_release;
        }
        firstDotPtr[0] = '\0';
        
        record = _YMmDNSCreateServiceRecord(name, browser->type,
#ifdef YMmDNS_ENUMERATION
#error fixme
#else
                                            NULL,
#endif
                                            true, host, hostPort, txtRecord, txtLength); // could be optimized
        _YMmDNSBrowserAddOrUpdateService(browser, record);
    }
    
catch_callback_and_release:
    if ( browser->serviceResolved )
        browser->serviceResolved((YMmDNSBrowserRef)browser, okay, record, browser->callbackContext);
    
    //DNSServiceRefDeallocate( *(browser->resolveServiceRef) );
    //free( browser->resolveServiceRef );
    //browser->resolveServiceRef = NULL;
}

// this function was mostly lifted from "Zero Configuration Networking: The Definitive Guide" -o'reilly
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