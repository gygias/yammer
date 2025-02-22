# defaults to linux
# ARCH=macos hasn't been tested since 10.11.* in 2016-ish, no longer have access to hardware
# DEBUG=1, RPI=1 (working around compatibility issue between clang and valgrind on my rpi default environment)

# gcc-multilib-mips-linux-gnu gcc-mips-linux-gnu qemu-user
# QEMUBUILD=1

OUT=out

LSRC=YMAddress.c YMArray.c YMBase.c YMCompression.c YMConnection.c YMDictionary.c YMLocalSocketPair.c YMLock.c YMLog.c \
				 YMmDNS.c YMmDNSBrowser.c YMmDNSService.c YMNumber.c YMPeer.c YMPipe.c YMPlexer.c YMRSAKeyPair.c \
                 YMSecurityProvider.c YMSemaphore.c YMSession.c YMSocket.c YMStream.c YMString.c YMThread.c YMTLSProvider.c YMUtilities.c \
				 YMX509Certificate.c YMTask.c YMDispatch.c YMDispatchUtils.c
LTGT=libyammer.a

TSRC=CryptoTests.c DictionaryTests.c LocalSocketPairTests.c mDNSTests.c PlexerTests.c SessionTests.c Tests.c TLSTests.c \
	TaskTests.c ThreadTests.c GrabBagTests.c CompressionTests.c
TOBJ=$(TSRC:%.c=%.o)
TDEP=$(TOBJ:%.o=%.o)

GCC=gcc
CLANG=clang
MIPSGCC=mips-linux-gnu-gcc

ifeq ($(ARCH),macos)
	DEFS=-DYMAPPLE
	CC=$(CLANG)
    CCF=-x c -arch x86_64 -Wall -Werror -Wno-unused-label -mmacosx-version-min=10.11 \
			-isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.11.sdk
	STD=gnu99
    IEX=-I/opt/local/include
    LLEX=-arch x86_64 -dynamiclib -install_name /usr/local/lib/libyammer.dylib -single_module -compatibility_version 1 -current_version 1 \
            -L/opt/local/lib -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.11.sdk \
	 -framework CoreFoundation -framework SystemConfiguration -lbz2 -lz
else
	LSRC+= arc4random.c interface.c
	DEFS=-DYMLINUX
	ifeq ($(QEMUBUILD),1)
		CC=$(MIPSGCC)
	else
		CC=$(CLANG)
	endif
	STD=gnu17
	IEX=-Ilinux
	LLEX=-ldns_sd -lbz2 -lz -lm
	PT=-pthread
endif
LOBJ=$(LSRC:%.c=%.o)
LDEP=$(LOBJ:%.o=$(OUT)/%.o)
INC=-I. -Iprivate -Ilibyammer $(IEX)
LLIBS=-lssl -lcrypto $(LLEX)
ifeq ($(DEBUG),1)
	DBGO=-g -Og
	ifeq ($(RPI),1)
# https://bugs.kde.org/show_bug.cgi?id=452758
		DBGO+=-gdwarf-4
	endif
else
	DBGO=-O3
endif
ifeq ($(STACK),1)
	STACKFLAG=-fstack-protector
endif
FLG=-Wall -include private/yammerpch.h -std=$(STD) $(DEFS) $(PT) -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fPIC $(STACKFLAG)
DLIBS=-L. -lyammer


all: $(OUT) $(TGT) ymtest ymchat ym-dispatch-main-test pta

clean:
	rm -r "$(OUT)"

$(OUT):
	mkdir -p "$(OUT)"

$(LTGT): $(LOBJ)
	$(CC) -shared -o "$(OUT)/$@" $(PT) $(ALG) $(LLIBS) $(DBGO) $(LDEP)

interface.o: linux/interface.c
	$(CC) -c $< -o "$(OUT)/$@" $(CCF) $(INC) $(FLG) $(DBGO)

%.o: src/%.c
	$(CC) -c $< -o "$(OUT)/$@" $(CCF) $(INC) $(FLG) $(DBGO)

%.o: test/%.c
	$(CC) -c $< -o "$(OUT)/$@" $(CCF) $(INC) $(FLG) $(DBGO)

ymtest: $(LTGT) $(TOBJ) TestsMain.o
	cd "$(OUT)" ;	$(CC) -o $@ TestsMain.o $(PT) $(DLIBS) $(DBGO) $(TDEP)

TestsMain.o:
	$(CC) -c test/TestsMain.c $(CCF) -o "$(OUT)/$@" $(INC) $(FLG) $(DBGO)

ymchat: $(LTGT) chat.o
	cd "$(OUT)" ; $(CC) -o $@ $(PT) $(DLIBS) $(DBGO) chat.o

chat.o:
	$(CC) -c misc/chat/main.c $(CCF) -o "$(OUT)/$@" $(INC) $(FLG) $(DBGO)

ym-dispatch-main-test: $(LTGT) ym-dispatch-main-test.o
	cd "$(OUT)" ;	$(CC) -o $@ $(PT) ym-dispatch-main-test.o $(DLIBS) $(DBGO) $(TDEP)

ym-dispatch-main-test.o:
	$(CC) -c test/ym-dispatch-main-test.c -o "$(OUT)/$@" $(CCF) $(INC) $(FLG) $(DBGO)

pta: $(LTGT) pta.o
	cd "$(OUT)" ; $(CC) -o $@ $(PT) $(DLIBS) $(DBGO) pta.o

pta.o:
	$(CC) -c misc/pta-cli/main.c $(CCF) -o "$(OUT)/$@" $(INC) $(FLG) $(DBGO)
