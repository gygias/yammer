//
//  YMSession.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSession_h
#define YMSession_h

#include <libyammer/YMBase.h>

#include <libyammer/YMString.h>
#include <libyammer/YMDictionary.h>
#include <libyammer/YMPeer.h>
#include <libyammer/YMConnection.h>

typedef YMTypeRef YMSessionRef;

// browsing, discover & connection
typedef void(*ym_session_added_peer_func)(YMSessionRef,YMPeerRef,void*);
typedef void(*ym_session_removed_peer_func)(YMSessionRef,YMPeerRef,void*);
typedef void(*ym_session_resolve_failed_func)(YMSessionRef,YMPeerRef,void*);
typedef void(*ym_session_resolved_peer_func)(YMSessionRef,YMPeerRef,void*);
typedef void(*ym_session_connect_failed_func)(YMSessionRef,YMPeerRef,void*);

// advertising
typedef bool(*ym_session_should_accept_func)(YMSessionRef,YMPeerRef,void*);

// connection & streams
typedef void(*ym_session_connected_func)(YMSessionRef,YMConnectionRef,void*);
typedef void(*ym_session_interrupted_func)(YMSessionRef,void*);
typedef void(*ym_session_new_stream_func)(YMSessionRef,YMConnectionRef,YMStreamRef,void*);
typedef void(*ym_session_stream_closing_func)(YMSessionRef,YMConnectionRef,YMStreamRef,void*);

YMSessionRef YMSessionCreate(YMStringRef type);

void YMSessionSetBrowsingCallbacks(YMSessionRef session, ym_session_added_peer_func added, ym_session_removed_peer_func removed,
                                 ym_session_resolve_failed_func rFailed, ym_session_resolved_peer_func resolved,
                                 ym_session_connect_failed_func cFailed, void *context);
void YMSessionSetAdvertisingCallbacks(YMSessionRef session,ym_session_should_accept_func should, void* context);
void YMSessionSetCommonCallbacks(YMSessionRef session, ym_session_connected_func connected, ym_session_interrupted_func interrupted,
                                 ym_session_new_stream_func new_, ym_session_stream_closing_func closing);

bool YMSessionStartAdvertising(YMSessionRef session, YMStringRef name);
bool YMSessionStopAdvertising(YMSessionRef session);

bool YMSessionStartBrowsing(YMSessionRef session);
YMPeerRef YMSessionGetPeerNamed(YMSessionRef session, YMStringRef peerName);
bool YMSessionResolvePeer(YMSessionRef session, YMPeerRef peer);
bool YMSessionConnectToPeer(YMSessionRef session, YMPeerRef peer, bool sync);
bool YMSessionStopBrowsing(YMSessionRef session);

bool YMSessionCloseAllConnections(YMSessionRef session);

YMConnectionRef YMSessionGetDefaultConnection(YMSessionRef session);
YMDictionaryRef YMSessionGetConnections(YMSessionRef session);

#endif /* YMSession_h */