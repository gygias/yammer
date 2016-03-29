#pragma once

using namespace System;

#include "Session.h"
#include "Connection.h"
#include "Peer.h"
#include "Stream.h"

namespace Yammer {

	public ref class Session
	{
		delegate void PeerDiscoveredEvent(Session ^session, Peer ^peer);
		delegate void PeerDisappearedEvent(Session ^session, Peer ^peer);
		delegate void PeerResolveEvent(Session ^session, Peer ^peer, bool resolved);

		delegate bool ShouldAcceptEvent(Session ^session, Peer ^peer);
		delegate void InitializingEvent(Session ^session);
		delegate void ConnectFailedEvent(Session ^session, Peer ^peer);
		delegate void NewConnectionEvent(Session ^session, Connection ^connection);

		delegate void NewStreamEvent(Session ^session, Connection ^connection, Stream ^stream);
		delegate void StreamClosingEvent(Session ^session, Connection ^connection, Stream ^stream);
		delegate void InterruptedEvent(Session ^session);


	};
}

