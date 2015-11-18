//
//  YMConnectionPriv.h
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMConnectionPriv_h
#define YMConnectionPriv_h

#import <yammer/YMConnection.h>

@interface YMConnection (Private)

- (id)_initWithConnectionRef:(YMConnectionRef)connectionRef;

- (BOOL)_isEqualToRef:(YMConnectionRef)connectionRef;

- (YMStream *)_streamForRef:(YMStreamRef)streamRef;

@end

#endif /* YMConnectionPriv_h */
