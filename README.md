# yammer

Yammer is a C library simplifying creation of secure, multi-stream connections over the local network. It also profiles available routes between hosts with multiple network interfaces, allowing the user to adopt a faster connection method should one become available after the initial connection is made.

The library aims to be fully cross-platform, supporting Windows, OS X, iOS, Linux (and ostensibly Android). Visual Studio solution, Xcode project, and makefile are provided. It depends on OpenSSL and mDNSResponder, which are included on Apple and available for Linux & Windows platforms. 32-bit OpenSSL libraries for Windows, pre-built with Microsoft VC are included in the project source.

The main interface is YMSession. Users specify an mDNS service type (e.g. _myappproto._tcp). Servers then 'advertise' a service with a given name (e.g. MyApp on David's iPhone). Clients can easily enumerate and connect to service instances by name, configuring security for the connection, and create arbitrarily many bi-directional, full-duplex streams over the connection.

_this is basically an application-level implementation of Stream Control Transmission Protocol (SCTP), which is for many applications a better transport protocol than TCP, but never seemed to get enough inertia to have implementations (or exposed implementations) on major platforms._
