//
//  YMConnection.m
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YMConnection.h"
#import "YMConnectionPriv.h"

#import "YMStreamPriv.h"

@interface YMConnection ()

@property (nonatomic) YMConnectionRef connectionRef;

@property (nonatomic,retain) NSMutableArray *streams;

@end

@implementation YMConnection

- (id)_initWithConnectionRef:(YMConnectionRef)connectionRef
{
    if ( ( self = [super init] ) )
    {
        self.connectionRef = YMRetain(connectionRef);
        self.streams = [NSMutableArray array];
    }
    
    return self;
}

- (void)dealloc
{
    YMRelease(self.connectionRef);
}

- (BOOL)_isEqualToRef:(YMConnectionRef)connectionRef
{
    return ( self.connectionRef == connectionRef );
}

- (YMStream *)_streamForRef:(YMStreamRef)streamRef
{
    __block YMStream *theStream = nil;
    [self.streams enumerateObjectsUsingBlock:^(id  _Nonnull obj, __unused NSUInteger idx, BOOL * _Nonnull stop) {
        YMStream *aStream = (YMStream *)obj;
        if ( [aStream _isEqualToRef:streamRef] )
        {
            theStream = aStream;
            *stop = YES;
        }
    }];
    
    return theStream;
}

@end
