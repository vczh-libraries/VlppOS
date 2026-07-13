# Http Service on TCP Socket

Implements:
- socket api layer
- text network protocol on socket api layer
- http api layer on socket api layer
- text network protocol on http api layer on socket api layer
  - compatible with windows http api implementation

- Files are based in `Source/InterProcess`.
- [IAsyncSocket(Server|Client)](./TODO_SocketHttp_AsyncSocket.md)
- [IHttpRequest(Server|Client) on IAsyncSocket(Server|Client)](./TODO_SocketHttp_HttpRequest.md)
- [SocketHttp(server|client) based on IHttpRequest(Server|Client)](./TODO_SocketHttp_MiniHttpApi.md)
- [INetworkProtocol(Server|Client) based on SocketHttp(Server|Client)](./TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md)

## Refactoring for Preparation

Http(Server|Client)(Api)?.Windows.(h|cpp) moved from `Windows` to `Windows/HTTP` folder, using `vl::inter_process::windows_http` namespace.
NamedPipe.Windows.(h|cpp) using `vl::inter_process::named_pipe` namespace.
