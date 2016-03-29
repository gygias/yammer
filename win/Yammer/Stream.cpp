#include "stdafx.h"

#include "Stream.h"

#include <malloc.h>

namespace Yammer {

	Stream::Stream(YMStreamRef streamRef)
	{
		this->streamRef = (YMStreamRef)YMRetain(streamRef);
	}

	bool Stream::_IsEqualToRef(YMStreamRef streamRef)
	{
		return ( streamRef == this->streamRef );
	}
	
	array<unsigned char>^ Stream::Read(int length)
	{		
		int idx = 0;
		array<unsigned char> ^data = gcnew array<unsigned char>(length),
			^outData = nullptr;

		uint16_t bufLen = 16384;
		unsigned char *buf = (unsigned char *)malloc(bufLen);
		while ( idx < length ) {
			uint16_t outLen = 0;
			int remaining = ( length - idx );
			uint16_t aReadLen = ( remaining < bufLen ) ? (uint16_t)remaining : bufLen;
			YMIOResult result = YMStreamReadUp(this->streamRef, buf, aReadLen, &outLen);
			if ( result != YMIOError ) {
				Marshal::Copy((IntPtr)buf,data,idx,outLen);
				if ( result == YMIOEOF ) {
					if ( idx == 0 )
						data = nullptr;
					break;
				}
			} else {
				//?log("%s: read %d-%d failed with %d",__PRETTY_FUNCTION__,idx,idx+bufLen,result);
				goto catch_return;
			}

			idx += outLen;
		}

		outData = data;

	catch_return:
		free(buf);
		return outData;
	}

	bool Stream::Write(array<unsigned char>^ data)
	{
		int idx = 0;
		while ( idx < data->Length ) {
			int remaining = data->Length - idx;
			uint16_t aLength = remaining < UINT16_MAX ? (uint16_t)remaining : UINT16_MAX;
			pin_ptr<unsigned char> uData = &data[idx];
			YMIOResult result = YMStreamWriteDown(this->streamRef, uData, aLength);
			if ( result != YMIOSuccess ) {
				//?log("%s: write %u-%u failed with %d",__PRETTY_FUNCTION__,idx,idx + aLength,result);
				return false;
			}

			idx += aLength;
		}

		return true;
	}
	
	Stream::Stream()
	{
		throw gcnew Exception("Stream cannot be constructed directly");
	}

	Stream::!Stream()
	{
		if ( this->streamRef )
			YMRelease(streamRef);
	}

	Stream::~Stream()
	{
		this->!Stream();
	}

	YMStreamRef Stream::_StreamRef()
	{
		return this->streamRef;
	}
}