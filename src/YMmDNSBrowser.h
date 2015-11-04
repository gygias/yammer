//
//  YMmDNSBrowser.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMmDNSBrowser_h
#define YMmDNSBrowser_h

#include "YMBase.h"
#include "YMmDNS.h"

#include <dns_sd.h>

typedef struct _YMmDNSBrowser *YMmDNSBrowserRef;

typedef void (*ym_mdns_service_appeared_func)(YMmDNSBrowserRef, YMmDNSServiceRecord *);
typedef void (*ym_mdns_service_removed_func)(YMmDNSBrowserRef, YMmDNSServiceRecord *);
typedef void (*ym_mdns_service_updated_func)(YMmDNSBrowserRef, YMmDNSServiceRecord *);
typedef void (*ym_mdns_service_resolved_func)(YMmDNSBrowserRef, YMmDNSServiceRecord *, bool);

YMmDNSBrowserRef YMmDNSBrowserCreate(char *type, ym_mdns_service_appeared_func serviceAppeared, ym_mdns_service_removed_func serviceRemoved);

// ended up putting these in the constructor, but they may be useful to sequester certain events
void YMmDNSBrowserSetServiceAppearedFunc(YMmDNSBrowserRef browser, ym_mdns_service_appeared_func func);
void YMmDNSBrowserSetServiceRemovedFunc(YMmDNSBrowserRef browser, ym_mdns_service_removed_func func);
void YMmDNSBrowserSetServiceUpdatedFunc(YMmDNSBrowserRef browser, ym_mdns_service_updated_func func);
void YMmDNSBrowserSetServiceResolvedFunc(YMmDNSBrowserRef browser, ym_mdns_service_resolved_func func);

#ifdef YMmDNS_ENUMERATION
bool YMmDNSBrowserStartEnumerating(YMmDNSBrowserRef browser);
bool YMmDNSBrowserStopEnumerating(YMmDNSBrowserRef browser);
#endif

bool YMmDNSBrowserStartBrowsing(YMmDNSBrowserRef browser);
bool YMmDNSBrowserStopBrowsing(YMmDNSBrowserRef browser);

bool YMmDNSBrowserResolve(YMmDNSBrowserRef browser, const char *serviceName, ym_mdns_service_resolved_func);

YMmDNSServiceRecord *YMmDNSBrowserGetServiceWithName(YMmDNSBrowserRef browser, const char *name);

#endif /* YMmDNSBrowser_h */
