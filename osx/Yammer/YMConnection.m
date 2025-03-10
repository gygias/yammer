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
#import "YMUtilities.h"

#import <libyammer/YMString.h>

@interface YMConnection ()

@property (nonatomic) YMConnectionRef connectionRef;

@property (nonatomic,retain) NSMutableArray *streams;

@end

@implementation YMConnection

- (id)_initWithConnectionRef:(YMConnectionRef)connectionRef
{
    if ( ( self = [super init] ) ) {
        self.connectionRef = YMRetain(connectionRef);
        self.streams = [NSMutableArray array];
    }
    
    return self;
}

- (void)dealloc
{
    if ( self.connectionRef )
        YMRelease(self.connectionRef);
    
    //[super dealloc]; // arc
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
        if ( [aStream _isEqualToRef:streamRef] ) {
            theStream = aStream;
            *stop = YES;
        }
    }];
    
    if ( ! theStream ) {
        theStream = [[YMStream alloc] _initWithStreamRef:streamRef];
        [self.streams addObject:theStream];
    }
    
    
    return theStream;
}

- (NSString *)localInterfaceName
{
    YMStringRef ymstr = YMConnectionGetLocalInterfaceName(self.connectionRef);
    return [NSString stringWithUTF8String:YMStringGetCString(ymstr)];
}

- (YMInterfaceType)localInterfaceType
{
    return YMConnectionGetLocalInterface(self.connectionRef);
}

- (NSString *)localInterfaceDescription
{
    return [NSString stringWithUTF8String:YMInterfaceTypeDescription(self.localInterfaceType)];
}

- (YMInterfaceType)remoteInterfaceType
{
    return YMConnectionGetRemoteInterface(self.connectionRef);
}

- (NSString *)remoteInterfaceDescription
{
    return [NSString stringWithUTF8String:YMInterfaceTypeDescription(self.remoteInterfaceType)];
}

- (NSNumber *)sample
{
    return @( YMConnectionGetSample(self.connectionRef) );
}

- (YMStream *)newStreamWithName:(NSString *)name
{
    YMStringRef ymstr = YMStringCreateWithCString([name UTF8String]);
    YMStreamRef ymstream = YMConnectionCreateStream(self.connectionRef, ymstr, YMCompressionNone);
    YMRelease(ymstr);
    
    YMStream *stream = [[YMStream alloc] _initWithStreamRef:ymstream];
    return stream;
}

- (void)closeStream:(YMStream *)stream
{
    YMConnectionCloseStream(self.connectionRef, [stream _streamRef]);
}

- (BOOL)forwardFile:(int)file toStream:(YMStream *)stream length:(size_t *)inOutLen
{
    return YMConnectionForwardFile(self.connectionRef, file, [stream _streamRef], inOutLen, true, NULL);
}

- (BOOL)forwardStream:(YMStream *)stream toFile:(int)file length:(size_t *)inOutLen
{
    return YMConnectionForwardStream(self.connectionRef, [stream _streamRef], file, inOutLen, true, NULL);
}

@end
