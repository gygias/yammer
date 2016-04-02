# yammer

Yammer is a C library simplifying concurrent, multi-stream connections over the local network, with configurable encryption and per-stream compression. It also profiles available routes between hosts with multiple network interfaces, allowing the user to adopt a faster connection method should one become available after the initial connection is made, and seamlessly reconnects if a disconnection occurs.

The library aims to be fully platform-agnostic, supporting Windows, OS X, iOS, Linux (and ostensibly Android, though I don't have access to one). VS2015 solution, Xcode project, makefile, Cocoa & .NET (work in progress) wrapper frameworks and fairly comprehensive test suite are included. It depends on OpenSSL and mDNSResponder, which are included on Apple and available for Linux & Windows platforms. 32-bit OpenSSL dlls for Windows, pre-built by me using Microsoft VC are included in the project source.

The main interface is YMSession. Users specify an mDNS service type (e.g. _myappproto._tcp). Servers then 'advertise' a service with a given name (e.g. MyApp on David's iPhone). Clients can easily enumerate and connect to service instances by name, configuring security for the connection, and create arbitrarily many bi-directional, full-duplex streams over the connection.

_(this is more or less a zeroconf dns wrapped, app-layer library inspired by Stream Control Transmission Protocol (SCTP), which is for many applications a better transport protocol than TCP, but never seemed to get enough inertia to have [exposed?] implementations on major platforms. 3rd party, in-kernel SCTP are available on the internet._

_it includes a mini CF-like class library - masochistic desire to write tons of OS-portable C code and class-posing boilerplate withstanding :)_
