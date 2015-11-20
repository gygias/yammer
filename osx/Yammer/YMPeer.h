//
//  YMPeer.h
//  yammer
//
//  Created by david on 11/19/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface YMPeer : NSObject

- (NSString *)name;

- (NSData *)publicKeyData;

@end
