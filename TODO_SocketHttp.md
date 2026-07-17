# Http Service on TCP Socket

- Files are based in `Source/InterProcess`.
- [x] [Async Socket on Multiple Platforms](./TODO_SocketHttp_AsyncSocket.md)
- [x] [HTTP Request and Response Transmission](./TODO_SocketHttp_HttpRequest.md)
- [x] [Minimized HTTP Server/Client](./TODO_SocketHttp_MiniHttpApi.md)
  - [x] Multiple HTTP server shares one HTTP request server, dispatching by URL prefix.
  - [x] Test application to host a website, test against firefox/safari/chrome on different platforms.
  - [x] [INetworkProtocol(Server|Client) implementation](./TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md)
    - Figure out why there are so many parsing code, since actual parsing should belong to api layer.
    - Move code to correct place, remove duplicate logic.
- [ ] Recognize time consume test cases and put them in Release only
  - Reorganize test cases in proper test categories.
  - Some is resolvable when `WINHTTP_OPTION_IPV6_FAST_FALLBACK` is supported.
