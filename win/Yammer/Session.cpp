#include "stdafx.h"

#include "Session.h"

using namespace System;

namespace Yammer {

	Session::Session(String ^type, String ^name)
	{
		this->type = type;
		this->sName = name;
	}

	// depends on ShouldAccept, NewConnection events
	bool Session::StartAdvertising(String ^name)
	{
		return false;
	}

	// depends on PeerDiscovered, PeerDisappeared
	bool Session::BrowsePeers()
	{
		return false;
	}
	// depends on PeerResolve
	bool Session::ResolvePeer(Peer ^peer)
	{
		return false;
	}
	// NewConnection, ConnectFailed
	bool Session::ConnectToPeer(Peer ^peer)
	{
		return false;
	}

	void Session::Stop()
	{
	}
};