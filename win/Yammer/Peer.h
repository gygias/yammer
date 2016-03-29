#pragma once

using namespace System;

namespace Yammer {

	public ref class Peer
	{
	private:
		YMPeerRef peerRef;

	public:
		String ^Name();
		array<unsigned char> ^PublicKeyData();

		!Peer();
		~Peer();
	internal:
		Peer(YMPeerRef peerRef);
		bool _IsEqualToRef(YMPeerRef peerRef);

	};
}