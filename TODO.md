# TODO

## 2.0

### IChannelServer

- `INetworkProtocol` implementations currently accept one client, need improvement.
  - Fix `HttpServer`.
- `IChannelServer`:
  - Server need to assign clientId after connection established.
  - Client need to send `clientId` (empty to broadcast), `channelName`, `str`.
- InterProcess test project.
  - `InterProcessServerTest`.
  - `InterProcessorClientTest`.
  - Simple chat:
    - Type name to login.
    - Type text to broadcast to `Group` channel.
    - Type `name:text` to send to `Private` channel.
    - Both `Group` and `Private` renders to the CLI application.
    - Support multiple clients, server can chat as admin.
  - `/Http` to use `HttpServer` (windows only).
  - `/Pipe` to use `NamedPipe` (windows only).
  - `/Socket`.
- `HttpServer` to accept customized port and first level url fragment for identifier.
- Windows stdio
- Windows socket
- Linux stdio
- Linux socket

## Optional
