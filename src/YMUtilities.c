//
//  YMUtilities.c
//  yammer
//
//  Created by david on 11/3/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMUtilities.h"

#if defined(YMLINUX)
#define __USE_GNU
#endif
#include <fcntl.h>

#include "YMLock.h"
#include "YMAddress.h"
#include "YMArray.h"
#include "YMTask.h"

#define ymlog_type YMLogDefault // this file isn't very clearly purposed, if something reaches critical mass break it out
#include "YMLog.h"

#if defined(YMAPPLE) || defined(YMLINUX)
# include <netinet/in.h>
# include <sys/ioctl.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <ifaddrs.h>
# include <dirent.h>
# if defined(YMLINUX)
#  include <sys/resource.h>
# include <sys/sysinfo.h>
#  include <signal.h>
#  define __USE_UNIX98
# else // YMAPPLE, YMIsDebuggerAttached stuff
#  include <assert.h>
#  include <stdbool.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <sys/sysctl.h>
# endif
# include <pthread.h>
# if defined (YMAPPLE)
#  include <mach/mach.h>
#  include <sys/sysctl.h>
#  if !defined(YMIOS)
#   include <dlfcn.h>
#  endif
#  include <SystemConfiguration/SystemConfiguration.h>
#  include <sys/time.h>
#  define YM_PORT_MAX IPPORT_HILASTAUTO
# else
#  define YM_PORT_MAX 65535
# endif
#elif defined(YMWIN32)
# define YM_PORT_MAX IPPORT_DYNAMIC_MAX
# include <Winsock2.h>
# include <Ws2tcpip.h>
# include <ws2ipdef.h>
# include <Wlanapi.h>
# include <iphlpapi.h> // GetAdapterInfo etc.
# include <time.h>
# include <Winternl.h> // NtQuery
# include <Processthreadsapi.h> // GetCurrentProcessId
# include <VersionHelpers.h> // IsWindows*
# include "windirent.h"
#endif

YM_EXTERN_C_PUSH

const char *YMGetCurrentTimeString(char *buf, size_t bufLen)
{
    struct timeval epoch = {0,0};
    int result = gettimeofday(&epoch, NULL);
	if ( result != 0 )
		return NULL;
	const time_t secsSinceEpoch = epoch.tv_sec;
    struct tm *now = localtime(&secsSinceEpoch); // um, what? todo
    if ( ! now )
        return NULL;
    result = (int)strftime(buf, bufLen, "%Y-%m-%d %H:%M:%S", now);
    if ( result == 0 )
        return NULL;
    if ( result < (int)bufLen - 4 )
        snprintf(buf + result, bufLen - result, ".%03"PRId64,(int64_t)epoch.tv_usec/1000);
    return buf;
}

void YMGetTheBeginningOfPosixTimeForCurrentPlatform(struct timeval *time)
{
    // todo i'm not sure what 'extension used' or have any idea how this macro works
	YM_WPPUSH
	time->tv_sec = 0;
    time->tv_usec = 0;
	YM_WPOP
}
    
void YMGetTheEndOfPosixTimeForCurrentPlatform(struct timeval *time)
{
	YM_WPPUSH
    
#ifdef YMAPPLE
    time->tv_sec = MAX_OF(typeof(time->tv_sec));
	time->tv_usec = MAX_OF(typeof(time->tv_usec));
#else
    time->tv_sec = LONG_MAX;
    time->tv_usec = LONG_MAX;
#endif
    
	YM_WPOP
}

ComparisonResult YMTimevalCompare(struct timeval a, struct timeval b)
{
    if ( a.tv_sec < b.tv_sec )
        return LessThan;
    else if ( a.tv_sec > b.tv_sec )
        return GreaterThan;
    else {
        if ( a.tv_usec < b.tv_usec )
            return LessThan;
        else if ( a.tv_usec > b.tv_usec )
            return GreaterThan;
    }
    
    return EqualTo;
}

ComparisonResult YMAPI YMTimespecCompare(struct timespec a, struct timespec b)
{
    if ( a.tv_sec < b.tv_sec )
        return LessThan;
    else if ( a.tv_sec > b.tv_sec )
        return GreaterThan;
    else {
        if ( a.tv_nsec < b.tv_nsec )
            return LessThan;
        else if ( a.tv_nsec > b.tv_nsec )
            return GreaterThan;
    }

    return EqualTo;
}

// from GNU
#ifndef TIMEVAL_TO_TIMESPEC
# define TIMEVAL_TO_TIMESPEC(tv, ts) {                                   \
	(ts)->tv_sec = (tv)->tv_sec;                                    \
	(ts)->tv_nsec = (tv)->tv_usec * 1000;                           \
}
#endif
#ifndef TIMESPEC_TO_TIMEVAL
# define TIMESPEC_TO_TIMEVAL(tv, ts) {                                   \
	(tv)->tv_sec = (ts)->tv_sec;                                    \
	(tv)->tv_usec = (ts)->tv_nsec / 1000;
#endif
# define mytimespecsub(a, b, result)						      \
  do {									      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			      \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;			      \
    if ((result)->tv_nsec < 0) {					      \
      --(result)->tv_sec;						      \
      (result)->tv_nsec += 1000000000;					      \
    }									      \
  } while (0)


#ifndef USEC_PER_SEC
#define USEC_PER_SEC 1000000
#endif
#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000
#endif

double YMTimevalSince(struct timeval then, struct timeval now)
{
    double ret;
#if defined(YMLINUX) || defined(YMAPPLE) // may exist on apple too
    struct timeval difference;
    timersub(&then, &now, &difference);
    ret = difference.tv_sec + ( (double)difference.tv_usec / (double)1000000 );
#else
# error implement me
#endif
    return ret;
}

double YMAPI YMTimespecSince(struct timespec then, struct timespec now)
{
    double ret;
    struct timespec difference;
    mytimespecsub(&then,&now,&difference);
    ret = difference.tv_sec + ( (double)difference.tv_nsec / (double)NSEC_PER_SEC );
    return ret;
}

struct timespec YMTimespecNormalize(struct timespec ts)
{
	while(ts.tv_nsec >= (long)NSEC_PER_SEC)
	{
		++(ts.tv_sec);
		ts.tv_nsec -= NSEC_PER_SEC;
	}
	
	while(ts.tv_nsec <= (long)-NSEC_PER_SEC)
	{
		--(ts.tv_sec);
		ts.tv_nsec += NSEC_PER_SEC;
	}
	
	if(ts.tv_nsec < 0)
	{
		/* Negative nanoseconds isn't valid according to POSIX.
		 * Decrement tv_sec and roll tv_nsec over.
		*/
		
		--(ts.tv_sec);
		ts.tv_nsec = (NSEC_PER_SEC + ts.tv_nsec);
	}
	
	return ts;
}

struct timespec YMTimespecFromDouble(double s)
{
	struct timespec ts = {
		.tv_sec  = s,
		.tv_nsec = (s - (long)(s)) * NSEC_PER_SEC,
	};
	
	return YMTimespecNormalize(ts);
}

double YMDoubleFromTimespec(struct timespec ts)
{
	return ((double)(ts.tv_sec) + ((double)(ts.tv_nsec) / NSEC_PER_SEC));
}

YMIOResult YMReadFull(YMFILE fd, uint8_t *buffer, size_t bytes, size_t *outRead)
{
    YM_IO_BOILERPLATE
    
    if ( buffer == NULL || bytes == 0 || fd < 0 ) {
        ymabort("io: invalid params %d %p %zu",fd,buffer,bytes);
        return YMIOError;
    }
    
    size_t off = 0;
    YMIOResult ioResult = YMIOSuccess;
    while ( off < bytes ) {
        YM_READ_FILE(fd, buffer + off, bytes - off);
        if ( result == 0 ) {
            ymdbg("io: read(f%d, %p + %zu, %zu - %zu) EOF",fd, buffer, off, bytes, off);
            ioResult = YMIOEOF;
            break;
        } else if ( result == -1 ) {
            ymerr("io: read(f%d, %p + %zu, %zu - %zu) failed: %d (%s)",fd, buffer, off, bytes, off, error, errorStr);
            ioResult = YMIOError;
            break;
        }
        ymdbg("io: read(f%d, %p + %zu, %zu - %zu): %zd",fd, buffer, off, bytes, off, result);
        off += result;
    }
    
    if ( outRead )
        *outRead = off;
    
    return ioResult;
}

YMIOResult YMWriteFull(YMFILE fd, const uint8_t *buffer, size_t bytes, size_t *outWritten)
{
    YM_IO_BOILERPLATE
    
    if ( buffer == NULL || bytes == 0 || fd < 0 ) {
        ymabort("io: invalid params %d %p %zu",fd,buffer,bytes);
        return YMIOError;
    }
    
    size_t off = 0;
    YMIOResult ioResult = YMIOSuccess;
    while ( off < bytes ) {
        YM_WRITE_FILE(fd, buffer + off, bytes - off);
        switch(result) {
            case 0:
                ymabort("io: write(f%d, %p + %zu, %zu - %zu) failed 0?: %d (%s)",fd, buffer, off, bytes, off, error, errorStr);
                //goto catch_fail;
            case -1:
                ymabort("io: write(f%d, %p + %zu, %zu - %zu) failed: %d (%s)",fd, buffer, off, bytes, off, error, errorStr);
                ioResult = YMIOError;
                goto catch_fail;
            default:
                ymdbg("io: write(f%d, %p + %zu, %zu - %zu): %ld",fd, buffer, off, bytes, off, result);
                break;
        }
        off += result;
    }
    
catch_fail:
    if ( outWritten )
        *outWritten = off;
    return ioResult;
}
    
#if defined(YMWIN32)
YM_ONCE_FUNC(__YMNetworkingInit,
{
	WSADATA wsa;
	int result = WSAStartup(MAKEWORD(2,2),&wsa);
    if ( result != 0 ) {
		ymerr("fatal: WSAStartup failed: %x %x",result,GetLastError());
		exit(1);
	}
})
#endif

void YMNetworkingInit(void)
{
#if defined(YMWIN32)
	YM_ONCE_DO_LOCAL(__YMNetworkingInit);
#endif
}

int32_t YMPortReserve(bool ipv4, int *outSocket)
{
    YM_IO_BOILERPLATE

    bool okay = false;
    uint16_t aPort = IPPORT_RESERVED;
    uint16_t thePort = aPort;
    YMSOCKET aSocket = NULL_SOCKET;
    
    uint8_t length = ipv4 ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    struct sockaddr *addr = YMALLOC(length);
    addr->sa_family = ipv4 ? AF_INET : AF_INET6;
#if defined(YMAPPLE)
    addr->sa_len = length;
#endif
    if ( ipv4 )
        ((struct sockaddr_in *)addr)->sin_addr.s_addr = INADDR_ANY;
    else
        ((struct sockaddr_in6 *)addr)->sin6_addr = in6addr_any;
    
    while (aPort < YM_PORT_MAX) {
        thePort = aPort++;
        
        int domain = ipv4 ? PF_INET : PF_INET6;
        YMSOCKET aResult = socket(domain, SOCK_STREAM, IPPROTO_TCP);
#if !defined(YMWIN32)
        if ( aResult < 0 )
#else
		if ( aResult == INVALID_SOCKET )
#endif
            goto catch_continue;
        
        aSocket = aResult;
        
        int yes = 1;
        aResult = setsockopt(aSocket, SOL_SOCKET, SO_REUSEADDR, (const void *)&yes, sizeof(yes));
        if ( aResult != 0 )
            goto catch_continue;
        
        if ( ipv4 )
            ((struct sockaddr_in *)addr)->sin_port = htons(thePort);
        
        else
            ((struct sockaddr_in6 *)addr)->sin6_port = htons(thePort);
        
        aResult = bind(aSocket, addr, length);
        if ( aResult != 0 )
            goto catch_continue;
        
        *outSocket = aSocket;
        okay = true;
        break;
        
    catch_continue:
        if ( aSocket > 0 ) {
            YM_CLOSE_SOCKET(aSocket);
		}
    }
    
    YMFREE(addr);
    return okay ? (uint32_t)thePort : -1;
}

#warning this doesn't seem to count semaphores, or match lsof -p | wc -l, on debian, fix
int YMGetNumberOfOpenFilesForCurrentProcess(void)
{
    int nFiles = 0;
#if !defined(YMWIN32)
    struct rlimit r_limit;
    int result = getrlimit(RLIMIT_NOFILE, &r_limit);
    ymsoftassert(result==0, "getrlimit: %d %s",errno,strerror(errno));
    
    for( rlim_t i = 0; i < r_limit.rlim_cur; i++ ) {
        errno = 0;
        result = fcntl((int)i, F_GETFD);
        if ( result == 0 )
            nFiles++;
    }
#else // maybe there's a hidden "getrlimit" for win32? couldn't find it
	// cribbed from http://www.codeproject.com/Articles/18975/Listing-Used-Files
    // Get the list of all handles in the system
    typedef struct _SYSTEM_HANDLE {
		DWORD       dwProcessId;
		BYTE		bObjectType;
		BYTE		bFlags;
		WORD		wValue;
		PVOID       pAddress;
		DWORD GrantedAccess;
	} SYSTEM_HANDLE;

    typedef struct _SYSTEM_HANDLE_INFORMATION {
		DWORD         dwCount;
		SYSTEM_HANDLE Handles[1];
	} SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION, **PPSYSTEM_HANDLE_INFORMATION;

	typedef enum _SYSTEM_INFORMATION_CLASS {
		SystemHandleInformation = 0X10,
	} SYSTEM_INFORMATION_CLASS;

#define VISTA_FILETYPE  25
#define XP_FILETYPE 28
	int nFileType = IsWindowsVistaOrGreater() ? VISTA_FILETYPE : XP_FILETYPE;

	SYSTEM_HANDLE_INFORMATION *pSysHandleInformation = NULL;
	DWORD sysHandleInformationSize = 0;
    NTSTATUS status = NtQuerySystemInformation( SystemHandleInformation,
                                               NULL, 0, &sysHandleInformationSize);
	if ( NT_SUCCESS(status) || sysHandleInformationSize == 0 )
		return -1;

	sysHandleInformationSize = sysHandleInformationSize + 1024;
	pSysHandleInformation = malloc(sysHandleInformationSize);
    status = NtQuerySystemInformation( SystemHandleInformation,
                                        pSysHandleInformation, sysHandleInformationSize, &sysHandleInformationSize);
    if( !NT_SUCCESS(status) ) {
		YMFREE(pSysHandleInformation);
		return -1;
	}
    
	DWORD currentProcessID = GetCurrentProcessId();
	for ( DWORD i = 0; i < pSysHandleInformation->dwCount; i++ ) {
		SYSTEM_HANDLE sh = pSysHandleInformation->Handles[i];
		if ( sh.dwProcessId != currentProcessID )
			continue;
		if( sh.bObjectType != nFileType )// Under windows XP file handle is of type 28
			continue;
		ymerr("open file: %d",sh.wValue);
		nFiles++;
	}

	YMFREE(pSysHandleInformation);
#endif
    
    return nFiles;
}

int YMGetPipeSize(__unused YMFILE file)
{
#if defined(YMLINUX)
    return fcntl(file, F_GETPIPE_SZ); // this is a gnu libc thing, just bookmarking this
#elif defined(YMAPPLE)
    return UINT16_MAX; // xxx
#else
#error implement me
#endif
}

YMDictionaryRef YMInterfaceMapCreateLocal(void)
{
    YMDictionaryRef map = YMDictionaryCreate2(true,true);
    
#if defined(YMAPPLE) || defined(YMLINUX)
    struct ifaddrs *ifaddrsList = NULL, *ifaddrsIter;
    if ( getifaddrs(&ifaddrsList) != 0 ) {
        ymerr("getifaddrs failed: %d %s",errno,strerror(errno));
        goto catch_return;
    }
    ifaddrsIter = ifaddrsList;
    
    while ( ifaddrsIter ) {
        // IFF_UP // defined in ifconfig.c, anywhere else?
        //ioctl(<#int#>, <#unsigned long, ...#>);
        if ( ifaddrsIter->ifa_name && ifaddrsIter->ifa_addr ) {
            YMAddressRef address = YMAddressCreate(ifaddrsIter->ifa_addr, 0);
            if ( address ) {
                YMStringRef name = YMSTRC(ifaddrsIter->ifa_name);
                YMDictionaryRef ifInfo = YMDictionaryGetItem(map, (YMDictionaryKey)name);
                if ( ! ifInfo ) {
                    ifInfo = YMDictionaryCreate2(false,true);
                    YMDictionaryAdd(map, (YMDictionaryKey)name, (void *)ifInfo);
                    YMNumberRef ymType = YMNumberCreateWithInt32(YMInterfaceTypeForName(name));
                    YMDictionaryAdd(ifInfo, kYMIFMapTypeKey, ymType);
                    YMRelease(ymType);
                    YMRelease(ifInfo);
                }
                YMArrayRef addresses = YMDictionaryGetItem(ifInfo, kYMIFMapAddressesKey);
                if ( ! addresses ) {
                    addresses = YMArrayCreate2(true);
                    YMDictionaryAdd(ifInfo, kYMIFMapAddressesKey, (void *)addresses);
                    YMRelease(addresses);
                }
                YMArrayAdd(addresses, address);
                YMRelease(name);
                YMRelease(address);
            }
        }
        ifaddrsIter = ifaddrsIter->ifa_next;
    }
    freeifaddrs(ifaddrsList);
    
    // todo fix [un]managed key/value mixing, currently leaks, should be opaque
#elif defined(YMWIN32)
	//GetInterfaceInfo(NULL, &ifInfoLen);

	ULONG apInfoLen = 0;
	PIP_ADAPTER_INFO apInfo, apInfoIter;

	DWORD result = GetAdaptersInfo(NULL, &apInfoLen);
	if ( result != ERROR_BUFFER_OVERFLOW )
		goto catch_return;

	apInfo = YMALLOC(apInfoLen);
	result = GetAdaptersInfo(apInfo, &apInfoLen);
	if ( result != ERROR_SUCCESS ) // success is the error of winners!
		goto catch_return;
	else if ( apInfo ) {
		apInfoIter = apInfo;
		while ( apInfoIter ) {
			IP_ADDR_STRING anIpString = apInfoIter->IpAddressList;
			YMStringRef ymIp = YMSTRC(anIpString.IpAddress.String);

			if ( ( apInfoIter->Type == MIB_IF_TYPE_ETHERNET ) 
				|| ( apInfoIter->Type == IF_TYPE_IEEE80211 ) ) {
				
				YMStringRef name = YMSTRC(apInfoIter->AdapterName);
                YMDictionaryRef ifInfo = YMDictionaryGetItem(map, (YMDictionaryKey)name);
                if ( ! ifInfo ) {
                    ifInfo = YMDictionaryCreate();
                    YMDictionaryAdd(map, (YMDictionaryKey)name, (void *)ifInfo);
                    YMNumberRef ymType = YMNumberCreateWithInt32(YMInterfaceTypeForName(name));
                    YMDictionaryAdd(ifInfo, kYMIFMapTypeKey, ymType);
                    YMRelease(ymType);
                    YMRelease(ifInfo);
                }
				YMAddressRef address = YMAddressCreateWithIPStringAndPort(ymIp, 0);

				if ( address ) {
					YMArrayRef addresses = YMDictionaryGetItem(ifInfo, kYMIFMapAddressesKey);
					if ( ! addresses ) {
						addresses = YMArrayCreate2(true);
						YMDictionaryAdd(ifInfo, kYMIFMapAddressesKey, (void *)addresses);
					}
					YMArrayAdd(addresses, address);
                    YMRelease(address);
				} else
					ymlog("warning: failed to parse if %s address %s",YMSTR(name),YMSTR(ymIp));

				YMRelease(name);
			} else
				ymlog("unknown windows network interface: %u: %s : %s",apInfoIter->Type,anIpString.IpAddress.String,apInfoIter->AdapterName);
			
			YMRelease(ymIp);
			apInfoIter = apInfoIter->Next;
		}
	}

	YMFREE(apInfo);
#else
#error if mapping unimplemented for this platform
#endif
    
    ymlog("current interface map (%p):",map);
    YMDictionaryEnumRef denum = YMDictionaryEnumeratorBegin(map);
    while ( denum ) {
        YMStringRef ifName = (YMStringRef)denum->key;
        YMDictionaryRef ifInfo = denum->value;
        YMInterfaceType thisType = YMNumberInt32Value((YMNumberRef)YMDictionaryGetItem(ifInfo, kYMIFMapTypeKey));

        #define ymif_aline 1024
        uint16_t max = ymif_aline, off = 0;
        char line[ymif_aline];
        off += snprintf(line+off,max-off," %s (%s):",YMSTR(ifName),YMInterfaceTypeDescription(thisType));
        YMArrayRef addresses = YMDictionaryGetItem(ifInfo, kYMIFMapAddressesKey);
        if ( addresses ) {
            for ( int i = 0; i < YMArrayGetCount(addresses); i++ ) {
                off += snprintf(line+off,max-off," %s",YMSTR(YMAddressGetDescription((YMAddressRef)YMArrayGet(addresses, i))));
            }
        }
        ymlog("%s",line);
        denum = YMDictionaryEnumeratorGetNext(denum);
    }
    YMDictionaryEnumeratorEnd(denum);
    
catch_return:
    return map;
}

YMInterfaceType YMInterfaceTypeForName(YMStringRef ifName)
{
	YMInterfaceType defaultType = YMInterfaceUnknown;

#if defined(YMAPPLE)
    
    if ( YMStringHasPrefix2(ifName, "lo") ) {
        return YMInterfaceLoopback;
    } else if ( YMStringHasPrefix2(ifName, "pdp_ip") ) {
        return YMInterfaceCellular;
    } else if ( YMStringHasPrefix2(ifName, "en") ) {
# if !defined(YMIOS)
#  define YM_SCNETWORKINTERFACE 1
#  if defined(YM_SCNETWORKINTERFACE)
        bool matched = false;
        CFStringRef cfIfName = CFStringCreateWithCString(NULL, YMSTR(ifName), kCFStringEncodingASCII);
        if ( cfIfName ) {
            CFArrayRef scInterfaces = SCNetworkInterfaceCopyAll();
            if ( scInterfaces ) {
                for ( CFIndex idx = 0; idx < CFArrayGetCount(scInterfaces); idx++ ) {
                    SCNetworkInterfaceRef scInterface = CFArrayGetValueAtIndex(scInterfaces, idx);
                    if ( scInterface ) {
                        CFStringRef scName = SCNetworkInterfaceGetBSDName(scInterface);
                        if ( scName && CFEqual(scName, cfIfName) ) {
                            CFStringRef scType = SCNetworkInterfaceGetInterfaceType(scInterface);
                            if ( scType && CFEqual(scType, kSCNetworkInterfaceTypeIEEE80211) ) {
                                matched = true;
                                break;
                            }
                        }
                    }
                }
                CFRelease(scInterfaces);
            }
            CFRelease(cfIfName);
        }
        
        if ( matched )
            return YMInterfaceWirelessEthernet;
#  elif defined(YM_DLAPPLE80211)
#define Apple80211FrameworkBinaryPath "/System/Library/PrivateFrameworks/Apple80211.framework/Apple80211"
        void *dl = dlopen(Apple80211FrameworkBinaryPath,RTLD_LAZY);
        void (*functionstub)();
        functionstub = dlsym(dlhandle, "Apple80211Open"); // Apple80211GetIfListCopy ...
#  else
        // the only proper interface i know of for this is obj-c and CoreWLAN/CWInterface.h#interfaceName
        // so we'd either need an objc helper library for apple platforms, or maybe this is good enough
        YMStringRef path = YMSTRC("/usr/sbin/networksetup");
        YMArrayRef args = YMArrayCreate();
        YMArrayAdd(args, "-getairportnetwork");
        YMArrayAdd(args, YMSTR(ifName));
        YMArrayAdd(args, "2&>");
        YMArrayAdd(args, "/dev/null");
        
        YMTaskRef task = YMTaskCreate(path, args, true);
        YMTaskLaunch(task);
        YMTaskWait(task);
        int status = YMTaskGetExitStatus(task);
        
        YMRelease(path);
        YMRelease(args);
        YMRelease(task);
        
        if ( status == 0 )
            return YMInterfaceWirelessEthernet;
#endif
        return YMInterfaceWiredEthernet;
# else
        return YMInterfaceWirelessEthernet;
# endif
    } else if ( YMStringHasPrefix2(ifName, "awdl") ) {
        return YMInterfaceAWDL;
    } else if ( YMStringHasPrefix2(ifName, "ipsec") ) {
        return YMInterfaceIPSEC;
    } else if ( YMStringHasPrefix2(ifName, "fw") ) {
        return YMInterfaceFirewire400;
    } else
        goto catch_return;
    
#elif defined(YMWIN32)
	// todo should be optimized, known in if mapping function

	// fussing with 10 different string types and 5 different "string <-> guid" functions
	// took substantially longer than the rest of the if matching stuff, neat!
	// strip {..}
	GUID guid = {0};
	char *nakedGuid = strdup(YMSTR(ifName));
	nakedGuid[strlen(nakedGuid)-1] = '\0';
	RPC_STATUS result2 = UuidFromStringA(nakedGuid + 1,&guid);
	YMFREE(nakedGuid);
	if ( result2 != RPC_S_OK ) {
		ymerr("CLSIDFromString failed: %08x", GetLastError());
		goto catch_return;
	}

# define ymWlanXPSP2_3		1
# define ymWlanVistaAndUp	2
	DWORD wlanVersion = 0;
	HANDLE wlanHandle = NULL;

	DWORD result = WlanOpenHandle(WFD_API_VERSION_1_0, NULL, &wlanVersion, &wlanHandle);
	if ( result != ERROR_SUCCESS ) {
		ymerr("WlanOpenHandle failed: %u: %08x", result, GetLastError());
		goto catch_return;
	}

	WLAN_CONNECTION_ATTRIBUTES attrs;
	DWORD attrsLen = sizeof(attrs);
	WLAN_OPCODE_VALUE_TYPE opcode;
	result = WlanQueryInterface(wlanHandle, (const GUID *)&guid, wlan_intf_opcode_current_connection, NULL, &attrsLen, (PVOID)&attrs, &opcode);
	if ( result != ERROR_SUCCESS ) {
		//ymerr("WlanQueryInterface failed: %u: %08x", result, GetLastError());
		defaultType = YMInterfaceWiredEthernet; // todo excluding others for now
		goto catch_close;
	} else
		defaultType = YMInterfaceWirelessEthernet;

catch_close:
	result = WlanCloseHandle(wlanHandle, NULL);
	if ( result != ERROR_SUCCESS )
		ymerr("WlanCloseHandle failed: %u: %08x", result, GetLastError());

#elif defined(YMLINUX)

	if ( YMStringHasPrefix2(ifName, "lo") ) {
	  return YMInterfaceLoopback;
	} else if ( YMStringHasPrefix2(ifName, "eth") || YMStringHasPrefix2(ifName, "en") ) {
	  return YMInterfaceWiredEthernet;                  // wlx is assigned to usb wifi adapters
	} else if ( YMStringHasPrefix2(ifName, "wlan") || YMStringHasPrefix2(ifName, "wlx") ) {
	  return YMInterfaceWirelessEthernet;
	}
    goto catch_return;
#else
#error if-matching not implemented for this configuration
#endif
catch_return:
    return defaultType;
}

const char *YMInterfaceTypeDescription(YMInterfaceType type)
{
    switch ( type ) {
        case YMInterfaceLoopback:
            return "loopback";
            break;
        case YMInterfaceAWDL:
            return "awdl";
            break;
        case YMInterfaceIPSEC:
            return "ipsec";
            break;
        case YMInterfaceCellular:
            return "cellular";
            break;
        case YMInterfaceWirelessEthernet:
            return "wifi";
            break;
        case YMInterfaceBluetooth:
            return "bluetooth";
            break;
        case YMInterfaceWiredEthernet:
            return "ethernet";
            break;
        case YMInterfaceFirewire400:
        case YMInterfaceFirewire800:
        case YMInterfaceFirewire1600:
        case YMInterfaceFirewire3200:
            return "firewire";
            break;
        case YMInterfaceThunderbolt:
            return "thunderbolt";
            break;
		default:
		case YMInterfaceUnknown:
			return "unknown";
			break;
    }
}

bool YMRecursiveDelete(YMStringRef rootPath)
{
    bool okay = true;
    int result;
    DIR *dir = opendir(YMSTR(rootPath));
    struct dirent *dir_ent = NULL;
    if ( dir ) {
        while (1) {
            dir_ent = readdir(dir);
            if ( ! dir_ent )
                break;
            if ( dir_ent->d_type != DT_REG )
                continue;
            
            char *aFile = dir_ent->d_name;
            char fullPath[PATH_MAX];
            strcpy(fullPath, YMSTR(rootPath));
            strcat(fullPath, "/");
            strcat(fullPath, aFile);
            
            result = unlink(fullPath);
            if ( result != 0 ) {
                ymerr("delete %s %d %s",fullPath,errno,strerror(errno));
                okay = false;
            }
        }
        closedir(dir);
    }
    
    result = rmdir(YMSTR(rootPath));
    if ( result != 0 && errno != ENOENT ) {
        ymerr("rmdir failed %d %s",errno,strerror(errno));
        okay = false;
    }
    
    return okay;
}

#if !defined(YMWIN32)
pthread_mutex_t *YMCreateMutexWithOptions(YMLockOptions options)
{
    pthread_mutex_t *mutex = NULL;
    pthread_mutexattr_t attributes;
    pthread_mutexattr_t *attributesPtr = NULL;
    
    int result = pthread_mutexattr_init(&attributes);
    if ( result != 0 ) {
        fprintf(stderr,"pthread_mutexattr_init failed: %d (%s)\n", result, strerror(result));
        goto catch_release;
    }
    
    attributesPtr = &attributes;
    
    if ( options ) {
        int optionsList[4] = { YMLockRecursive, PTHREAD_MUTEX_RECURSIVE, YMLockErrorCheck, PTHREAD_MUTEX_ERRORCHECK };
        for(uint8_t i = 0; i < 4; i+=2 ) {
            if ( options & optionsList[i] ) {
                result = pthread_mutexattr_settype(attributesPtr, optionsList[i+1]);
                if ( result != 0 ) {
                    fprintf(stderr,"pthread_mutexattr_settype failed: %d (%s)\n", result, strerror(result));
                    goto catch_release;
                }
            }
        }
    }
    
    mutex = YMALLOC(sizeof(pthread_mutex_t));
    result = pthread_mutex_init(mutex, attributesPtr);
    if ( result != 0 ) {
        fprintf(stderr,"pthread_mutex_init failed: %d (%s)\n", result, strerror(result));
        YMFREE(mutex);
        mutex = NULL;
    }
    
catch_release:
    if ( attributesPtr )
        pthread_mutexattr_destroy(attributesPtr);
    return mutex;
}

void __YM_MUTEX_CATCH(void)
{
    ymassert(false,"mutex error: %d %s",errno,strerror(errno));
}

bool YMLockMutex(pthread_mutex_t *mutex)
{
    int result = pthread_mutex_lock(mutex);
    bool okay = true;
    switch(result) {
        case 0:
            break;
        case EDEADLK:
            fprintf(stderr,"mutex: error: %p EDEADLK\n", mutex);
            okay = false;
            break;
        case EINVAL:
            fprintf(stderr,"mutex: error: %p EINVAL\n", mutex);
            okay = false;
            break;
        default:
            fprintf(stderr,"mutex: error: %p unknown error\n", mutex);
            break;
    }

    if ( ! okay )
        __YM_MUTEX_CATCH();
    
    return okay;
}

bool YMUnlockMutex(pthread_mutex_t *mutex)
{
    int result = pthread_mutex_unlock(mutex);
    bool okay = true;
    switch(result) {
        case 0:
            break;
        case EPERM:
            fprintf(stderr,"mutex: error: unlocking thread doesn't hold %p\n", mutex);
            okay = false;
            break;
        case EINVAL:
            fprintf(stderr,"mutex: error: unlock EINVAL %p\n", mutex);
            okay = false;
            break;
        default:
            fprintf(stderr,"mutex: error: unknown %p\n", mutex);
            break;
    }
    
    return okay;
}

bool YMDestroyMutex(pthread_mutex_t *mutex)
{
    int result = pthread_mutex_destroy(mutex);
    if ( result != 0 ) fprintf(stderr,"mutex: failed to destroy mutex: %d %s\n",errno,strerror(errno));
    YMFREE(mutex);
    return ( result == 0 );
}

#else // win32

bool YMLockMutex(HANDLE mutex)
{
	return ( WaitForSingleObject(mutex,INFINITE) == WAIT_OBJECT_0 );
}

bool YMUnlockMutex(HANDLE mutex)
{
	return ReleaseMutex(mutex);
}

bool YMDestroyMutex(HANDLE mutex)
{
	return CloseHandle(mutex);
}

HANDLE YMCreateMutexWithOptions(YMLockOptions options)
{
	return CreateMutex(NULL, false, NULL);
}
#endif

#if defined(YMLINUX)
bool gYMDebuggerChecked = false;
bool gYMDebuggerAttached = true;
void __ymutilities_debug_sigtrap(int sig)
{
  gYMDebuggerAttached = false;
  signal(SIGTRAP, SIG_DFL);
}
#endif

bool YMIsDebuggerAttached(void)
{
#if defined(YMAPPLE)
    int                 junk;
    int                 mib[4];
    struct kinfo_proc   info;
    size_t              size;
    
    // Initialize the flags so that, if sysctl fails for some bizarre
    // reason, we get a predictable result.
    
    info.kp_proc.p_flag = 0;
    
    // Initialize mib, which tells sysctl the info we want, in this case
    // we're looking for information about a specific process ID.
    
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();
    
    // Call sysctl.
    
    size = sizeof(info);
    junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
    assert(junk == 0);
    
    // We're being debugged if the P_TRACED flag is set.    
    return ( (info.kp_proc.p_flag & P_TRACED) != 0 );
#elif defined(YMLINUX)
    if ( ! gYMDebuggerChecked ) { // todo review this
      gYMDebuggerChecked = true;
      signal(SIGTRAP, __ymutilities_debug_sigtrap);
      raise(SIGTRAP);
    }
    return gYMDebuggerAttached;
#elif defined(YMWIN32)
	return IsDebuggerPresent();
#else
#warning todo: debugger detection for this platform
    return false;
#endif

    return false;
}

int YMAPI YMGetNumberOfCoresAvailable(void)
{
    int nCores = 0;
#if defined(YMLINUX)
    nCores = get_nprocs();
#elif defined (YMAPPLE)
    // xxx
    size_t len = sizeof(nCores);
    int result = sysctlbyname("hw.ncpu",&nCores,&len,0,0);
    ymassert(result==0,"failed to get number of cores %d %d %s",result,errno,strerror(errno));
#else
#error implement me
#endif
    return nCores;
}

int YMAPI YMGetDefaultThreadsForCores(int cores)
{
    return cores * 1;
}

int YMAPI YMGetNumberOfThreadsInCurrentProcess(void)
{
    int nThreads = 0;
#if defined(YMLINUX)
    pid_t pid = getpid();
    int nBuf = 256;
    char buf[nBuf];
    snprintf(buf,nBuf,"/proc/%d/task",pid);
    DIR *dir = opendir(buf);
    if ( ! dir ) {
        ymlog("opendir(%s) failed: %d %s",buf,errno,strerror(errno));
        return -1;
    }

    struct dirent *aFile;
    while ( ( aFile = readdir(dir) ) )
        nThreads++;

    if ( closedir(dir) != 0 )
        ymlog("closedir(%s) failed: %d %s",buf,errno,strerror(errno));
#elif defined(YMAPPLE)
    mach_port_t me = mach_task_self();
    kern_return_t res;
    thread_array_t threads;
    mach_msg_type_number_t n_threads;

    res = task_threads(me, &threads, &n_threads);
    if (res != KERN_SUCCESS) {
        // Handle error...
    }
    nThreads = n_threads;

    // You now have `n_threads` as well as the `threads` array
    // You can use these to extract info about each thread

    res = vm_deallocate(me, (vm_address_t)threads, n_threads * sizeof(*threads));
    if (res != KERN_SUCCESS) {
        // Handle error...
    }
#else
#error implement me
#endif
    return nThreads;
}

void YMUtilitiesFreeGlobals(void)
{
#if defined(YMWIN32) && defined(YM_FEELING_THOROUGH)
	WSACleanup();
#endif
}

#if defined(YMWIN32) || defined(_YOLO_DONT_TELL_PROFESSOR)
int gettimeofday(struct timeval * tp, struct timezone * tzp)
{
	// Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
	static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

	SYSTEMTIME  system_time;
	FILETIME    file_time;
	uint64_t    time;

	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &file_time);
	time = ((uint64_t)file_time.dwLowDateTime);
	time += ((uint64_t)file_time.dwHighDateTime) << 32;

	tp->tv_sec = (long)((time - EPOCH) / 10000000L);
	tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
	return 0;
}
#endif

void YMRandomDataWithLength(uint8_t *buf, size_t len)
{
    size_t countdown = len;
    while ( countdown-- ) {
        uint8_t aByte = (uint8_t)arc4random_uniform(0x100);
        buf[countdown] = aByte;
    }
}

void YMRandomASCIIStringWithLength(char *buf, size_t len, bool for_mDNSServiceName, bool for_txtKey)
{
    buf[--len] = '\0';

    char maxChar = for_mDNSServiceName ? 'z' : 0x7E, minChar = for_mDNSServiceName ? 'a' : 0x20;
    char range = maxChar - minChar;
    while ( len-- ) {
        char aChar;
        do {
            aChar = (char)arc4random_uniform(range + 1) + minChar;
        } while ( for_txtKey && (aChar == '='));

        buf[len] = aChar;
    }
}

YM_EXTERN_C_POP
