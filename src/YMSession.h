//
//  YMSession.h
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMSession_h
#define YMSession_h

#include "YMBase.h"

#include "YMPeer.h"
#include "YMStream.h"
#include "YMDictionary.h"
#include "YMConnection.h"

typedef struct __YMSession *YMSessionRef;

// client, discover->connect
typedef void(*ym_session_added_peer_func)(YMSessionRef,const char*);
typedef void(*ym_session_removed_peer_func)(YMSessionRef,const char*);
typedef void(*ym_session_resolve_failed_func)(YMSessionRef,const char*);
typedef void(*ym_session_resolved_peer_func)(YMSessionRef,YMPeerRef);
typedef void(*ym_session_connect_failed_func)(YMSessionRef,const char*);

// server
typedef bool(*ym_session_should_accept_func)(YMSessionRef,YMPeerRef);

// connection
typedef void(*ym_session_connected_func)(YMSessionRef,YMConnectionRef);
typedef void(*ym_session_interrupted_func)(YMSessionRef);

// streams
typedef void(*ym_session_new_stream_func)(YMSessionRef,YMStreamRef);
typedef void(*ym_session_stream_closing_func)(YMSessionRef,YMStreamRef);

YMSessionRef YMSessionCreateClient(const char *type);
#pragma message "option to have client reserve and specify port?"
YMSessionRef YMSessionCreateServer(const char *type, const char *name);

void YMSessionSetClientCallbacks(YMSessionRef session, ym_session_added_peer_func added, ym_session_removed_peer_func removed,
                                 ym_session_resolve_failed_func rFailed, ym_session_resolved_peer_func resolved,
                                 ym_session_connect_failed_func cFailed, void *context);
void YMSessionSetServerCallbacks(YMSessionRef session,ym_session_should_accept_func should, void* context);
void YMSessionSetSharedCallbacks(YMSessionRef session, ym_session_connected_func connected, ym_session_interrupted_func interrupted,
                                 ym_session_new_stream_func new_, ym_session_stream_closing_func closing);

ymbool YMSessionServerStartAdvertising(YMSessionRef session);
ymbool YMSessionServerStopAdvertising(YMSessionRef session);

#endif /* YMSession_h */
