# TODO

## 2.0

- `INetworkProtocol` implementations currently accept one client, need improvement.
  - Server need to assign clientId after connection established.
  - Client need to send `clientId` (empty to broadcast), `channelName`, `str`.
  - `*Client` inherits from `INetworkProtocol`.
  - `*Server` maintains multiple `INetworkProtocol` for clients.
  - Fix `NamedPipeServer`.
  - Fix `HttpServer`.
- InterProcess test project.
  - `InterProcessServerTest`.
  - `InterProcessorClientTest`.
  - Simple chat:
    - Type name to login.
    - Type text to broadcast to `Group` channel.
    - Type `name:text` to send to `Private` channel.
    - Both `Group` and `Private` renders to the CLI application.
    - Support multiple clients, server can chat as admin.
  - `/Http` to use `HttpServer`.
  - `/Pipe` to use `NamedPipe`.
- `HttpServer` to accept customized port and first level url fragment for identifier.

## Optional
