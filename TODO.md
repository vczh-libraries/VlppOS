# TODO

## 2.0

### IChannelServer

- New tests.
  - Concurrent `SendString` handling.
  - `WaitForServer` without server started.
  - Client or server stops any time.
- Windows socket
- Linux socket
- stdio?
- Refactoring
  - Initialize underlying resource and check if the callback is installed in `WaitForServer`.
  - Make sure `Stop` blocks until all clients are stopped and resources are released therefore no more messages can be received after `Stop` exits.
  - Windows only simple HTTP server APIs, and refactor `HttpServer` `INetworkProtocolServer` implementation.
    - Optional `HttpVerbOPTIONS` switch.
    - `HttpServerApi.Windows.(h|cpp)`.
  - Windows only simple HTTP client APIs, and refactor `HttpClient` `INetworkProtocolClient` implementation.
    - Delete `HttpUtility.h` and `HttpUtility.Windows.cpp`.
    - The API design should be similar to above files but everything would be async.
    - Keep `UrlEncodeQuery`.
    - `HttpClientApi.Windows.(h|cpp)`.
  - `INetworkProtocolClient::WaitForServer` should only be called when status is `Ready`.
    - `NetworkProtocolLocalChannelClient` will skip this check.
  - `NetworkProtocolLocalChannelClient`
    - `WaitForServer` should just do nothing, everything should be done in `IChannelServer::ConnectLocalClient`.
    - Common base class with `NetworkProtocolChannelClient` should be extracted as the current base class `NetworkProtocolChannelClient` has members that do not work with `NetworkProtocolLocalChannelClient`.
  - `NamedPipeConnection`
    - Remove `lockReadWait` as `readWaitContext` is already atomic.
  - `NamedPipeServer::PendingConnection`
    - Remove `lockConnectWait` as `connectWaitContext` is already atomic.
  - `HttpServerConnection::InstallCallback`
    - Only move `queuedStrings` in `SPIN_LOCK`.

## Optional
