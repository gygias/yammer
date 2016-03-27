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
@property (readonly) YMInterfaceType localInterfaceType;
@property (readonly) NSString * localInterfaceDescription;
@property (readonly) YMInterfaceType remoteInterfaceType;
@property (readonly) NSString * remoteInterfaceDescription;

- (YMStream *)newStreamWithName:(NSString *)name;
- (void)closeStream:(YMStream *)stream;

@end
