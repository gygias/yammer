//
//  defs.h
//  yammer
//
//  Created by david on 11/16/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef defs_h
#define defs_h

void _ym_session_added_peer_func(YMSessionRef session, YMPeerRef peer, void* context);
void _ym_session_removed_peer_func(YMSessionRef session, YMPeerRef peer, void* context);
void _ym_session_resolve_failed_func(YMSessionRef session, YMPeerRef peer, void* context);
void _ym_session_resolved_peer_func(YMSessionRef session, YMPeerRef peer, void* context);
void _ym_session_connect_failed_func(YMSessionRef session, YMPeerRef peer, void* context);
bool _ym_session_should_accept_func(YMSessionRef session, YMPeerRef peer, void* context);
void _ym_session_connected_func(YMSessionRef session,YMConnectionRef connection, void* context);
void _ym_session_interrupted_func(YMSessionRef session, void* context);
void _ym_session_new_stream_func(YMSessionRef session, YMStreamRef stream, void* context);
void _ym_session_stream_closing_func(YMSessionRef session, YMStreamRef stream, void* context);

#endif /* defs_h */
