using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using System.Runtime.InteropServices;

namespace Yammer
{
    using YMSessionRef = IntPtr;
    using YMConnectionRef = IntPtr;
    using YMPeerRef = IntPtr;
    using YMStringRef = IntPtr;

    public class Session
    {
        public delegate void PeerDiscovered(Session s, Peer p);
        public delegate void PeerDisappeared(Session s, Peer p);
        public delegate void PeerResolve(Session s, Peer p, bool ok);

        public delegate bool ShouldAccept(Session s, Peer p);
        public delegate void Initializing(Session s);
        public delegate void ConnectFailed(Session s, Peer p);
        public delegate void Connected(Session s, Connection c);

        public delegate void NewStream(Session s, Connection c, Stream st);
        public delegate void StreamClosing(Session s, Connection c, Stream st);
        public delegate void Interrupted(Session s);

        public Session() { throw new Exception("Session default constructor not allowed"); }

        IntPtr sessionRef;
        //String type, name;
        //Peer resolvingPeer, connectingPeer;
        //Connection[] connections;

        [DllImport("libyammer.dll", EntryPoint = "YMSessionCreate")]
        public static extern YMSessionRef YMSessionCreate(YMSessionRef s, YMStringRef n);

        public Session(String type, String name)
        {
            string lol = System.IO.Directory.GetCurrentDirectory();
            sessionRef = YMSessionCreate(new IntPtr(0x101), new IntPtr(0x010));
        }

        public bool StartAdvertising(String name)
        {
            return false;
        }

        public bool StartBrowsing()
        {
            return false;
        }

        public bool ResolvePeer(Peer p)
        {
            return false;
        }

        public bool ConnectToPeer(Peer p)
        {
            return false;
        }

        public void Stop()
        {

        }
    }
}
