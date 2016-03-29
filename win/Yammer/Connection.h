#pragma once

using namespace System;

#include "Connection.h"

#include "Peer.h"
#include "Stream.h"

namespace Yammer {

	public ref class Connection
	{
	private:
		YMConnectionRef connectionRef;
		YMInterfaceType localIFType, remoteIFType;
		String ^localIFName, ^localIFDescription,
				^remoteIFName, ^remoteIFDescription;
		Int64 sample;

		Connection();
		!Connection();
		~Connection();
	public:
		Stream ^ NewStreamWithName(String ^name);
		void CloseStream(Stream ^stream);
	};
}