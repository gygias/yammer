//
//  YMSession.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSession_h
#define YMSession_h

#ifdef __cplusplus
extern "C" {
#endif

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

YMAPI YMSessionRef YMSessionCreate(YMStringRef type);

YMAPI void YMSessionSetBrowsingCallbacks(YMSessionRef session, ym_session_added_peer_func added, ym_session_removed_peer_func removed,
                                 ym_session_resolve_failed_func rFailed, ym_session_resolved_peer_func resolved,
                                 ym_session_connect_failed_func cFailed, void *context);
YMAPI void YMSessionSetAdvertisingCallbacks(YMSessionRef session,ym_session_should_accept_func should, void* context);
YMAPI void YMSessionSetCommonCallbacks(YMSessionRef session, ym_session_connected_func connected, ym_session_interrupted_func interrupted,
                                 ym_session_new_stream_func new_, ym_session_stream_closing_func closing);

YMAPI bool YMSessionStartAdvertising(YMSessionRef session, YMStringRef name);
YMAPI bool YMSessionStopAdvertising(YMSessionRef session);

YMAPI bool YMSessionStartBrowsing(YMSessionRef session);
YMAPI YMPeerRef YMSessionGetPeerNamed(YMSessionRef session, YMStringRef peerName);
YMAPI bool YMSessionResolvePeer(YMSessionRef session, YMPeerRef peer);
YMAPI bool YMSessionConnectToPeer(YMSessionRef session, YMPeerRef peer, bool sync);
YMAPI bool YMSessionStopBrowsing(YMSessionRef session);

YMAPI bool YMSessionCloseAllConnections(YMSessionRef session);

YMAPI YMConnectionRef YMSessionGetDefaultConnection(YMSessionRef session);
YMAPI YMDictionaryRef YMSessionGetConnections(YMSessionRef session);

#ifdef __cplusplus
}
#endif

#endif /* YMSession_h */
