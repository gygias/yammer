#include "stdafx.h"

#include "Peer.h"

namespace Yammer {
		
		Peer::Peer(YMPeerRef peerRef)
		{
			this->peerRef = peerRef;
		}

		bool Peer::_IsEqualToRef(YMPeerRef peerRef)
		{
			return ( peerRef == this->peerRef );
		}

		String ^ Peer::Name()
		{
			const char *cName = YMSTR(YMPeerGetName(this->peerRef));
			return gcnew String(cName);
		}

		array<unsigned char> ^ Peer::PublicKeyData()
		{
			return nullptr;
		}

		Peer::!Peer()
		{
			if ( this->peerRef )
				YMRelease(this->peerRef);
		}

		Peer::~Peer()
		{
			this->!Peer();
		}
}