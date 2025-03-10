//
//  YMStream.m
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YMStream.h"

#import <libyammer/YMStream.h>

@interface YMStream()

@property (nonatomic) YMStreamRef streamRef;

@end

@implementation YMStream

- (id)_initWithStreamRef:(YMStreamRef)streamRef
{
    if ( ( self = [super init] ) ) {
        self.streamRef = YMRetain(streamRef);
    }
    return self;
}

- (void)dealloc
{
    if ( self.streamRef )
        YMRelease(self.streamRef);
    
    //[super dealloc]; // arc
}

- (BOOL)_isEqualToRef:(YMStreamRef)streamRef
{
    return ( self.streamRef == streamRef );
}

- (YMStreamRef)_streamRef
{
    return self.streamRef;
}

- (NSData *)readDataOfLength:(NSUInteger)length
{
    NSUInteger idx = 0;
    NSMutableData *data = [NSMutableData data];
    
    uint16_t bufLen = 16384;
    uint8_t buf[bufLen];
    while ( idx < length ) {
        size_t outLen = 0;
        NSUInteger remaining = ( length - idx );
        size_t aReadLen = ( remaining < bufLen ) ? remaining : bufLen;
        YMIOResult result = YMStreamReadUp(self.streamRef, buf, aReadLen, &outLen);
        if ( result != YMIOError ) {
            [data appendBytes:buf length:outLen];
            if ( result == YMIOEOF ) {
                if ( [data length] == 0 )
                    data = nil;
                break;
            }
        } else {
            NSLog(@"%s: read %lu-%lu failed with %d",__PRETTY_FUNCTION__,(unsigned long)idx,(unsigned long)(idx+bufLen),result);
            return nil;
        }
        
        idx += outLen;
    }
    
    return data;
}

- (BOOL)writeData:(NSData *)data
{
    NSUInteger idx = 0;
    while ( idx < [data length] ) {
        size_t remaining = [data length] - idx;
        size_t aLength = remaining < UINT16_MAX ? remaining : UINT16_MAX;
        YMIOResult result = YMStreamWriteDown(self.streamRef, (uint8_t *)[data bytes] + idx, aLength);
        if ( result != YMIOSuccess ) {
            NSLog(@"%s: write %lu-%lu failed with %d",__PRETTY_FUNCTION__,(unsigned long)idx,(unsigned long)(idx + aLength),result);
            return NO;
        }
        
        idx += aLength;
    }
    
    return YES;
}

@end
