//
//  YMIO.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMIO.h"

bool YMRead(int fd, const void *buffer, size_t bytes)
{
    size_t off = 0;
    while ( off < bytes )
    {
        ssize_t aRead = read(fd, (uint8_t *)buffer + off, bytes - off);
        if ( aRead == 0 )
            return false;
        else if ( aRead == -1 )
            return false;
        off += aRead;
    }
    return true;
}

bool YMWrite(int fd, const void *buffer, size_t bytes)
{
    size_t off = 0;
    while ( off < bytes )
    {
        ssize_t aWrite = write(fd, (uint8_t *)buffer + off, bytes - off);
        switch(aWrite)
        {
            case 0:
                printf("YMWrite: aWrite=0?");
            case -1:
                return false;
                break;
            default:
                break;
        }
        off += aWrite;
    }
    return true;
}