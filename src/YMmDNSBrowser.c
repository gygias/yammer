//
//  YMmDNSBrowser.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMmDNSBrowser.h"

#include "YMUtilities.h"
#include "YMThread.h"

#include <errno.h>

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogmDNS
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

typedef struct __ym_mdns_browser
{
    _YMType _type;
    
    YMStringRef type;
    YMmDNSServiceList *serviceList; // convert to collection
    
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
    
    uint16_t debugExpectedPairs;
} ___ym_mdns_browser;
typedef struct __ym_mdns_browser __YMmDNSBrowser;
typedef __YMmDNSBrowser *__YMmDNSBrowserRef;

#pragma mark callback/event goo
#ifdef YMmDNS_ENUMERATION
void DNSSD_API __YMmDNSEnumerateReply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *replyDomain, void *context);
#endif
static void DNSSD_API __ym_mdns_browse_callback(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *serviceName,
                                            const char *reg_type, const char *reply_domain, void *context);
void DNSSD_API __ym_mdns_resolve_callback(DNSServiceRef serviceRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *name,
                                      const char *host, uint16_t port, uint16_t txtLen, const unsigned char *txtRecord, void *context );

void __ym_mdns_browser_event_proc(void *);
void __YMmDNSBrowserAddOrUpdateService(__YMmDNSBrowserRef browser, YMmDNSServiceRecord *record);
void __YMmDNSBrowserRemoveServiceNamed(__YMmDNSBrowserRef browser, YMStringRef name);

YMmDNSServiceRecord *__YMmDNSBrowserGetServiceWithName(__YMmDNSBrowserRef browser, YMStringRef name, bool remove);

YMmDNSBrowserRef YMmDNSBrowserCreate(YMStringRef type)
{
    return YMmDNSBrowserCreateWithCallbacks(type, NULL, NULL, NULL, NULL, NULL);
}

YMmDNSBrowserRef YMmDNSBrowserCreateWithCallbacks(YMStringRef type,
                                                  ym_mdns_service_appeared_func serviceAppeared,
                                                  ym_mdns_service_updated_func serviceUpdated,
                                                  ym_mdns_service_resolved_func serviceResolved,
                                                  ym_mdns_service_removed_func serviceRemoved,
                                                  void *context)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)_YMAlloc(_YMmDNSBrowserTypeID,sizeof(__YMmDNSBrowser));
    
    browser->type = YMRetain(type);
    browser->serviceList = NULL;
    YMmDNSBrowserSetServiceAppearedFunc(browser, serviceAppeared);
    YMmDNSBrowserSetServiceUpdatedFunc(browser, serviceUpdated);
    YMmDNSBrowserSetServiceResolvedFunc(browser, serviceResolved);
    YMmDNSBrowserSetServiceRemovedFunc(browser, serviceRemoved);
    YMmDNSBrowserSetCallbackContext(browser, context);
    
    browser->debugExpectedPairs = UINT16_MAX;
    return browser;
}

void _YMmDNSBrowserFree(YMTypeRef object)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)object;
    if ( browser->type )
        YMRelease(browser->type);
    
#ifdef YMmDNS_ENUMERATION
    if ( browser->enumerateServiceRef )
        free(browser->enumerateServiceRef);
    if ( browser->enumerateEventThread )
        YMRelease(browser->enumerateEventThread)
#endif
        
    if ( browser->browseServiceRef )
        free(browser->browseServiceRef);
    if ( browser->browseEventThread )
        YMRelease(browser->browseEventThread);
    
    if ( browser->resolveServiceRef )
        free(browser->resolveServiceRef);
    
    if ( browser->serviceList )
        _YMmDNSServiceListFree((YMmDNSServiceList *)(browser->serviceList));
}

void YMmDNSBrowserSetServiceAppearedFunc(YMmDNSBrowserRef browser_, ym_mdns_service_appeared_func func)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->serviceAppeared = func;
}

void YMmDNSBrowserSetServiceRemovedFunc(YMmDNSBrowserRef browser_, ym_mdns_service_removed_func func)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->serviceRemoved = func;
}

void YMmDNSBrowserSetServiceUpdatedFunc(YMmDNSBrowserRef browser_, ym_mdns_service_updated_func func)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->serviceUpdated = func;
}

void YMmDNSBrowserSetServiceResolvedFunc(YMmDNSBrowserRef browser_, ym_mdns_service_resolved_func func)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->serviceResolved = func;
}

void YMmDNSBrowserSetCallbackContext(YMmDNSBrowserRef browser_, void *context)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->callbackContext = context;
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
    char *threadName = YMStringCreateWithFormat("mdns-enum-%s",YMSTR(browser->type));
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

bool YMmDNSBrowserStart(YMmDNSBrowserRef browser_)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    
    if ( browser->browsing )
        return false;
    browser->browsing = true;
    
    if ( browser->browseServiceRef )
        free( browser->browseServiceRef );
    browser->browseServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    
    DNSServiceErrorType result = DNSServiceBrowse(browser->browseServiceRef, // DNSServiceRef
                                                  0, // DNSServiceFlags
                                                  0, // interfaceIndex
                                                  YMSTR(browser->type), // type
                                                  NULL, // domain
                                                  __ym_mdns_browse_callback, // callback
                                                  browser); // context
    
    if( result != kDNSServiceErr_NoError )
    {
        ymlog("YMmDNSBrowserStartEnumerating: %d", result);
        return false;
    }
    
    YMStringRef threadName = YMStringCreateWithFormat("mdns-browse-%s",YMSTR(browser->type),NULL);
    browser->browseEventThread = YMThreadCreate(threadName, __ym_mdns_browser_event_proc, browser);
    YMRelease(threadName);
    
    bool okay = YMThreadStart(browser->browseEventThread);
    
    return okay;
}

bool YMmDNSBrowserStop(YMmDNSBrowserRef browser_)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    
    if ( browser->browseServiceRef )
    {
        DNSServiceRefDeallocate(*(browser->browseServiceRef));
        free(browser->browseServiceRef); // not sure this is right
        browser->browseServiceRef = NULL;
    }
    return true;
}

bool YMmDNSBrowserResolve(YMmDNSBrowserRef browser_, YMStringRef serviceName)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    
    YMmDNSServiceRecord *theService = __YMmDNSBrowserGetServiceWithName(browser, serviceName, false);
    if ( ! theService )
    {
        ymlog("YMmDNS asked to resolve '%s', but it doesn't have a record of this service",YMSTR(serviceName));
        return false;
    }
    
    browser->resolveServiceRef = (DNSServiceRef *)calloc( 1, sizeof(DNSServiceRef) );
    DNSServiceErrorType result = DNSServiceResolve ( browser->resolveServiceRef, // DNSServiceRef
                                                    0, // DNSServiceFlags
                                                    0, // interfaceIndex
                                                    YMSTR(serviceName),
                                                    YMSTR(browser->type), // type
                                                    YMSTR(theService->domain), // domain
                                                    __ym_mdns_resolve_callback,
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

void __YMmDNSBrowserAddOrUpdateService(__YMmDNSBrowserRef browser, YMmDNSServiceRecord *record)
{
    YMmDNSServiceList *aListItem = (YMmDNSServiceList *)browser->serviceList;
    // update?
    while ( aListItem )
    {
        if ( 0 == strcmp(YMSTR(record->name),YMSTR(((YMmDNSServiceRecord *)aListItem->service)->name)) )
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
    YMmDNSServiceList *newItem = (YMmDNSServiceList *)YMALLOC(sizeof(YMmDNSServiceList));
    newItem->service = record;
    newItem->next = aListItem;
    browser->serviceList = newItem;
    
    if ( browser->serviceAppeared )
        browser->serviceAppeared((YMmDNSBrowserRef)browser, record, browser->callbackContext);
}

#define _YMmDNSServiceFoundAndRemovedHack ((YMmDNSServiceRecord *)0xEA75F00D)
YMmDNSServiceRecord *__YMmDNSBrowserGetServiceWithName(__YMmDNSBrowserRef browser, YMStringRef name, bool remove)
{    
    YMmDNSServiceRecord *matchedRecord = NULL;
    YMmDNSServiceList *aListItem = (YMmDNSServiceList *)browser->serviceList,
                        *previousListItem = (YMmDNSServiceList *)browser->serviceList;
    while ( aListItem )
    {
        YMmDNSServiceRecord *aRecord = aListItem->service;
        if ( 0 == strcmp(YMSTR(aRecord->name), YMSTR(name)) )
        {
            if ( remove )
            {
                previousListItem->next = aListItem->next; // nil or not
                if ( aListItem == browser->serviceList )
                    browser->serviceList = NULL;
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

YMmDNSServiceRecord *YMmDNSBrowserGetServiceWithName(YMmDNSBrowserRef browser_, YMStringRef name)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    return __YMmDNSBrowserGetServiceWithName(browser, name, false);
}

void __YMmDNSBrowserRemoveServiceNamed(__YMmDNSBrowserRef browser, YMStringRef name)
{
    __YMmDNSBrowserGetServiceWithName(browser, name, true);
    if ( browser->serviceRemoved )
        browser->serviceRemoved((YMmDNSBrowserRef)browser, name, browser->callbackContext);
}

#ifdef YMmDNS_ENUMERATION
void DNSSD_API __YMmDNSEnumerateReply(DNSServiceRef sdRef, DNSServiceFlags flags, uint32_t interfaceIndex, DNSServiceErrorType errorCode, const char *replyDomain, void *context)
{
    ymlog("YMmDNSEnumerateReply: flags: %04x", flags);
}
#endif

static void DNSSD_API __ym_mdns_browse_callback(__unused DNSServiceRef serviceRef,
                                                DNSServiceFlags flags,
                                                __unused uint32_t ifIdx,
                                                DNSServiceErrorType result,
                                                const char *name,
                                                const char *type,
                                                const char *domain,
                                                void *context)
{
    
    //ymlog("__ym_mdns_browse_callback: %s/%s:?: if: %u flags: %04x", type, name, ifIdx, result);
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)context;
    if ( result != kDNSServiceErr_NoError )
    {
        ymerr("mDNS[%s]: error: browse callback: %d",YMSTR(browser->type),result);
        return;
    }
    
    // "An enumeration callback with the "Add" flag NOT set indicates a "Remove", i.e. the domain is no longer valid.
    bool remove = (flags & kDNSServiceFlagsAdd) == 0;
    
    if ( remove )
        __YMmDNSBrowserRemoveServiceNamed(browser, YMSTRC(name));
    else
    {
        YMmDNSServiceRecord *record = _YMmDNSCreateServiceRecord(name, type, domain, false, NULL, 0, NULL, 0);
        __YMmDNSBrowserAddOrUpdateService(browser, record);
    }    
}

void DNSSD_API __ym_mdns_resolve_callback(__unused DNSServiceRef serviceRef,
                                          __unused DNSServiceFlags flags,
                                          __unused uint32_t interfaceIndex,
                                          DNSServiceErrorType result,
                                          const char *fullname,
                                          const char *host,
                                          uint16_t port,
                                          uint16_t txtLength,
                                          const unsigned char *txtRecord,
                                          void *context )
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)context;
    //ymlog("__ym_mdns_resolve_callback: %s/%s -> %s:%u",browser->type,fullname,host,(unsigned)port);
    uint16_t hostPort = ntohs(port);
    
    bool okay = ( result == kDNSServiceErr_NoError );
    
    YMmDNSServiceRecord *record = NULL;
    if ( okay )
    {
        // fullname:        The full service domain name, in the form <servicename>.<protocol>.<domain>.
        char *firstDotPtr = strstr(fullname, ".");
        if ( ! firstDotPtr )
        {
            ymlog("__ym_mdns_resolve_callback doesn't know how to parse name '%s'",fullname);
            okay = false;
            goto catch_callback_and_release;
        }
        firstDotPtr[0] = '\0';
        
        record = _YMmDNSCreateServiceRecord(fullname, YMSTR(browser->type),
#ifdef YMmDNS_ENUMERATION
#error fixme
#else
                                            NULL,
#endif
                                            true, host, ntohs(hostPort), txtRecord, txtLength); // could be optimized
        
        __YMmDNSBrowserAddOrUpdateService(browser, record);
    }
    
catch_callback_and_release:
    if ( browser->serviceResolved )
        browser->serviceResolved((YMmDNSBrowserRef)browser, okay, record, browser->callbackContext);
    
    //DNSServiceRefDeallocate( *(browser->resolveServiceRef) );
    //free( browser->resolveServiceRef );
    //browser->resolveServiceRef = NULL;
}

// this function was mostly lifted from "Zero Configuration Networking: The Definitive Guide" -o'reilly
void __ym_mdns_browser_event_proc( void *ctx )
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)ctx;
    int fd  = DNSServiceRefSockFD(
#ifdef YMmDNS_ENUMERATION
#error fixme
#else
                                  *(browser->browseServiceRef)
#endif
                                  );
    
    ymlog("mDNS event thread %d entered",fd);
    
    bool keepGoing = true;
    while ( keepGoing )
    {
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
                ymerr("mdns: DNSServiceProcessResult on %d returned %d", fd, err);
                keepGoing = false;
            }
        }
        else if (result == 0)
        {
            // timeout elapsed but no fd-s were signalled.
        }
        else
        {
            ymlog("mDNS: browser select on %d returned: %d: %d (%s)",fd, result,errno,strerror(errno));
            keepGoing = false;
        }
    }
    
    ymlog("mDNS event thread %d exiting",fd);
}

void _YMmDNSBrowserDebugSetExpectedTxtKeyPairs(YMmDNSBrowserRef browser_, uint16_t nPairs)
{
    __YMmDNSBrowserRef browser = (__YMmDNSBrowserRef)browser_;
    browser->debugExpectedPairs = nPairs;
}
