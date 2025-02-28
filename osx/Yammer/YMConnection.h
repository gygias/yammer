//
//  YMConnection.h
//  yammer
//
//  Created by david on 11/18/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <Foundation/Foundation.h>

#import <Yammer/YMStream.h>

@interface YMConnection : NSObject

@property (readonly) NSString * localInterfaceName;
//@property (readonly) YMInterfaceType localInterfaceType;
@property (readonly) NSString * localInterfaceDescription;
//@property (readonly) YMInterfaceType remoteInterfaceType;
@property (readonly) NSString * remoteInterfaceDescription;
@property (readonly) NSNumber *sample; // bytes/sec

- (YMStream *)newStreamWithName:(NSString *)name;
- (void)closeStream:(YMStream *)stream;

- (BOOL)forwardFile:(int)file toStream:(YMStream *)stream length:(size_t *)inOutLen;
- (BOOL)forwardStream:(YMStream *)stream toFile:(int)file length:(size_t *)inOutLen;

@end
