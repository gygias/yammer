//
//  YammerTests.m
//  YammerTests
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#import "YammerTests.h"

NSString *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, BOOL for_mDNSServiceName)
{
    NSMutableString *string = [NSMutableString string];
    
    uint8_t randomLength = (uint8_t)arc4random_uniform(maxLength + 1);
    if ( randomLength == 0 ) randomLength = 1;
    uint8_t maxChar = for_mDNSServiceName ? 'z' : 0x7E, minChar = for_mDNSServiceName ? 'a' : 0x20;
    uint8_t range = maxChar - minChar;
    
    while ( randomLength-- )
    {
        char aChar;
        while ( ( aChar = (uint8_t)(arc4random_uniform(range + 1) + minChar) ) == '=' );
        [string appendFormat:@"%c",aChar];
    }
    
    return string;
}

NSData *YMRandomDataWithMaxLength(uint16_t length)
{
    NSMutableData *data = [NSMutableData data];
    
    uint16_t randomLength = (uint16_t)arc4random_uniform(length+1);
    if ( randomLength == 0 ) randomLength = 1;
    
    while ( randomLength-- )
    {
        uint8_t aByte = (uint8_t)arc4random_uniform(0x100);
        [data appendBytes:&aByte length:sizeof(aByte)];
    }
    
    return data;
}
