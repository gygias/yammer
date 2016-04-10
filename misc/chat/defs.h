//
//  defs.h
//  yammer
//
//  Created by david on 11/16/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef defs_h
#define defs_h

#if defined(YMWIN32) || defined(YMLINUX)
#define __unused
#endif

void _added_peer_func(YMSessionRef session, YMPeerRef peer, void* context);
void _removed_peer_func(YMSessionRef session, YMPeerRef peer, void* context);
void _resolve_failed_func(YMSessionRef session, YMPeerRef peer, void* context);
void _resolved_func(YMSessionRef session, YMPeerRef peer, void* context);
void _connect_failed_func(YMSessionRef session, YMPeerRef peer, bool moreComing, void* context);
bool _ym_session_should_accept_func(YMSessionRef session, YMPeerRef peer, void* context);
void _connected_func(YMSessionRef session,YMConnectionRef connection, void* context);
void _interrupted_func(YMSessionRef session, void* context);
void _new_stream_func(__unused YMSessionRef session, __unused YMConnectionRef connection, YMStreamRef stream, __unused void* context);
void _closing_func(__unused YMSessionRef session, __unused YMConnectionRef connection, __unused YMStreamRef stream, __unused void* context);

#endif /* defs_h */
