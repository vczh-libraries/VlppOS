# TODO

## 2.0

- `InterProcessServerTest` project.
- `InterProcessorClientTest` project.
- `INetworkProtocol` implementations currently accept one client, need improvement.
  - Need to send `clientId` (empty to broadcast), `channelName`, `str`.
  - Only `*Client` inherits from `INetworkProtocol`.
  - `NamedPipeServer`.
  - `HttpServer`.

## Optional
