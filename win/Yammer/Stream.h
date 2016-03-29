#pragma once

using namespace System;

namespace Yammer {

	public ref class Stream
	{
	private:
		YMStreamRef streamRef;
		
	public:
		array<unsigned char>^ Read(int length);
		bool Write(array<unsigned char>^ data);

		Stream();
		!Stream();
		~Stream();

	internal:
		Stream(YMStreamRef streamRef);
		YMStreamRef _StreamRef();
		bool _IsEqualToRef(YMStreamRef streamRef);
	};
}