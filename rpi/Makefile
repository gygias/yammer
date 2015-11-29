OUT=out
SRC=YMAddress.c YMBase.c YMConnection.c YMDictionary.c YMLinkedList.c YMLocalSocketPair.c YMLock.c YMLog.c YMmDNS.c YMmDNSBrowser.c YMmDNSService.c YMPeer.c YMPipe.c YMPlexer.c YMRSAKeyPair.c YMSecurityProvider.c YMSemaphore.c YMSession.c YMStream.c YMString.c YMThread.c YMTLSProvider.c YMUtilities.c YMX509Certificate.c arc4random.c
OBJ=$(SRC:%.c=%.o)
DEP=$(OBJ:%.o=$(OUT)/%.o)
CC=gcc
INC=-I../ -I../private -I../libyammer -I../win
PT=-pthread
FLG=-include ../private/yammer.pch -DRPI -std=c99 $(PT)

#ALG=-L/usr/lib/arm-linux-gnueabihf
LIB=-lssl -lcrypto -ldns_sd

TGT = libyammer.o

all: $(OUT) $(TGT)

clean:
	rm -rf $(OUT)

$(OUT):
	mkdir -p $(OUT)

$(TGT): $(OBJ)
	$(CC) -shared -o $(OUT)/$@ $(PT) $(DEP) $(ALG) $(LIB)

%.o: ../src/%.c
	$(CC) -c $< -o $(OUT)/$@ $(INC) $(FLG)