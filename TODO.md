# TODO

## 2.0

### IAsyncSocketServer / IAsyncSocketClient

- Binary async-only interface implemented in:
  - Windows
  - Linux
  - macOS
- INetworkProtocolServer / INetworkProtocolClient:
  - Text service based on socket.
  - Text block encoded in length in bytes + non-zero-terminated utf-8 string.
- SocketHttpServer / SocketHttpClient
  - On Windows, use socket vs http api in unit test
  - On any platform, use socket vs socket in unit test
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

### IChannelServer

- New tests.
  - Concurrent `SendString` handling.
  - `WaitForServer` without server started.
  - Client or server stops any time.
- Pay attention when `BroadcastFromClient` is happening when the server is accepting new clients.
- stdio?

## Optional
