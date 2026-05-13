# TODO

## 2.0

### IChannelServer

- `INetworkProtocol` implementations currently accept one client, need improvement.
  - `NamedPipeServer::WaitForClient` needs to stop when `NamedPipeServer::Stop` is called.
  - `HttpServer::WaitForClient` needs to stop when `HttpServer::Stop` is called.
  - Simple unit test:
    - One server two clients in their own threads.
    - Each client sends its name (`Tom` and `Jerry`) to server.
    - When server receives both, send `OK`. After that when server receives `ReceiverName:Message`, it redirects to the client by change the name `SenderName:Message`.
    - `Tom` after receiving `OK`, sends `Jerry:Hello`.
    - `Jerry` after receiving `Tom:Hello`, sends `Tom:Good`, and `Bye`, release the thread.
    - `Tom` after receiving `Jerry:Good`, sends `Bye`, release the thread.
    - When server receives both `Bye`, stops, release the thread.
    - Each callback argument assertions writes to each bool variables.
    - One more thread to wait for 10 seconds verify if server and clients all end.
    - When all threads end, kill the 10 seconds thread first, `TEST_ASSERT` if all variables are `true`.
    - Run it on `HttpServer` and `NamedPipeServer`.
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
  - `/Http` to use `HttpServer` (windows only, `localhost:8765/VlppOSTestPipe`).
  - `/Pipe` to use `NamedPipe` (windows only, `\\.\pipe\VlppOSTestPipe`).
  - `/Socket`.
- `HttpServer` to accept customized port and first level url fragment for identifier.
- Windows stdio
- Windows socket
- Linux stdio
- Linux socket

## Optional
