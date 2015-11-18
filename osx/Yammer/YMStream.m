//
//  YMStream.m
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YMStream.h"

@interface YMStream()

@property (nonatomic) YMStreamRef streamRef;

@end

@implementation YMStream

- (id)_initWithStreamRef:(YMStreamRef)streamRef
{
    if ( ( self = [super init] ) )
    {
        self.streamRef = YMRetain(streamRef);
    }
    return self;
}

- (BOOL)_isEqualToRef:(YMStreamRef)streamRef
{
    return ( self.streamRef == streamRef );
}

@end
