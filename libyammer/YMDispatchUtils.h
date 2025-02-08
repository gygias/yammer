#ifndef YMDispatchUtils_h
#define YMDispatchUtils_h

#include "YMBase.h"
#include "YMDispatch.h"
#include "YMStream.h"

YM_EXTERN_C_PUSH

typedef void (*ym_forward_file_callback)(void *, YMIOResult, uint64_t);
typedef struct ym_forward_file
{
    ym_forward_file_callback callback;
    void * context;
} ym_forward_file;
typedef struct ym_forward_file ym_forward_file_t;

// the 'forward file' problem
// user should be able to asynchronously forward a file, release ownership of stream and forget it
// plexer cannot send remote-close at the time user forwards and releases
// want to be able to leave stream open, forward file doesn't imply close after completion
// need event based way to either notify user when forward is complete, or 'close when done' in sync with plexer's list of streams
// should the client 'forward' api be on connection, so it can ConnectionCloseStream either on callback or after sync method?
bool YMAPI YMDispatchForwardFile(YMDispatchQueueRef queue, YMFILE fromFile, YMStreamRef toStream, const uint64_t *nBytesPtr, bool sync, ym_forward_file_t *callbackInfo);
bool YMAPI YMDispatchForwardStream(YMDispatchQueueRef queue, YMStreamRef fromStream, YMFILE toFile, const uint64_t *nBytesPtr, bool sync, ym_forward_file_t *callbackInfo);
void YMAPI YMDispatchJoin(YMDispatchQueueRef queue);

YM_EXTERN_C_POP

#endif /* YMDispatchUtils_h */
