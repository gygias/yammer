#include "stdafx.h"

#include "Connection.h"

namespace Yammer {

	Connection::Connection()
	{
		throw gcnew Exception("Connection cannot be constructed directly");
	}

	Connection::!Connection()
	{
		if ( this->connectionRef )
			YMRelease(this->connectionRef);
	}

	Connection::~Connection()
	{
		this->!Connection();
	}

	Stream ^ Connection::NewStreamWithName(String ^name)
	{
		// my fatha's gonna be so prouda me!!
		char *uName = (char *)Marshal::StringToHGlobalAuto(name).ToPointer();
		YMStringRef ymstr = YMStringCreateWithCString(uName);
		Marshal::FreeHGlobal( (IntPtr)uName );

		YMStreamRef streamRef = YMConnectionCreateStream(this->connectionRef, ymstr, YMCompressionNone);
		YMRelease(ymstr);

		Stream ^stream = gcnew Stream(streamRef);
		return stream;
	}
	void Connection::CloseStream(Stream ^stream)
	{
		YMConnectionCloseStream(this->connectionRef, stream->_StreamRef());
	}
};