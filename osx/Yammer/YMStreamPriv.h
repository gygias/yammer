//
//  YMStreamPriv.h
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMStreamPriv_h
#define YMStreamPriv_h

@interface YMStream (Private)

- (id)_initWithStreamRef:(YMStreamRef)streamRef;

- (BOOL)_isEqualToRef:(YMStreamRef)streamRef;

@end

#endif /* YMStreamPriv_h */
