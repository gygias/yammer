OUT=out

LSRC=YMAddress.c YMBase.c YMConnection.c YMDictionary.c YMLinkedList.c YMLocalSocketPair.c YMLock.c YMLog.c YMmDNS.c \
				 YMmDNSBrowser.c YMmDNSService.c YMPeer.c YMPipe.c YMPlexer.c YMRSAKeyPair.c YMSecurityProvider.c \
				 YMSemaphore.c YMSession.c YMStream.c YMString.c YMThread.c YMTLSProvider.c YMUtilities.c \
				 YMX509Certificate.c YMTask.c YMArray.c
LOBJ=$(LSRC:%.c=%.o)
LDEP=$(LOBJ:%.o=$(OUT)/%.o)
LTGT=libyammer.a

TSRC=CryptoTests.c DictionaryTests.c LocalSocketPairTests.c mDNSTests.c PlexerTests.c SessionTests.c Tests.c TLSTests.c \
	TaskTests.c ThreadTests.c
TOBJ=$(TSRC:%.c=%.o)
TDEP=$(TOBJ:%.o=%.o)

GCC=gcc
CLANG=clang

ifeq ($(ARCH),macos)
	DEFS=-DYMMACOS
	CC=$(CLANG)
    CCF=-x c -arch x86_64 -Wall -Werror -Wno-unused-label -mmacosx-version-min=10.11\
			-isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.11.sdk
	STD=gnu99
    IEX=-I/opt/local/include
    LLEX=-arch x86_64 -dynamiclib -install_name /usr/local/lib/libyammer.dylib -single_module -compatibility_version 1 -current_version 1\
            -L/opt/local/lib -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.11.sdk
else
	LSRC+= arc4random.c
	DEFS=-DYMLINUX
	CC=$(GCC)
	STD=c99
	LLEX=-ldns_sd
	PT=-pthread
endif
INC=-I. -Iprivate -Ilibyammer $(IEX)
LLIBS=-lssl -lcrypto $(LLEX)
DBG=-ggdb3
FLG=-include private/yammerpch.h -std=$(STD) $(DEFS) $(PT) -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

#ALG=-L/usr/lib/arm-linux-gnueabihf
DLIBS=-L. -lyammer


all: $(OUT) $(TGT) ymtest ymchat ym-dispatch-main-test

clean:
	rm -r "$(OUT)"

$(OUT):
	mkdir -p $(OUT)

$(LTGT): $(LOBJ)
	$(CC) -shared -o $(OUT)/$@ $(PT) $(ALG) $(LLIBS) $(DBG) $(LDEP)

%.o: src/%.c
	$(CC) -c $< -o $(OUT)/$@ $(CCF) $(INC) $(FLG) $(DBG)

%.o: test/%.c
	$(CC) -c $< -o $(OUT)/$@ $(CCF) $(INC) $(FLG) $(DBG)

ymtest: $(LTGT) $(TOBJ) TestsMain.o
	cd $(OUT) ;	$(CC) -o $@ TestsMain.o $(PT) $(DLIBS) $(DBG) $(TDEP)

TestsMain.o:
	$(CC) -c test/TestsMain.c $(CCF) -o $(OUT)/$@ $(INC) $(FLG) $(DBG)

ymchat: $(LTGT) chat.o
	cd $(OUT) ; $(CC) -o $@ $(PT) $(DLIBS) $(DBG) chat.o

chat.o:
	$(CC) -c misc/chat/main.c $(CCF) -o $(OUT)/$@ $(INC) $(FLG) $(DBG)

ym-dispatch-main-test: $(LTGT) ym-dispatch-main-test.o
	cd $(OUT) ;	$(CC) -o $@ $(PT) ym-dispatch-main-test.o $(DLIBS) $(DBG) $(TDEP)

ym-test-dispatch-main.o:
	$(CC) -c test/ym-dispatch-main-test.c -o $(OUT)/$@ $(CCF) $(INC) $(FLG) $(DBG)
