//
//  Tests.c
//  yammer
//
//  Created by david on 12/2/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "Tests.h"

#ifdef WIN32
#include "arc4random.h"
#endif

char *YMRandomASCIIStringWithMaxLength(uint16_t maxLength, bool for_mDNSServiceName, bool for_txtKey)
{
    uint8_t randomLength = (uint8_t)arc4random_uniform(maxLength + 1 + 1);
    if ( randomLength == 0 ) randomLength = 1 + 1;
    char *string = malloc(randomLength);
    uint8_t maxChar = for_mDNSServiceName ? 'z' : 0x7E, minChar = for_mDNSServiceName ? 'a' : 0x20;
    uint8_t range = maxChar - minChar;
    
    string[--randomLength] = '\0';
    while ( randomLength-- )
    {
        char aChar;
        do {
            aChar = (char)arc4random_uniform(range + 1) + minChar;
        } while ( for_txtKey && (aChar == '='));
        
        string[randomLength] = aChar;
    }
    
    return string;
}

uint8_t *YMRandomDataWithMaxLength(uint16_t length, uint16_t *outLength)
{
    uint16_t randomLength = (uint16_t)arc4random_uniform(length+1);
    if ( randomLength == 0 ) randomLength = 1;
    uint8_t *randomData = malloc(randomLength);
    
    uint16_t countdown = randomLength;
    while ( countdown-- )
    {
        uint8_t aByte = (uint8_t)arc4random_uniform(0x100);
        randomData[countdown] = aByte;
    }
    
    if ( outLength )
        *outLength = randomLength;
    
    return randomData;
}
