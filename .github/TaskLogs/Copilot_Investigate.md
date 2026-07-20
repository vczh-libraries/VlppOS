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

## UPDATE

I found a design mistake, accepting a Ptr<IAsyncSocket(Server|Client)> makes passing “port” not making sense, because the socket already locks in a port. So I hope you already found that, if not remove these port constructor arguments. By the way, the new test would be multiple http servers listening to the same port with different urlPrefix

## UPDATE

SocketHttpClient should not need `clientFactory` in its constructor because the first argument is already the socket client

## UPDATE

by the way, http client should treat 404 as a fatal local error, and I think it should be properly handled by `HttpRequestClient` as this is the class who actually deal with socket client directly.

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

- No.1 INJECT SOCKET COMPOSITION AND SHARE ONLY THE SERVER LISTENER [CONFIRMED]
- No.2 DERIVE THE HTTP PORT FROM THE INJECTED SOCKET [CONFIRMED]
- No.3 KEEP THE HIGH CLIENT POINTER-ONLY [CONFIRMED]
- No.4 MAKE 404 A STRUCTURED FATAL CLIENT FAILURE [CONFIRMED]

## No.1 INJECT SOCKET COMPOSITION AND SHARE ONLY THE SERVER LISTENER

Move both default factory declarations into the platform-neutral async-socket contract and define the same unqualified functions from each platform translation unit. Call sites and portable tests can then use `CreateDefaultAsyncSocketServer` and `CreateDefaultAsyncSocketClient` directly without selecting a platform namespace or concrete type.

Change `SocketHttpServerApi` to accept an injected server and origin-form path prefix. Retain the existing OPTIONS policy as a defaulted third argument. Normalize trailing slashes, derive the port from the server, synthesize the absolute `GetUrlPrefix()` value as `http://localhost:port/prefix`, preserve the existing decoded-path policy, and dispatch only exact or slash-delimited descendants. A small registry keyed by `IAsyncSocketServer*` remains necessary because one native server can install only one callback. It will coordinate one `HttpRequestServer` for every group of APIs sharing the exact same pointer, but it will no longer create listeners, select platforms, retry binds, or join unrelated objects merely because their ports match.

Change `SocketHttpClientApi` to accept an injected client and server name. It will validate the loopback server, read the port from the client, and construct the same Host authority used today.

Change `SocketHttpServer` to accept and forward `(Ptr<IAsyncSocketServer>, urlPrefix)` without retaining duplicate endpoint state or creating another socket server.

One logical Socket HTTP client simultaneously owns a long-poll receive connection and a control/send connection, and either physical lane can require replacement after transport failure. Reusing one physical connection would deadlock ordinary sends behind the infinite poll; dropping the second lane would remove full-duplex behavior and Windows interoperability. The public constructor nevertheless needs only `(Ptr<IAsyncSocketClient>, server, urlPrefix)`: it uses the exact injected object first, while `IAsyncSocketClient::CreateSameEndpointClient()` supplies distinct fresh same-transport clients for the other lane and recovery.

Add constructor remarks to `HttpRequestServer`, `HttpRequestClient`, `SocketHttpServerApi`, `SocketHttpClientApi`, `SocketHttpServer`, and `SocketHttpClient` stating that explicit socket dependency injection is intentional and platform socket creation must remain behind the injected composition boundary.

Adapt the Mini HTTP test suite to pass one explicit server pointer to all APIs that share a listener, replace the five-retry test with one-attempt propagation from an injected server, and add the explicit `/ABC/def` versus `/ABC/defghi` case using one client. Collapse platform-selected test registrations and native constructions to the common factory functions, retaining preprocessor guards only for genuinely Windows-only interoperability or concrete-type template coverage. Update the MiniHttpServer sample and all affected knowledge-base/manual construction examples.

Verification requires the Debug/x64 solution build, the complete unit-test suite with no memory-leak report, and focused review of all remaining platform guards and factory declarations.

### CODE CHANGE

- Move the two default factory declarations to `AsyncSocket.h`, define all three platform implementations in `vl::inter_process::async_tcp_socket`, and remove guarded duplicate declarations and portable platform selection.
- Refactor `SocketHttpServerApi` around a caller-supplied server and a registry keyed by the exact `IAsyncSocketServer*`. The registry retains one `HttpRequestServer`, coordinates concurrent publication and callback draining, and stops the listener after its final prefix is removed.
- Normalize server prefixes as origin paths, derive the absolute getter from the injected server, and preserve longest-prefix routing with exact-or-slash-delimited boundary matching.
- Refactor `SocketHttpClientApi` and `SocketHttpServer` to forward the injected socket dependency without constructing platform sockets internally.
- Add the requested constructor-design remarks and update the sample, portable tests, knowledge-base pages, and manual to use the common factories and explicit composition boundary.

### CONFIRMED

The common factory declarations now have one platform-neutral public location and link to the selected Windows, Linux, or macOS implementation. The server registry shares a listener only among API objects receiving the exact same server pointer; it starts that listener once, rejects duplicate normalized prefixes, keeps other prefixes active when one API stops, and stops the listener after the last API stops. It no longer creates listeners, selects a platform, retries binds, or groups unrelated server objects merely because their ports match.

The shared-listener regression supplies one server pointer to the `/ABC/def` and `/XYZ` APIs and uses one client to reach both. It confirms exact and descendant routing, rejects the textual near-match `/ABC/defghi`, and verifies independent API and final-listener shutdown. The complete test suite and browser sample verification confirm the refactored composition and routing behavior.

## No.2 DERIVE THE HTTP PORT FROM THE INJECTED SOCKET

Add `GetPort()` to both common async-socket interfaces and implement it as the immutable constructor port on Windows, Linux, and macOS. Remove `port` from `SocketHttpServerApi`, `SocketHttpClientApi`, and `SocketHttpServer`; they derive their absolute prefix, Host authority, and validation endpoint from the injected pointer.

For the high-level logical client, use `(client, server, urlPrefix)`. The injected `Ptr<IAsyncSocketClient>` is the endpoint source and first physical lane. Its `CreateSameEndpointClient()` method supplies the simultaneous second lane and transport recovery; every returned client must be distinct, fresh, `Ready`, and report the same port.

Update the regression to make the intended topology explicit: several `SocketHttpServerApi` objects receive the same server pointer, therefore listen on the same locked-in port, and use distinct normalized prefixes that one client can query independently.

### CODE CHANGE

- Add immutable `GetPort()` contracts to `IAsyncSocketServer` and `IAsyncSocketClient` and return each native object's constructor port on Windows, Linux, and macOS.
- Remove `port` from the constructors of `SocketHttpServerApi`, `SocketHttpClientApi`, `SocketHttpServer`, and `SocketHttpClient`.
- Derive the server's absolute URL, Host validation endpoint, and the client's Host authority from the injected socket. Update all call sites, tests, samples, and documentation accordingly.

### CONFIRMED

Every HTTP adapter now has one endpoint source: its injected socket. Source and documentation scans find no active HTTP constructor retaining a duplicate port argument. Native coverage confirms `GetPort()` is stable on all three implementations, while the shared-pointer regression demonstrates that several HTTP servers necessarily use the one port locked into their common injected listener.

## No.3 KEEP THE HIGH CLIENT POINTER-ONLY

Add `IAsyncSocketClient::CreateSameEndpointClient()` to the common contract and implement it on Windows, Linux, and macOS by constructing a fresh native client for the immutable endpoint. Remove `NativeClientFactory` from `SocketHttpClient`; retain the injected pointer as its transport-composition source, consume that exact client for the first lane, and use the same-endpoint method for all later lanes. Validate non-null, distinct, `Ready`, same-port results and make contract violations terminal instead of spinning in a replacement loop.

### CODE CHANGE

- Add `IAsyncSocketClient::CreateSameEndpointClient()` and implement it with a fresh same-transport, same-port native client on Windows, Linux, and macOS.
- Remove `NativeClientFactory` and its constructor argument from `SocketHttpClient`.
- Consume the exact injected client for the first physical lane, serialize later lane creation through the injected endpoint source, and validate every returned client as non-null, distinct, `Ready`, and on the same port. Treat a broken clone contract as terminal so recovery cannot spin.

### CONFIRMED

The public high-level client constructor is pointer-only apart from its server name and URL prefix. It uses the exact supplied object first and obtains the simultaneous second lane and recovery connections through that object's transport-preserving clone contract. Endpoint-cloning coverage confirms distinct fresh clients report the same immutable port, and lifecycle/concurrency review found no replacement-loop or lane-ownership defect.

## No.4 MAKE 404 A STRUCTURED FATAL CLIENT FAILURE

Add `HttpResponseFailure::NotFound` and `IHttpRequestCallback::OnReadResponseFailure`. Direct `HttpRequestConnection` remains a general parser by default, but `HttpRequestClient` enables the fatal-404 policy because it owns the physical socket client. A 404 skips ordinary response delivery, reports the structured fatal failure, and stops the connection. `SocketHttpClientApi` exposes `SocketHttpClientErrorCode::ResponseNotFound`; the high logical client recognizes it on `/Connect`, `/Request`, and `/Response` and stops immediately with one fatal local error.

### CODE CHANGE

- Add `HttpResponseFailure::NotFound` and `IHttpRequestCallback::OnReadResponseFailure` without changing the general parser's default response policy.
- Enable fatal-404 classification in `HttpRequestClient`, skip ordinary response delivery for that status, emit the structured failure, and stop the owned physical socket client.
- Add `SocketHttpClientErrorCode::ResponseNotFound`, map the lower-layer failure through `SocketHttpClientApi`, and make `SocketHttpClient` recognize it on `/Connect`, `/Request`, and `/Response` as one fatal local error with no retry.
- Add direct and logical-client regressions for permissive raw parsing, owned-client shutdown, structured error mapping, and every logical protocol route.

### CONFIRMED

Direct `HttpRequestConnection` remains permissive and delivers a normal 404 response, preserving its role as a general parser. `HttpRequestClient`, which directly owns the socket client, instead emits `HttpResponseFailure::NotFound` and stops that client. The API mapping exposes `ResponseNotFound`, and high-level `/Connect`, `/Request`, and `/Response` regressions confirm exactly one fatal local error, no ordinary response delivery, and no endpoint retry.

Final verification on 2026-07-20:

- Debug/x64 `UnitTest.sln` build succeeded with 0 warnings and 0 errors.
- All 15 test files and 221 test cases passed, including default-factory endpoint cloning, the same-pointer `/ABC/def` and `/XYZ` routing case with `/ABC/defghi` boundary rejection, permissive raw 404 parsing, owned-client fatal-404 shutdown, and one-attempt logical failures on `/Connect`, `/Request`, and `/Response`.
- `Execute.log` contains no `Detected memory leaks!` report.
- `MiniHttpServer` started both the root site and `/Assets` site, then exited normally. A Chromium browser loaded both website pages, the Assets module, and cross-origin JSON; it also handled the deterministic button action and reported zero browser errors. Direct probes returned 404 for paths outside the `/Assets` segment boundary, and ports 8888 and 8889 were released after shutdown.
