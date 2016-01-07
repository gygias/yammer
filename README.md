# yammer

Yammer is a C library simplifying the process of creating secure multi-stream connections, based on a user-provided service type. It supports connection switching, should a faster connection method come available after an individual connection session has started.

The library is cross-platform, supporting Windows, OS X, iOS, Linux (and ostensibly Android). Visual Studio solution, Xcode project, and makefile are provided. It depends on OpenSSL and mDNSResponder, which are included in Apple & Linux platforms, and available for Windows. Pre-built 32-bit OpenSSL libraries are included in the project source.

The main interface is YMSession. Users specify an mDNS service (e.g. _myappproto._tcp). Session clients can easily enumerate and connect to service instances by name, configuring security for the connection, and create arbitrarily many concurrent streams over the connection.
