investigate repro

# Socket HTTP network protocol adapter

Implement the portable `INetworkProtocol*` compatibility adapter described by [TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md](./TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md) on top of the completed `vl::inter_process::async_tcp_socket::SocketHttpServerApi` and `vl::inter_process::async_tcp_socket::SocketHttpClientApi`.

The public adapter types are `vl::inter_process::async_tcp_socket::SocketHttpServer` and `vl::inter_process::async_tcp_socket::SocketHttpClient`. Implement them in:

- `Source/InterProcess/AsyncSocket/AsyncSocket_HttpServer.h/.cpp`
- `Source/InterProcess/AsyncSocket/AsyncSocket_HttpClient.h/.cpp`

This is the final unchecked item in [TODO_SocketHttp.md](./TODO_SocketHttp.md). It must preserve the successful legacy Windows HTTP wire protocol while using the portable MiniHttp stack and native async-socket implementations on Windows, Linux, and macOS.

## Authoritative corrections from the completed dependencies

Treat `TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md` as the wire-level source of truth, with these corrections for the code that actually landed:

- The portable application layer is named `SocketHttpServerApi`, `SocketHttpClientApi`, and `SocketHttpRequestContext`; the old proposed `SocketHttpServer` / `SocketHttpClient` names are now available for this `INetworkProtocol*` adapter.
- The request layer has concrete `HttpRequestServer` and `HttpRequestClient` wrappers over `IHttpRequestConnection`; there are no `IHttpRequestServer` or `IHttpRequestClient` interfaces.
- `Source/InterProcess/NetworkProtocolHttp.h/.cpp` already contains the shared route constants, Windows-shaped `windows_http::HttpRequest` / `HttpResponse` / `HttpError` values, UTF-8 body helpers, and URL query helpers. Do not recreate or move them.
- `SocketHttpServerApi` takes an absolute URL prefix and owns shared native-listener selection internally. It intentionally has no public `IAsyncSocketServer` injection constructor. Tests bind the requested native server through the existing private `SetSocketHttpServerListenerFactoryForTesting` seam.
- One `SocketHttpClientApi` owns one `HttpRequestClient`, serializes one physical connection, and becomes terminal after a transport/framing/timeout failure. The adapter therefore needs two live client APIs and a way to create replacements; two fixed `Ptr<IAsyncSocketClient>` objects are insufficient for the documented retry policy.
- `SocketHttpRequestContext::Respond` is one-shot and reports physical delivery through `Func<void(bool)>`; `Cancel` is the supported way to abandon an outdated long poll and closes that physical connection.
- The completed MiniHttp layer already owns HTTP parsing, fixed response framing, `Date`, no-cache policy, CORS, `OPTIONS`, response deadlines, prefix dispatch, listener sharing, and callback draining. Reuse these behaviors rather than adding another parser or another server registry.

Do not use the direct length-framed `async_tcp_socket::NetworkProtocolServer<TAsyncSocketServer>` / `NetworkProtocolClient<TAsyncSocketClient>` implementation for this adapter. It remains a separate portable transport and is only a test-pattern reference.

## Public adapter surface

Use this public shape unless implementation evidence requires a small signature correction, in which case record the correction before coding:

```cpp
namespace vl::inter_process::async_tcp_socket
{
	class SocketHttpServer
		: public SocketHttpServerApi
		, public virtual INetworkProtocolServer
	{
	protected:
		void                                OnHttpRequestReceived(Ptr<SocketHttpRequestContext> context) override;
		void                                OnHttpServerStopping() override;

	public:
		SocketHttpServer(const WString& baseUrl, vint port);
		~SocketHttpServer();

		virtual WaitForClientResult          OnClientConnected(INetworkProtocolConnection* connection) override;
		void                                Start() override;
		void                                Stop() override;
		bool                                IsStopped() override;
	};

	class SocketHttpClient
		: public Object
		, public virtual INetworkProtocolClient
		, public virtual INetworkProtocolConnection
	{
	public:
		using NativeClientFactory = Func<Ptr<IAsyncSocketClient>(vint)>;

		SocketHttpClient(const WString& baseUrl, vint port);
		SocketHttpClient(const WString& baseUrl, vint port, NativeClientFactory clientFactory);
		~SocketHttpClient();

		INetworkProtocolConnection*         GetConnection() override;
		void                                WaitForServer() override;
		ClientStatus                        GetStatus() override;
		void                                InstallCallback(INetworkProtocolCallback* callback) override;
		void                                BeginReadingLoopUnsafe() override;
		void                                SendString(const WString& str) override;
		void                                Stop() override;
	};
}
```

The two-argument client constructor uses a private platform-guarded default factory for `windows_socket::AsyncSocketClient`, `linux_socket::AsyncSocketClient`, or `macos_socket::AsyncSocketClient`. The factory overload is per instance and must be used by the shared tests to select the exact native client type. Every call to the factory creates a fresh client for a new physical connection; validate that it returns a non-null object. Do not add mutable global client-factory state when the per-instance factory solves retries and testing directly.

Both adapter classes are non-copyable and non-movable. `SocketHttpServer` must remain inheritable so `NetworkProtocolChannelServer<..., SocketHttpServer>` and the test callback host can derive from it. Construction initializes state only; `Start` begins callbacks after the most-derived object exists. Every most-derived server destructor must call `Stop` before destroying fields visible to `OnClientConnected` or connection callbacks, even though `SocketHttpServer` also performs a final safety stop.

Validate `baseUrl` before starting asynchronous work. It is either empty or a legal ASCII origin-form path prefix that begins with `/`, has no trailing `/`, query, fragment, backslash, NUL, raw non-ASCII, malformed percent escape, or percent-encoded path separator or NUL. A caller that needs non-ASCII path text supplies its canonical percent-encoded UTF-8 bytes. At construction, validate the fixed `{baseUrl}{HttpServerUrl_Connect}` target against `HttpRequestLineSizeLimit`, including HTTP start-line overhead; the server must also reserve the documented maximum locally generated token length for both token-bearing routes. The client cannot know a remote server's opaque paths yet, so after every `/Connect` response validate both actual `{baseUrl}{returnedPath}` targets as legal ASCII origin forms within the same start-line limit before entering `Connected`. An illegal or oversized returned path is a malformed `/Connect` attempt handled by its retry policy. Use the empty string for the origin root. Construct the server prefix as `http://localhost:{port}{baseUrl}` and every client authority as `localhost:{port}`. The TCP client still connects to `127.0.0.1`; the exact `localhost` HTTP authority is required for HTTP.sys interoperability.

## Logical connection and physical connection model

One opaque GUID-like token identifies one logical `INetworkProtocolConnection`. A TCP connection or `SocketHttpRequestContext` does not.

```text
logical connection {token}
|- receive lane: one SocketHttpClientApi with one pending POST /Request/{token}
`- control/send lane: one SocketHttpClientApi for GET /Connect, then FIFO POST /Response/{token}
```

- The server owns the token-to-logical-connection map above every physical HTTP request context.
- The client maintains at least the two lanes above because a FIFO HTTP/1.1 connection cannot send `/Response` while its infinite `/Request` exchange is pending.
- A terminal transport error replaces only the affected physical lane through `NativeClientFactory`; it does not create a new logical token except while retrying `/Connect` before logical connection succeeds.
- EOF, cancellation, or replacement of one physical HTTP connection must not remove the logical server connection.
- There is no disconnect route, heartbeat, acknowledgement, deduplication, or abandoned-token reclamation. A logical server connection is removed only by its local `Stop` or whole-server shutdown. Preserve the documented possibility of both duplicate delivery and loss around retries and lost HTTP responses; do not claim exactly-once or unconditional at-least-once delivery.
- Receive-lane, send-lane, and server callbacks may run concurrently and have no total cross-lane ordering.

Generate a fresh fixed-length, URL-safe token for every accepted `/Connect`; a canonical 36-character lowercase UUID form keeps the legacy bound. It must be unique among active connections and treated as opaque by both peers. Do not introduce a Windows RPC dependency into common code; generate the bounded GUID-format value portably and collision-check it against the connection map.

## HTTP wire construction

Use `HttpServerUrl_Connect`, `HttpServerUrl_Request`, and `HttpServerUrl_Response` from `NetworkProtocolHttp.h`. Route server requests with `SocketHttpRequestContext::GetRelativePath()`; it is already prefix-relative and UTF-8 decoded. Do not decode it again. The paths returned by `/Connect` intentionally exclude `baseUrl`, and the client prepends its validated `baseUrl` exactly once.

Use the exact legacy media type everywhere it is required:

```text
application/json; charset=utf8
```

- Each logical message is one nonempty `WString` encoded directly as UTF-8 bytes, without BOM, terminator, JSON quoting, compression, chunks at the logical layer, or trailers. Before either server or client accepts a send, reject embedded NUL characters and reject an encoded byte count greater than the existing `HttpBodySizeLimit`. This is a synchronous contract failure, not an HTTP attempt to retry.
- `GET {baseUrl}/VlppInterProcess/Connect` has an empty body and sends `Accept` with the exact legacy media type.
- `POST {baseUrl}/VlppInterProcess/Request/{token}` has an empty body, exact `Accept`, explicit `Content-Length: 0`, and infinite `receiveTimeout`.
- `POST {baseUrl}/VlppInterProcess/Response/{token}` has exact `Accept` and `Content-Type`, a fixed positive UTF-8 byte `Content-Length`, and exactly one message body.
- Every successful route response is `200 OK` with the exact legacy `Content-Type`, including successful empty `/Request` or `/Response` bodies. Build `async_tcp_socket::HttpResponse` objects and let `SocketHttpServerApi` add legal `Content-Length`, `Date`, cache, and CORS fields.
- A successful empty response means no logical message and never calls `OnReadString`.
- Validate a `/Connect` response as exactly two nonempty semicolon-separated origin-form paths before entering the connected state. Keep their token/path components otherwise opaque.
- On `/Response`, require exactly one legal fixed `Content-Length` greater than zero, no `Transfer-Encoding`, the exact content type, a complete body of that byte size, valid UTF-8, and no NUL. Return a non-`200` response for invalid input.

The completed MiniHttp policy has two harmless differences from the old HTTP.sys route dispatcher:

- methods outside `GET`, `HEAD`, `POST`, and `OPTIONS` receive MiniHttp's global `501` before adapter dispatch instead of the legacy route-level `404`;
- `respondToOptions = true` returns MiniHttp's compatible CORS/`OPTIONS` superset, which also advertises `HEAD` and `Accept`.

Do not fork or weaken the completed MiniHttp policy to reproduce those negative-path details. Successful routes and the exact media type/body convention remain wire-compatible with `windows_http::HttpServer` and `windows_http::HttpClient`.

## Server logical-connection behavior

Implement each logical server connection as an internal `INetworkProtocolConnection` object owned by the server map. `BeginReadingLoopUnsafe` is a no-op because `SocketHttpServerApi` already dispatches completed requests.

### `GET /Connect`

1. Create the token and logical connection and publish it in the map before calling `SocketHttpServer::OnClientConnected` outside all internal locks.
2. If accepted, respond with `{HttpServerUrl_Request}/{token};{HttpServerUrl_Response}/{token}` as direct UTF-8 text with the exact success media type.
3. If rejected or the callback throws, remove and detach the connection, cancel any retained work, and return `404 Connection rejected` without reporting a successful connection.
4. A lost accepted response may leave an abandoned logical connection, matching the legacy retry behavior. Do not merge repeated `/Connect` calls or enforce the obsolete comment that it can be called only once.

### `POST /Request/{token}`

- Look up the token exactly. Unknown tokens and malformed adapter paths receive `404`.
- Retain at most one pending long-poll context and allow at most one in-flight long-poll `Respond` per logical connection. Cancel an older still-pending context outside the connection lock before retaining a newer one.
- If no response is in flight and an outbound string is already queued, claim the oldest and respond through the pending context immediately. If a replacement poll or another send arrives while a prior response is in flight, retain/queue it but do not service it yet; otherwise a later message could overtake the claimed one.
- Keep ownership of an in-flight outbound string until the `Respond` completion reports success. On success, service the retained poll from the next queued message. If `Respond` loses the race or its completion reports `false`, clear the stale in-flight state, restore that string at the front of the FIFO, and only then service the retained poll.
- A client normally issues the replacement poll only after the previous response, but the server must still handle deterministic poll replacement without holding locks across `Cancel`, `Respond`, or completion callbacks.

### `POST /Response/{token}`

- Validate and decode the entire fixed body before invoking user code. Deliver it exactly once through `OnReadString`, or queue it until callback installation if no callback exists yet.
- `InstallCallback` calls `OnInstalled` before any queued `OnReadString`, installs only one callback at a time, supports `nullptr` uninstallation, and replays queued strings outside locks. Prevent a concurrent inbound dispatch from overtaking `OnInstalled`.
- Application code may synchronously call `SendString` on the same logical connection from its inbound callback. Track that condition with a callback-frame/thread-local association, not a connection-wide boolean that could steal a concurrent send from another thread.
- Return the first synchronously generated string in this `/Response` HTTP response, queue any additional generated strings FIFO, and use one previously queued outbound string only when the callback generated none. A send on another logical connection follows that other connection's normal long-poll path.
- A piggybacked message is not application-acknowledged. Do not add exactly-once bookkeeping or requeue it merely because the HTTP response may have been lost after submission.

### Server sends and shutdown

- `SendString` outside a matching `/Response` callback immediately satisfies a pending poll when possible; otherwise it queues FIFO.
- Never invoke `OnClientConnected`, `OnInstalled`, `OnReadString`, `OnLocalError`, `OnDisconnected`, `Respond` completion, or `SocketHttpServerApi` callbacks while holding connection, queue, map, or lifecycle locks.
- Logical connection `Stop` removes its token once, cancels its pending poll, drains entered callbacks, detaches it from the server, and calls `OnDisconnected` exactly once. It does not attempt an HTTP disconnect request.
- Whole-server `Stop` and unexpected native listener failure detach the complete map, cancel all pending contexts, drain all logical callbacks, and report one disconnection per accepted connection before returning from the hard shutdown boundary.
- External connection/server `Stop` calls are hard drains. If either is called reentrantly from `OnClientConnected`, `OnInstalled`, `OnReadString`, or `OnDisconnected`, wait for other entered callback frames but never for the current frame itself; keep the detached lifecycle state alive until that frame exits. Apply the same rule when a callback stops the whole server.
- Reuse `NetworkProtocolCallbackDomain` and the existing inter-process callback-frame/drain patterns where they fit instead of relying on callback owners being heap allocated or adding sleeps.

## Client state machine, retries, and ordering

`SocketHttpClient` implements both `INetworkProtocolClient` and its single logical `INetworkProtocolConnection`; `GetConnection()` returns `this`.

### Connect and start reading

- `GetStatus` reports `Ready`, `WaitingForServer`, `Connected`, and `Disconnected` from adapter state, not from one physical lane alone.
- `WaitForServer` can be called once. It creates the control/send API, waits for its native connection, performs `/Connect`, validates the two returned paths, then establishes the independent receive API before changing to `Connected` and invoking `OnConnected` outside locks. A failure while establishing that second physical connection is a silent receive-lane failure: publish each fresh bootstrap API before its blocking native wait so `Stop` can cancel it, retry with fresh APIs indefinitely while running, and never repeat `/Connect` or replace the logical token. `Stop` must unblock every in-progress wait.
- `BeginReadingLoopUnsafe` can begin once after logical connection. It only submits one infinite `/Request` on the already-connected receive API and must not perform a blocking native connection attempt.
- After every successful poll, enqueue/start its replacement on that same FIFO API from the response callback before delivering a nonempty body to `OnReadString`. This uses the completed `SocketHttpClientApi` callback-reentrant queue behavior and avoids a receive gap.
- A terminal `HttpError` on either lane schedules replacement worker work and returns from the lower HTTP/socket callback without blocking. The worker owns cleanup of the dead API, creates and publishes the replacement as the lane's current reconnecting API before blocking in its native `WaitForServer` so concurrent `Stop` can cancel/unblock it, retains the logical token, and retries only the operation owned by that lane. Track and drain every replacement worker and old API during `Stop`.
- Catch factory exceptions, null factory results, API-construction failures, and native `WaitForServer` failures inside every bootstrap/replacement worker. Convert each into the owning operation's silent retry or counted local-error/fatal transition, always complete the tracked worker state, and never let an exception escape into `ThreadPoolLite` and strand a lane in `reconnecting`.

### Send lane

- Reject empty or NUL-containing strings before queuing them. Encode one accepted string into one `/Response` body.
- Maintain an adapter-level FIFO with at most one active `/Response`. Do not submit every message directly to the lower API: a retry of the head message must not be overtaken by later accepted messages already queued in `SocketHttpClientApi`.
- On success, remove the head, start the next accepted send, then deliver any nonempty piggybacked response through `OnReadString`. Concurrent and callback-reentrant `SendString` calls append in deterministic acceptance order.
- `Stop` first rejects new sends and cancels the infinite poll. Give already accepted send-lane messages a bounded drain opportunity, including consecutive final messages used by `TestInterProcess.cpp`; then cancel any remainder and drain all lower callbacks. Use condition/event coordination, not sleeps or polling, and do not set the unsupported `windows_http::HttpRequest::keepAliveOnStop` flag.

### Retry policy

Preserve the legacy attempt policy, counting HTTP status/content-type failures and terminal transport failures consistently:

| Operation | Required behavior |
| --- | --- |
| `/Connect` | At most three immediate attempts. Attempts one and two call `OnLocalError(..., false)`; attempt three calls `OnLocalError(..., true)` and disconnects/unblocks `WaitForServer`. |
| `/Request/{token}` | Retry immediately and indefinitely while running, without a local-error callback. |
| `/Response/{token}` | Retry the same head body for at most three immediate attempts. Attempts one and two call `OnLocalError(..., false)`; attempt three calls `OnLocalError(..., true)` and disconnects. |

- Reuse a healthy API after a non-`200` or wrong-content-type HTTP response. Replace the affected API through `NativeClientFactory` after a terminal `HttpError` or physical disconnect.
- Keep the long poll infinite. Use the normal bounded response timeout for `/Connect` and `/Response`; do not apply that timeout to `/Request`.
- Validate every successful response before consuming it: `/Connect` must contain valid UTF-8, no NUL, and exactly the two legal paths; nonempty `/Request` and `/Response` bodies must contain valid UTF-8 and no NUL. Treat any malformed success body as a failed attempt owned by that operation, with the same retry, local-error, and fatal policy as a non-`200` or wrong-content-type response. Never let `CHECK_ERROR` escape from an asynchronous callback and never deliver malformed text.
- There is deliberately no retry delay, message id, acknowledgement id, or deduplication. A lost `/Response` reply can make the retried client message arrive twice.
- A fatal local error is delivered before `OnDisconnected`. `OnDisconnected` occurs exactly once. No protocol callback may touch the client after external `Stop` returns.
- Make `Stop` idempotent and reentrant from an adapter callback. Wait for other entered callbacks, but never wait for the current callback frame to unwind from inside itself; retain detached lifecycle state until it does.

## Shared verification in `TestInterProcess.cpp`

Add all adapter verification to the existing [TestInterProcess.cpp](./Test/Source/TestInterProcess.cpp). Do not create another test file and do not modify the `MiniHttpServer` browser application.

Reuse the existing `RunTextNetworkProtocol` and `RunNetworkProtocolChannel` helpers. They already verify two logical clients, connection statuses, callback installation, consecutive sends, raw routing, channel handshake/direct/broadcast/local-client behavior, explicit client/server shutdown, a five-second deadlock boundary, and `InterProcessTestRepeatCount` repetition.

Add common `AsyncSocket_HttpServer.h` / `AsyncSocket_HttpClient.h` includes, a `SocketHttpTextServer` callback host, a `SocketHttpChannelServer`, and one shared runner such as `RunSocketHttpNetworkProtocolTestCases<TNativeServer, TNativeClient>()`. Register the same raw-protocol and channel cases once; only native type binding varies by guard:

| Guard | Native server | Native client |
| --- | --- | --- |
| `VCZH_MSVC` | `async_tcp_socket::windows_socket::AsyncSocketServer` | `async_tcp_socket::windows_socket::AsyncSocketClient` |
| `VCZH_GCC && VCZH_APPLE` | `async_tcp_socket::macos_socket::AsyncSocketServer` | `async_tcp_socket::macos_socket::AsyncSocketClient` |
| `VCZH_GCC && !VCZH_APPLE` | `async_tcp_socket::linux_socket::AsyncSocketServer` | `async_tcp_socket::linux_socket::AsyncSocketClient` |

- Mirror `TestInterProcess_AsyncSocket_MiniHttpApi.cpp`: declare the existing server listener factory functions only in the test and keep an RAII `TNativeServer` factory scope alive until every server and callback drains.
- Construct `SocketHttpClient` with its per-instance `NativeClientFactory`, returning a fresh `TNativeClient(port)` for every lane or retry.
- Use `localhost:{port}` through the adapter and a leading/no-trailing-slash base URL.
- Pass `synchronizeServerStartup = true` on Windows, Linux, and macOS. These cases test protocol behavior, not the native client's separate retry-before-listen policy.
- Run portable `SocketHttpServer` against portable `SocketHttpClient` through both the raw and channel helpers on every platform.
- On Windows, add four dedicated cases in this file by running both successful cross-stack directions through both the raw and channel helpers:
  - socket-backed portable `SocketHttpServer` against the existing WinHTTP-backed `windows_http::HttpClient`;
  - existing HTTP.sys-backed `windows_http::HttpServer` against the socket-backed portable `SocketHttpClient`.
- Implement this `INetworkProtocol*` matrix again in `TestInterProcess.cpp`. Similar request/API coverage in [TestInterProcess_HttpRequest.cpp](./Test/Source/TestInterProcess_HttpRequest.cpp) and [TestInterProcess_AsyncSocket_MiniHttpApi.cpp](./Test/Source/TestInterProcess_AsyncSocket_MiniHttpApi.cpp) is lower-layer evidence and does not substitute for any of these four new raw/channel cases.
- Preserve all existing direct async-socket, NamedPipe, and legacy Windows HTTP cases. Use distinct, non-overlapping port ranges above the currently occupied `38000..38912` test range; for example `39000..39519` for the six new repeated matrices.
- Do not sleep. Use the existing events, startup barrier, timeout thread, repeated cases, and callback-thread failure reporting. The final consecutive sends must reach the server even when each client calls `Stop` immediately afterwards, proving the bounded send drain.

Also add mandatory deterministic focused cases in this same file. Use event/barrier-controlled scripted MiniHttp peers and the existing native listener factory; if an internal transition cannot be observed through the public APIs, add the smallest test-only seam beside the existing listener-factory seam and declare it only in the test. Do not use sleeps, duplicate the complete chat/channel scenarios, or repeat lower MiniHttp parser tests.

- Send a non-ASCII `WString` in each direction and verify the exact logical value after the UTF-8 request/response round trip, including the synchronous server reply path.
- Observe the receive-lane submission boundary and prove that the replacement `/Request` is enqueued before `OnReadString` is entered. In the same controlled exchange, call server `SendString` synchronously from the inbound `/Response` callback and prove that the first reply is piggybacked on that HTTP response rather than waiting for the poll.
- Script the first `/Response` attempt to fail and the retry to succeed while a second send is already accepted. Record request bodies and callback order as `first`, `first`, `second`; also force a terminal send-lane transport failure so the native-client factory count proves that a new physical API is connected without changing the logical token.
- Close a registered long-poll connection only after the server has claimed a queued message, wait for its `Respond` completion to report failure, then issue the replacement poll and prove it receives the original message. This validates failed-poll requeue without timing assumptions.
- Exercise both server and client outbound validation at `HttpBodySizeLimit` and one encoded byte beyond it, including a multibyte UTF-8 boundary. Prove that the limit is accepted and the oversized value fails synchronously before entering either adapter FIFO; do not put 16 MiB payloads into every repeated interoperability case.

## Source, project, and generated-file integration

- Add the four new product files explicitly to `Test/UnitTest/UnitTest/UnitTest.vcxproj`; wildcards are forbidden.
- Put all four under `Common\InterProcess\AsyncSocket` in `Test/UnitTest/UnitTest/UnitTest.vcxproj.filters`.
- `TestInterProcess.cpp` is already registered, so no new test source entry or solution project is required.
- The current Unix UnitTest directory is `Test/Linux/UnitTest`, not the historical `Test/Linux` root. Let its MSBuild-derived source list acquire the common `.cpp` files, and regenerate `vmake.txt` / `makefile` only through the absolute `.github/Ubuntu/build.sh`. Never hand-edit generated Unix files; commit the regenerated tracked `Test/Linux/UnitTest/vmake.txt` and `Test/Linux/UnitTest/makefile` with the implementation when that script changes them.
- Do not add the adapter sources to `MiniHttpServer.vcxproj`; that CLI consumes only `SocketHttpServerApi` and does not need `INetworkProtocol*`.
- Do not hand-edit `Release` outputs. Update only source/release-generation inputs if the repository's generator requires explicit registration.
- `Project.md` and `UnitTest.sln` need no structural change because no project is added.

## Documentation and knowledge-base reconciliation

Complete these updates with the implementation so documentation matches the new portable transport:

- Fix the stale source-of-truth reference in `TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md`: `NetworkProtocolHttp.h` already exists and the deleted `TODO_Task_MiniHttpApi.md` is no longer a future dependency.
- Correct the route comments in `Source/InterProcess/NetworkProtocolHttp.h`: `/Connect` is not `GET /Request`, repeated calls create separate logical connections, and `/Response` may return one piggybacked message.
- Mark the final adapter item in `TODO_SocketHttp.md` complete only after all implementation verification succeeds.
- Update `.github/KnowledgeBase/Index.md`, `.github/KnowledgeBase/Index_VlppOS.md`, `.github/KnowledgeBase/KB_VlppOS_InterProcessNetworkProtocolsAndChannels.md`, and `.github/KnowledgeBase/manual/vlppos/using-inter-process.md` to distinguish:
  - the portable direct length-framed `NetworkProtocolServer<T>` / `NetworkProtocolClient<T>` adapter;
  - the new portable HTTP-compatible `SocketHttpServer` / `SocketHttpClient` adapter;
  - the legacy Windows-only `windows_http::HttpServer` / `windows_http::HttpClient` reference implementation; and
  - the lower `SocketHttp*Api` and Windows `Http*Api` request/response helpers.
- Record the final public signatures, native-client factory choice, two-lane state machine, callback/shutdown ownership, retry behavior, Windows interoperability, project integration, and actual platform verification in `.github/TaskLogs/Copilot_Investigate.md`.

## Explicitly out of scope

- Changing native `IAsyncSocket*` interfaces, retry constants, or port-only concrete constructors.
- Reworking the direct length-framed `NetworkProtocolServer<T>` / `NetworkProtocolClient<T>` transport.
- Adding another HTTP parser, changing MiniHttp prefix/listener architecture, or adding public server-listener injection.
- Changing the `MiniHttpServer` CLI, browser fixtures, CORS browser workflow, or `Project.md` project list.
- Refactoring the legacy Windows HTTP transport except for necessary common comments/includes or a root-cause fix exposed by interoperability.
- Authentication, TLS/HTTPS, HTTP/2 or HTTP/3, proxying, cookies, compression, WebSocket, Server-Sent Events, binary logical messages, or REST semantics.
- A disconnect route, heartbeat, lease, abandoned-token collector, acknowledgement, deduplication, or exactly-once delivery.

## Verification and acceptance

Follow `.github/copilot-instructions.md`, `Project.md`, and all referenced coding, multithreading, source-file, build, unit-test, and native-dialog guidance.

- Establish a clean Debug x64 build and `TestInterProcess.cpp` baseline before product changes.
- Build `Test/UnitTest/UnitTest.sln` through `.github/Scripts/copilotBuild.ps1` for Debug/Release and Win32/x64. Require zero errors and resolve new warnings.
- Run focused `TestInterProcess.cpp` in Debug x64 through `.github/Scripts/copilotExecute.ps1`, then run the complete Debug x64 UnitTest suite.
- Inspect `Build.log` and `Execute.log` only after the wrappers finish. Require complete pass summaries and no post-summary Debug CRT memory-leak report.
- On Linux and macOS, build only from `Test/Linux/UnitTest` through the absolute `.github/Ubuntu/build.sh`, run the focused `/F:TestInterProcess.cpp /C` scenario asynchronously, then run the complete suite. Report only operating systems actually exercised; never infer one platform from another.
- Textually audit common headers for platform-specific leakage, the exact HTTP fields/body bytes, base URL and route construction, token-map ownership, callback installation ordering, callbacks outside locks, two-lane replacement, FIFO retry ownership, bounded send draining, reentrant hard shutdown, project/filter entries, and documentation consistency.
- Remove temporary diagnostics and test filters that were added only for investigation. Commit only intentional implementation, tests, project metadata, documentation, knowledge-base, and investigation evidence, then push the current branch.

Acceptance requires `SocketHttpServer` and `SocketHttpClient` in the requested files; exact successful wire interoperability with the Windows HTTP transport; two replaceable physical client lanes; deterministic send FIFO and retry ownership; mandatory deterministic coverage of non-ASCII UTF-8, long-poll replacement before callback delivery, same-response piggybacking, terminal lane replacement, send-head retry ordering, and pending-poll failure requeue; no callbacks under internal locks or after `Stop`; the same raw and channel scenarios bound to Windows, Linux, and macOS native socket types in `TestInterProcess.cpp`; all four dedicated Windows raw/channel cases across socket server with WinHTTP client and HTTP.sys server with socket client; clean builds/tests without leaks or deadlocks; and reconciled source/knowledge documentation.
