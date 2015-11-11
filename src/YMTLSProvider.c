//
//  YMTLSProvider.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMTLSProvider.h"

#include "YMThread.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <pthread.h>

#include "YMLog.h"
#undef ymlog_type
#define ymlog_type YMLogSecurity
#if ( ymlog_type > ymlog_target )
#undef ymlog
#define ymlog(x,...) ;
#endif

typedef struct __YMTLSProvider
{
    YMTypeID _type;
    
    int socketFile;
    bool isWrappingSocket;
} _YMTLSProvider;

static pthread_once_t gYMInitSSLOnce = PTHREAD_ONCE_INIT;
void __YMSSLInit()
{
    SSL_load_error_strings();
    // ``SSL_library_init() always returns "1", so it is safe to discard the return value.''
    SSL_library_init();
    OpenSSL_add_all_algorithms();
}

YMTLSProviderRef __YMTLSProviderCreateWithFullDuplexFile(int file, bool isWrappingSocket);

YMTLSProviderRef YMTLSProviderCreate(int inFile, int outFile)
{
    int sock;
    if ( inFile == outFile )
        sock = inFile;
    else
    {
        // todo? "raw" is only available to superuser, says the man page. protocol boxing not necessary here
        // even if this is currently only for the test case.
        sock = socket(PF_LOCAL, SOCK_STREAM, 0/* IP, /etc/sockets man 5 protocols*/);
        if ( sock == -1 )
        {
            ymerr("tls: failed to create socket for forwarding %d->%d: %d (%s)",inFile,outFile,errno,strerror(errno));
            return NULL;
        }
        
        bool okay = YMThreadDispatchForwardFile(inFile, sock);
        if ( ! okay )
        {
            ymerr("tls: dispatch forward file failed %d->%d",inFile,outFile);
            return NULL;
        }
    }
    
    YMTLSProviderRef tls = __YMTLSProviderCreateWithFullDuplexFile(sock,true);
    if ( ! tls )
        close(sock);
    
    return tls;
}

// designated initializer with shorter arguments list! am i doing it wrong?
YMTLSProviderRef YMTLSProviderCreateWithFullDuplexFile(int file)
{
    return __YMTLSProviderCreateWithFullDuplexFile(file, false);
}

YMTLSProviderRef __YMTLSProviderCreateWithFullDuplexFile(int file, bool isWrappingSocket)
{
    pthread_once(&gYMInitSSLOnce, __YMSSLInit);
    
    struct stat statbuf;
    fstat(file, &statbuf);
    if ( ! S_ISSOCK(statbuf.st_mode) )
    {
        ymerr("tls: error: file %d is not a socket",file);
        return NULL;
    }
    
    YMTLSProviderRef tls = (YMTLSProviderRef)YMALLOC(sizeof(struct __YMTLSProvider));
    tls->_type = _YMTLSProviderTypeID;
    
    tls->socketFile = file;
    tls->isWrappingSocket = isWrappingSocket;
    
    return tls;
}

void _YMTLSProviderFree(YMTypeRef object)
{
    YMTLSProviderRef tls = (YMTLSProviderRef)object;
    if ( tls->isWrappingSocket )
        close(tls->socketFile);
    free(tls);
#pragma message "BIG TODO - we can't fire and forget a forwarding thread, need a proper struct and flags such as a 'finished' callout (and maybe what to do with the output files when done)"
}
