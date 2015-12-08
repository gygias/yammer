OUT=out

LSRC=YMAddress.c YMBase.c YMConnection.c YMDictionary.c YMLinkedList.c YMLocalSocketPair.c YMLock.c YMLog.c YMmDNS.c \
				 YMmDNSBrowser.c YMmDNSService.c YMPeer.c YMPipe.c YMPlexer.c YMRSAKeyPair.c YMSecurityProvider.c \
				 YMSemaphore.c YMSession.c YMStream.c YMString.c YMThread.c YMTLSProvider.c YMUtilities.c \
				 YMX509Certificate.c arc4random.c
LOBJ=$(LSRC:%.c=%.o)
LDEP=$(LOBJ:%.o=$(OUT)/%.o)
LTGT=libyammer.a

TSRC=CryptoTests.c DictionaryTests.c LocalSocketPairTests.c mDNSTests.c PlexerTests.c SessionTests.c Tests.c TLSTests.c
TOBJ=$(TSRC:%.c=%.o)
TDEP=$(TOBJ:%.o=%.o)

GCC=gcc
CLANG=clang
CC=$(GCC)
INC=-I../ -I../private -I../libyammer
PT=-pthread
DEFS=-DRPI
DBG=-ggdb3
FLG=-include ../private/yammerpch.h -std=c99 $(DEFS) $(PT) -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

#ALG=-L/usr/lib/arm-linux-gnueabihf
LLIBS=-lssl -lcrypto -ldns_sd
DLIBS=-L. -lyammer


all: $(OUT) $(TGT) ymtest ymchat

clean:
	rm -r "$(OUT)"

$(OUT):
	mkdir -p $(OUT)

$(LTGT): $(LOBJ)
	$(CC) -shared -o $(OUT)/$@ $(PT) $(LDEP) $(ALG) $(LLIBS) $(DBG)

%.o: ../src/%.c
	$(CC) -c $< -o $(OUT)/$@ $(INC) $(FLG) $(DBG)

%.o: ../test/%.c
	$(CC) -c $< -o $(OUT)/$@ $(INC) $(FLG) $(DBG)

ymtest: $(LTGT) $(TOBJ)
	cd $(OUT) ;	$(CC) -o $@ $(PT) $(TDEP) $(DLIBS) $(DBG)

ymchat: $(LTGT) chat.o
	cd $(OUT) ; $(CC) -o $@ $(PT) chat.o $(DLIBS) $(DBG)

chat.o:
	$(CC) -c ../misc/chat/main.c -o $(OUT)/$@ $(INC) $(FLG) $(DBG)
