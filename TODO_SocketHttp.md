# Http Service on TCP Socket

- Files are based in `Source/InterProcess`.
- [Async Socket on Multiple Platforms](./TODO_SocketHttp_AsyncSocket.md)
- [HTTP Request and Response Transmission](./TODO_SocketHttp_HttpRequest.md)
- [Minimized HTTP Server/Client](./TODO_SocketHttp_MiniHttpApi.md)
  - [INetworkProtocol(Server|Client) implementation](./TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md)

## Refactoring for Preparation

Http(Server|Client)(Api)?.Windows.(h|cpp) moved from `Windows` to `Windows/HTTP` folder, using `vl::inter_process::windows_http` namespace.
NamedPipe.Windows.(h|cpp) using `vl::inter_process::named_pipe` namespace.
