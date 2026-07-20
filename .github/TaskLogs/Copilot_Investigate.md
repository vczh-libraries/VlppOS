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

- No.1 INJECT SOCKET COMPOSITION AND SHARE ONLY THE SERVER LISTENER

## No.1 INJECT SOCKET COMPOSITION AND SHARE ONLY THE SERVER LISTENER

Move both default factory declarations into the platform-neutral async-socket contract and define the same unqualified functions from each platform translation unit. Call sites and portable tests can then use `CreateDefaultAsyncSocketServer` and `CreateDefaultAsyncSocketClient` directly without selecting a platform namespace or concrete type.

Change `SocketHttpServerApi` to accept an injected server, port, and origin-form path prefix. Retain the existing OPTIONS policy as a defaulted fourth argument so the requested three-argument form is available without deleting observable CORS behavior. Normalize trailing slashes, synthesize the absolute `GetUrlPrefix()` value as `http://localhost:port/prefix`, preserve the existing decoded-path policy, and dispatch only exact or slash-delimited descendants. A small registry keyed by `IAsyncSocketServer*` remains necessary because one native server can install only one callback. It will coordinate one `HttpRequestServer` for every group of APIs sharing the exact same pointer, but it will no longer create listeners, select platforms, retry binds, or join unrelated objects merely because their ports match.

Change `SocketHttpClientApi` to accept an injected client, server name, and port. It will validate the loopback server and port separately and construct the same Host authority used today, matching the constructor shape of `windows_http::HttpClientApi`.

Change `SocketHttpServer` to accept and forward `(Ptr<IAsyncSocketServer>, port, urlPrefix)` without retaining or creating another socket server.

The requested literal `SocketHttpClient(Ptr<IAsyncSocketClient>, server, port)` shape cannot preserve the existing `INetworkProtocolClient` contract. One logical Socket HTTP client simultaneously owns a long-poll receive connection and a control/send connection, and either physical lane can require replacement after transport failure. `IAsyncSocketClient` exposes exactly one connection, and two `SocketHttpClientApi` objects cannot share it because both install callbacks on that connection. The logical client also needs the URL prefix in order to reach a non-root `SocketHttpServer`. Reusing one pointer would deadlock ordinary sends behind the infinite poll; dropping the second lane would remove full-duplex behavior and Windows interoperability.

Therefore the behavior-preserving constructor will inject `SocketHttpClient::NativeClientFactory` as its first argument, followed by `server`, `port`, and `urlPrefix`. The common `CreateDefaultAsyncSocketClient` function can be passed directly as that factory. This keeps every physical socket choice at the composition boundary, removes internal platform selection, retains the required two-lane and replacement behavior, and preserves non-root protocol prefixes.

Add constructor remarks to `HttpRequestServer`, `HttpRequestClient`, `SocketHttpServerApi`, `SocketHttpClientApi`, `SocketHttpServer`, and `SocketHttpClient` stating that explicit socket dependency injection is intentional and platform socket creation must remain at the caller's composition boundary. The `SocketHttpClient` remark will explain why its dependency is a factory rather than one pointer.

Adapt the Mini HTTP test suite to pass one explicit server pointer to all APIs that share a listener, replace the five-retry test with one-attempt propagation from an injected server, and add the explicit `/ABC/def` versus `/ABC/defghi` case using one client. Collapse platform-selected test registrations and native constructions to the common factory functions, retaining preprocessor guards only for genuinely Windows-only interoperability or concrete-type template coverage. Update the MiniHttpServer sample and all affected knowledge-base/manual construction examples.

Verification requires the Debug/x64 solution build, the complete unit-test suite with no memory-leak report, and focused review of all remaining platform guards and factory declarations.

### CODE CHANGE
