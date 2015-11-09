//
//  YMmDNSBrowser.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNSBrowser.h"
#include "YMPrivate.h"
#include "YMUtilities.h"

#include "YMThread.h"

#include <errno.h>

#include "YMLog.h"
#undef ymlogType
#define ymlogType YMLogmDNS
#if ( ymlogType >= ymLogTarget )
#undef ymlog
#define ymlog(x,...)
#endif

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
    YMmDNSBrowserRef browser = (YMmDNSBrowserRef)calloc(1,sizeof(struct __YMmDNSBrowser));
    browser->_typeID = _YMmDNSBrowserTypeID;
    
    browser->type = strdup(type);
    browser->serviceList = NULL;
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
    if ( browser->enumerating )
        return false;
    browser->enumerating = true;
    
    if ( browser->enumerateServiceRef )
        free(browser->enumerateServiceRef);
    browser->enumerateServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    
    DNSServiceErrorType result = DNSServiceEnumerateDomains(browser->enumerateServiceRef, // DNSServiceRef
                                                            kDNSServiceFlagsBrowseDomains, // DNSServiceFlags
                                                            0, // interfaceIndex
                                                            _YMmDNSEnumerateReply, // DNSServiceDomainEnumReply
                                                            browser ); // context
    if( result != kDNSServiceErr_NoError )
    {
        ymlog("YMmDNSBrowserStartEnumerating: %ld ", result);
        return false;
    }
    
    if ( browser->enumerateEventThread )
        YMFree(browser->enumerateEventThread);
    char *threadName = YMStringCreateWithFormat("mdns-enum-%s",browser->type);
    browser->enumerateEventThread = YMThreadCreate(threadName, _YMmDNSBrowserThread, browser);
    free(threadName);
    bool okay = YMThreadStart(_browser->enumerateEventThread);
    
    return okay;
}

bool YMmDNSBrowserEnumeratingStop(YMmDNSBrowserRef browser)
{
    if ( browser->enumerateServiceRef )
    {
        DNSServiceRefDeallocate(browser->enumerateServiceRef);
        free(browser->enumerateServiceRef); // not sure this is right
        browser->enumerateServiceRef = NULL;
    }
    return true;
}
#endif

bool YMmDNSBrowserStart(YMmDNSBrowserRef browser)
{
    if ( browser->browsing )
        return false;
    browser->browsing = true;
    
    if ( browser->browseServiceRef )
        free( browser->browseServiceRef );
    browser->browseServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    
    DNSServiceErrorType result = DNSServiceBrowse(browser->browseServiceRef, // DNSServiceRef
                                                  0, // DNSServiceFlags
                                                  0, // interfaceIndex
                                                  browser->type, // type
                                                  NULL, // domain
                                                  _YMmDNSBrowseCallback, // callback
                                                  browser); // context
    
    if( result != kDNSServiceErr_NoError )
    {
        ymlog("YMmDNSBrowserStartEnumerating: %d", result);
        return false;
    }
    
    char *threadName = YMStringCreateWithFormat("mdns-browse-%s",browser->type);
    browser->browseEventThread = YMThreadCreate(threadName, _YMmDNSBrowserEventThread, browser);
    free(threadName);
    
    bool okay = YMThreadStart(browser->browseEventThread);
    
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
    YMmDNSServiceRecord *theService = _YMmDNSBrowserGetServiceWithName(browser, serviceName, false);
    if ( ! theService )
    {
        ymlog("YMmDNS asked to resolve '%s', but it doesn't have a record of this service",serviceName);
        return false;
    }
    
    browser->resolveServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    DNSServiceErrorType result = DNSServiceResolve ( browser->resolveServiceRef, // DNSServiceRef
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex
                                                    serviceName,
                                                    browser->type, // type
                                                    theService->domain, // domain
                                                    _YMmDNSResolveCallback,
                                                    browser );
    if ( result != kDNSServiceErr_NoError )
    {
        ymlog("DNSServiceResolve failed: %d",result);
        // on error "the callback is never invoked and the DNSServiceRef is not initialized"
        // leading me to believe we free not DNSServiceRefDeallocate here
        free(browser->resolveServiceRef);
        browser->resolveServiceRef = NULL;
        return false;
    }
    
    result = DNSServiceProcessResult( *(browser->resolveServiceRef) );
    if ( result != kDNSServiceErr_NoError )
    {
        ymlog("DNSServiceProcessResult failed: %d",result);
        free(browser->resolveServiceRef);
        browser->resolveServiceRef = NULL;
        return false;
    }
    
    return true;
}

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
    YMmDNSServiceList *newItem = (YMmDNSServiceList *)YMMALLOC(sizeof(YMmDNSServiceList));
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
    YMmDNSServiceList *aListItem = (YMmDNSServiceList *)browser->serviceList,
                        *previousListItem = (YMmDNSServiceList *)browser->serviceList;
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
    ymlog("YMmDNSEnumerateReply: flags: %04x", flags);
}
#endif

static void DNSSD_API _YMmDNSBrowseCallback(__unused DNSServiceRef serviceRef, DNSServiceFlags flags, __unused uint32_t ifIdx, DNSServiceErrorType result, const char *name, const char *type,
                                            const char *domain, void *context)
{
    
    //ymlog("_YMmDNSBrowseCallback: %s/%s:?: if: %u flags: %04x", type, name, ifIdx, result);
    _YMmDNSBrowser *browser = (_YMmDNSBrowser *)context;
    if ( result != kDNSServiceErr_NoError )
    {
        ymerr("mDNS[%s]: error: browse callback: %d",browser->type,result);
        return;
    }
    
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

void DNSSD_API _YMmDNSResolveCallback(__unused DNSServiceRef serviceRef,__unused DNSServiceFlags flags, __unused uint32_t interfaceIndex, DNSServiceErrorType result, const char *fullname,
                                      const char *host, uint16_t port, uint16_t txtLength, const unsigned char *txtRecord, void *context )
{
    _YMmDNSBrowser *browser = (_YMmDNSBrowser *)context;
    //ymlog("_YMmDNSResolveCallback: %s/%s -> %s:%u",browser->type,fullname,host,(unsigned)port);
    uint16_t hostPort = ntohs(port);
    
    bool okay = ( result == kDNSServiceErr_NoError );
    
    YMmDNSServiceRecord *record = NULL;
    if ( okay )
    {
        // fullname:        The full service domain name, in the form <servicename>.<protocol>.<domain>.
        char *firstDotPtr = strstr(fullname, ".");
        if ( ! firstDotPtr )
        {
            ymlog("_YMmDNSResolveCallback doesn't know how to parse name '%s'",fullname);
            okay = false;
            goto catch_callback_and_release;
        }
        firstDotPtr[0] = '\0';
        
        record = _YMmDNSCreateServiceRecord(fullname, browser->type,
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
            ymlog("mDNS browser event select returned: %d: %d (%s)",result,errno,strerror(errno));
            keepGoing = false;
        }
    }
    
    ymlog("mDNS event thread exiting");
    return 0;
}
