//
//  Tests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "Tests.h"

const char *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, bool for_mDNSServiceName)
{
    uint8_t randomLength = (uint8_t)arc4random_uniform(maxLength + 1);
    if ( randomLength == 0 ) randomLength = 1;
    char *string = malloc(randomLength);
    uint8_t maxChar = for_mDNSServiceName ? 'z' : 0x7E, minChar = for_mDNSServiceName ? 'a' : 0x20;
    uint8_t range = maxChar - minChar;
    
    while ( randomLength-- )
    {
        char aChar;
        do {
            aChar = (char)arc4random_uniform(range + 1) + minChar;
        } while ( for_mDNSServiceName && (aChar == '='));
        
        string[randomLength] = aChar;
    }
    
    return string;
}

const uint8_t *YMRandomDataWithMaxLength(uint16_t length, uint16_t *outLength)
{
    uint16_t randomLength = (uint16_t)arc4random_uniform(length+1);
    if ( randomLength == 0 ) randomLength = 1;
    uint8_t *randomData = malloc(randomLength);
    
    while ( randomLength-- )
    {
        uint8_t aByte = (uint8_t)arc4random_uniform(0x100);
        randomData[randomLength] = aByte;
    }
    
    if ( outLength )
        *outLength = randomLength;
    
    return randomData;
}
