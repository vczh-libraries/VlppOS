# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

- Move extern of `CreateDefaultAsyncSocket(Server|Client)` to `AsyncSocket.h` right after the declaration of `AsyncSocketClientRetryDelay`.
  - Delete all other externs.
  - Notice that all 3 platforms have the same socket factory function declaration so no need to guard with macros.
  - Use them directly in all test cases, so to remove unnecessary guarding macros.
- Change`SocketHttpServerApi`'s constructor to `(Ptr<IAsyncSocketServer>, port, urlPrefix)`.
  - Remove all socket server creating code because it is not necessary anymore.
  - `urlPrefix` would be like `/ABC/def`, which means this server api is listening to `localhost:port/ABC/def` and `localhost:port/ABC/def/...`, it does not listen to `localhost:port/ABC/defghi`.
  - If `urlPrefix` ends with `/` ignore the `/`
- Change`SocketHttpClientApi`'s constructor to `(Ptr<IAsyncSocketClient>, server, port)`.
  - This keeps the same behavior with `windows_http::HttpClientApi`.
- `SocketHttp(Server|Client)`'s constructor should pass `Ptr<IAsyncSocket(Server|Client)>` as the first constructor argument, they are no going to access or create `IAsyncSocket(Server|Client)` in any other way:
  - `SocketHttpServer(socketServer, port, urlPrefix)` to match `SocketHttpServerApi`.
  - `SocketHttpClient(socketClient, server, port)` to match `SocketHttpClientApi`.
- In `SocketHttp(Server|Client)`, `SocketHttp(Server|Client)Api`, `HttpRequest(Server|Client)`'s constructor comment:
  - Say in your own world that, requiring to pass `IAsyncSocket(Server|Client)` is by design, do not change this design.
- Add new test case(s) to `TestInterProcess_AsyncSocket_MiniHttpApi.cpp` to test against:
  - Multiple `SocketHttpServerApi` is starting with the same `Ptr<IAsyncSocketServer>` but using different `urlPrefix`, and they could just work together. You can use one single `SocketHttpClientApi` to call all of them and make sure they all respond as expected.
- Update the knowledge base with this change.
- Commit and push all local changes to master.

# UPDATES

# TEST [CONFIRMED]

The current public surface and its existing tests confirm the design problem:

- `SocketHttpServerApi` accepts an absolute URL plus an OPTIONS flag, then calls a platform-selected listener factory internally. The test `SocketHttpServerApi shares one port and dispatches every persistent request to the longest prefix` proves that prefix sharing currently works only through the internal port registry, not through an explicitly shared `Ptr<IAsyncSocketServer>`.
- The same test already proves the important routing boundary on one persistent client: `/api` and `/api/...` select the `/api` registration, while `/api2` does not. It also proves longest-prefix dispatch and that stopping one registration leaves the others usable.
- The concurrent-start and occupied-port tests prove that `SocketHttpServerApi` currently owns listener creation, publication, and five-attempt bind retry. Those responsibilities must disappear when the listener is supplied by the caller.
- Source inspection confirms that the default factory declarations are repeated behind platform guards, and that `SocketHttpClientApi` accepts one precombined authority instead of the requested server and port.

The regression coverage will adapt the existing shared-prefix test and add an explicit `/ABC/def` boundary case. Multiple APIs will receive the exact same `Ptr<IAsyncSocketServer>`, use different normalized prefixes, and be queried through one `SocketHttpClientApi`. Success requires exact-prefix and descendant routing, rejection of a textual near-match such as `/ABC/defghi`, longest-prefix behavior, independent API shutdown, and final listener shutdown.

Baseline verification on 2026-07-20:

- Debug/x64 `UnitTest.sln` build succeeded with 0 warnings and 0 errors.
- All 15 test files and 215 test cases passed.
- `Execute.log` contains no `Detected memory leaks!` report.

# PROPOSALS
