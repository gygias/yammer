//
//  YMConnectionPriv.h
//  yammer
//
//  Created by david on 11/15/15.
//  Copyright © 2015 Combobulated Software. All rights reserved.
//

#ifndef YMConnectionPriv_h
#define YMConnectionPriv_h

// allows a connection-level test to clean up properly,
// but to clients, session should be stopped, not individual connections
bool _YMConnectionClose(YMConnectionRef connection);

#endif /* YMConnectionPriv_h */
