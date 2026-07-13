# Http Service on TCP Socket

Implements:
- socket api layer
- text network protocol on socket api layer
- http api layer on socket api layer
- text network protocol on http api layer on socket api layer
  - compatible with windows http api implementation

## IAsyncSocketServer / IAsyncSocketClient

- Binary async-only interface implemented in:
  - Windows
  - Linux
  - macOS
  - unit test
- INetworkProtocolServer / INetworkProtocolClient:
  - Text service based on socket.
  - Text block encoded in length in bytes + non-zero-terminated utf-8 string.
  - unit test (shared)

## HTTP server / client based on IAsyncSocketServer / IAsyncSocketClient

- SocketHttpServer / SocketHttpClient
  - On Windows, use socket vs http api in unit test.
  - Test app hosts http service in two different ports
    - JS from one service calls another service
    - Test against Windows(Chrome), Ubuntu(firefox), macOS(safari)
  - Multiple server on one port share the same IAsyncSocketServer.
    - A spin lock protects a global map pointer.
    - each item is refcount protected, released automatically.
    - the whole map is refcount protected, released automatically.
    - If creating socket server fails because of port is occupied:
      - server should take a look at the map again to see if one has been created.
      - if not created retry, in total 5 times.
      - creating socket server should not hold the spin lock.
- INetworkProtocolServer / INetworkProtocolClient based on SocketHttpServer / SocketHttpClient
  - On Windows, use socket vs http api in unit test (shared)
  - On any platform, use socket vs socket in unit test (shared)

## Determine the end of an HTTP request

## Async Socket Implementations

### Windows

IOCP

### Linux

### macOS

Framework.Network

## Http(Server|Client).Windows.(h|cpp) Request Convention
