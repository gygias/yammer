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
    NSMutableData *data = [NSMutableData data],
        *outData = nil;
    
    uint16_t bufLen = 16384;
    uint8_t *buf = malloc(bufLen);
    while ( idx < length ) {
        uint16_t outLen = 0;
        NSUInteger remaining = ( length - idx );
        uint16_t aReadLen = ( remaining < bufLen ) ? (uint16_t)remaining : bufLen;
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
            goto catch_return;
        }
        
        idx += outLen;
    }
    
    outData = data;
    
catch_return:
    YMFREE(buf);
    return outData;
}

- (BOOL)writeData:(NSData *)data
{
    NSUInteger idx = 0;
    while ( idx < [data length] ) {
        NSUInteger remaining = [data length] - idx;
        uint16_t aLength = remaining < UINT16_MAX ? (uint16_t)remaining : UINT16_MAX;
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
