# TODO

## 2.0

### IChannelServer

- New tests.
  - Concurrent `SendString` handling.
  - `WaitForServer` without server started.
  - Client or server stops any time.
- Review lock usage.
- Windows only simple HTTP server APIs, and refactor `HttpServer` `INetworkProtocolServer` implementation.
  - Optional `HttpVerbOPTIONS` switch.
- Windows socket
- Linux socket
- stdio?
- Refactoring
  - Initialize underlying resource and check if the callback is installed in `WaitForServer`.
  - Make sure `Stop` blocks until all clients are stopped and resources are released therefore no more messages can be received after `Stop` exits.
  - `NetworkProtocolLocalChannelClient`
    - `WaitForServer` should just do nothing, everything should be done in `IChannelServer::ConnectLocalClient`.
    - Common base class with `NetworkProtocolChannelClient` should be extracted as the current base class `NetworkProtocolChannelClient` has members that do not work with `NetworkProtocolLocalChannelClient`.

## Optional
