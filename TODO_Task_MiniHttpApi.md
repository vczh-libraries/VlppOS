investigate repro

# Minimized HTTP API on async sockets

Implement the cross-platform minimized HTTP application layer described by [TODO_SocketHttp_MiniHttpApi.md](./TODO_SocketHttp_MiniHttpApi.md) on top of the completed `HttpRequestServer`, `HttpRequestClient`, and `IHttpRequestConnection` layer in `Source/InterProcess/AsyncSocket`.

The main public classes are `vl::inter_process::async_tcp_socket::SocketHttpServerApi` and `vl::inter_process::async_tcp_socket::SocketHttpClientApi`. They provide a Windows-HTTP-API-like surface for prefix dispatch, deferred responses, CORS, browser-compatible HTTP behavior, and a native client. The later `INetworkProtocol*` compatibility adapter is outside this task.

## Authoritative corrections

Treat `TODO_SocketHttp_MiniHttpApi.md` as authoritative. In particular:

- Use the exact main names `SocketHttpServerApi` and `SocketHttpClientApi`; remove the old `SocketHttpServer` and `SocketHttpClient` API names from this task.
- Implement the product in `AsyncSocket_HttpServerApi.h/.cpp` and `AsyncSocket_HttpClientApi.h/.cpp` under `vl::inter_process::async_tcp_socket`.
- `SocketHttpServerApi` is constructed from an absolute URL prefix and `respondToOptions`, like `windows_http::HttpServerApi`. It does not take a caller-supplied `IAsyncSocketServer`.
- Multiple started server APIs on the same port share one internal specialized `Ptr<HttpRequestServer>`. The shared object dispatches every request by normalized URL prefix; it must not bind an accepted persistent connection permanently to one API.
- `SocketHttpClientApi` takes a `Ptr<IAsyncSocketClient>` plus explicit HTTP authority and constructs and owns one `HttpRequestClient`. No client sharing or client registry is needed.
- There is no route-registration table and no document-root setting. One derived `SocketHttpServerApi` is the handler for its prefix through `OnHttpRequestReceived`.
- Do not add a file-to-content callback, static-file fallback, MIME suffix table, upload API, or download API. Applications provide response bytes and metadata themselves.
- Browser automation and physical-folder hosting belong to the separate [TODO_Task_MiniHttpApi_TestApp.md](./TODO_Task_MiniHttpApi_TestApp.md). This task verifies browser-relevant wire behavior with socket and Windows HTTP interoperability tests.

## Common HTTP refactor first

Make the existing Windows client value types reusable before implementing `SocketHttpClientApi`.

1. Add `Source/InterProcess/NetworkProtocolHttp.h` and move the declarations of `windows_http::HttpRequest`, `windows_http::HttpResponse`, and `windows_http::HttpError` out of `Windows/HttpClientApi.Windows.h`. Keep their existing namespace and qualified names so current callers remain source-compatible.
2. Change `HttpError::errorCode` from `DWORD` to `vuint32_t`. Windows error values still fit exactly; common code no longer depends on a Windows typedef.
3. Move `HttpRequest::SetBodyUtf8` and `HttpResponse::GetBodyUtf8` out of `HttpClientApi.Windows.cpp` into platform-neutral common code. Add `NetworkProtocolHttp.cpp` if an implementation file is needed. Use VlppOS encoding/stream APIs, not Windows conversion functions.
4. Share URL query encode/decode behavior between `windows_http::HttpClientApi` and `SocketHttpClientApi`; do not maintain two subtly different implementations.
5. Move `HttpServerUrl_Connect`, `HttpServerUrl_Request`, and `HttpServerUrl_Response` unchanged from `Windows/NetworkProtocol.Windows.h` to `NetworkProtocolHttp.h`.
6. Make `NetworkProtocol.Windows.h` include `../NetworkProtocolHttp.h`, remove the three duplicated constants, and retain only Windows headers and Windows-specific declarations there.
7. Keep `NetworkProtocolHttp.h` free of Windows, Linux, and macOS implementation headers. Update existing Windows source and tests for the moved declarations without changing the public `windows_http::HttpRequest`, `HttpResponse`, and `HttpError` spellings.

Record the completed common-type dependency and any unavoidable request-layer extension in `.github/TaskLogs/Copilot_Investigate.md` before implementing the application APIs.

## Public API design gate

Implement the public surface from `TODO_SocketHttp_MiniHttpApi.md` and record the final signatures in `.github/TaskLogs/Copilot_Investigate.md` before filling in behavior. The following decisions are required:

- `SocketHttpRequestContext` has an implementation-only constructor, an out-of-line destructor, deleted copy/move operations, retains a completed `Ptr<async_tcp_socket::HttpRequest>`, exposes the decoded prefix-relative path and raw separated query, and is safe to retain after `OnHttpRequestReceived` returns.
- `SocketHttpRequestContext::Respond` accepts a caller-constructed `Ptr<async_tcp_socket::HttpResponse>` and an optional delivery callback. It succeeds only once and reports success only after the matching whole-message write completes.
- `SocketHttpRequestContext::Cancel` is one-shot, closes the physical connection, and races safely with `Respond`, disconnect, application stop, and shared-server stop.
- `SocketHttpServerApi(const WString& urlPrefix, bool respondToOptions)` exposes `Start`, idempotent hard-drain `Stop`, `IsStopped`, `GetUrlPrefix`, virtual `OnHttpRequestReceived`, and virtual `OnHttpServerStopping`.
- `SocketHttpClientApi(Ptr<IAsyncSocketClient> client, const WString& authority)` constructs its own `HttpRequestClient` and exposes `WaitForServer`, `GetStatus`, `HttpQuery`, idempotent hard-drain `Stop`, and shared URL encode/decode helpers.
- `HttpQuery` takes `windows_http::HttpRequest` and returns `Variant<windows_http::HttpResponse, windows_http::HttpError>` through a callback, matching the reusable Windows client value shape.
- Copy and move are deleted for both API objects. `SocketHttpClientApi` performs its hard stop in its destructor; `SocketHttpServerApi` relies on the explicit most-derived destructor rule below and uses its base destructor only as a non-virtual safety net.

Every most-derived `SocketHttpServerApi` destructor must call `Stop` before destroying state visible to request, completion, or stopping callbacks. The base destructor is only a final safety net and must not attempt virtual dispatch after the derived object has begun destruction.

Byte-oriented async-socket `HttpRequest` and `HttpResponse` objects remain the authoritative server representation. Optional UTF-8 helpers may be added for parity with the Windows API, but they must not hide invalid UTF-8 or replace the byte APIs.

## Shared server registry and lifecycle

Use a process-wide registry keyed by port. Each entry owns one private subclass of `HttpRequestServer`, the platform-native `IAsyncSocketServer`, the per-connection request dispatchers, and active normalized-prefix registrations.

- The common public header stays platform-neutral. In the private server API implementation, use platform guards to create `windows_socket::AsyncSocketServer`, `linux_socket::AsyncSocketServer`, or `macos_socket::AsyncSocketServer` for the parsed port.
- Protect the registry pointer/map and entry reference changes with one `SpinLock`. Create, bind, start, stop, drain, and invoke callbacks only after releasing it.
- Keep entries and the registry itself reference-counted so they disappear automatically when no server API or in-flight registry operation retains them.
- `SocketHttpServerApi::Start` registers its normalized prefix. The first starter on a port installs a reference-counted in-progress entry; later starters retain it and wait for its completion outside the global lock.
- Listener creation and `HttpRequestServer::Start` happen outside the global lock. Signal the in-progress entry after publishing success or failure. Waiting for another in-process creator does not consume a bind attempt. After address-in-use, recheck and join a published winner or retry through a fresh in-progress generation, for at most five actual bind attempts. If the port belongs to an unrelated process, propagate failure after the fifth attempt.
- If the existing async socket layer cannot distinguish address-in-use from another startup failure well enough to apply this policy, add the smallest platform-neutral failure seam and test it. Do not parse platform-specific exception text in common policy.
- Reject a duplicate active normalized prefix deterministically. Allow nested prefixes and select the longest boundary match for each completed request.
- One connection callback owns each accepted `IHttpRequestConnection`, but it looks up the target API again for every `OnReadRequest`. Sequential requests on one persistent connection may alternate between prefixes.
- Stopping one API makes its prefix unavailable, cancels and drains only contexts dispatched to it, and leaves other prefixes/listener connections active.
- The last API removes the registry entry while locked, then calls the shared `HttpRequestServer::Stop` and drains outside the lock. A native shared-listener failure transitions every attached API to stopped.
- Do not invoke request, response-completion, cancellation, disconnect, or stopping callbacks while holding registry, entry, prefix, connection, or context locks.

Constructor parsing must not bind the port. Construction validates and stores the normalized prefix; `Start` owns registry attachment and listener startup. `Start` is one-shot per API, and an API cannot restart after `Stop`.

## Prefix validation and dispatch

- Require `http://`, a loopback host, an explicit port in `1..65535`, and an optional absolute path. Reject HTTPS, credentials, query, fragment, malformed percent escapes, invalid UTF-8, NUL, encoded `/` or `\`, and unsupported authority forms.
- Normalize away a trailing `/`. Repeated trailing slashes normalize to none, so `http://localhost:38900/Demo/` and `http://localhost:38900/Demo` are duplicates. A slash-only path normalizes to the origin root.
- Validate exactly one HTTP/1.1 `Host` field and match the normalized authority before path dispatch.
- Percent-decode the prefix and request paths as UTF-8 before matching. Exclude the query from prefix matching, preserve the exact raw target in the underlying request, expose the decoded relative path, and expose the raw query substring without `?`.
- Match paths case-sensitively and on a segment boundary. `/Demo` matches `/Demo` and `/Demo/...`, but not `/Demo2` or `/Demo-suffix`.
- The root prefix is a fallback for all paths on its authority. When multiple active prefixes match, choose the longest.
- An exact prefix request exposes relative path `/`; a descendant exposes the unmatched suffix beginning with `/`.
- A valid request matching no active prefix receives `404 Not Found`. Deliver globally implemented methods to the selected API; that API must construct `405 Method Not Allowed` and `Allow` when it rejects an operation. Reject a method not implemented anywhere with `501 Not Implemented` before application dispatch.

## Required request-layer compatibility work

Reuse the completed byte parser and serializer. Extend it only where composition cannot implement the required semantics:

1. Add a structured terminal server-request failure seam for malformed start lines, unsupported versions/framing, size limits, and incomplete receive timeout, permitting one final `400`, `408`, `413`, `414`, `431`, `501`, or `505` response followed by close when writing is still safe.
2. Permit unsupported `Expect` to be rejected with `417 Expectation Failed` after the complete header section and before waiting indefinitely for a declared body. Interim `100 Continue` remains out of scope.
3. Associate client responses with the request method so a `HEAD` response ends after fields even when its representation `Content-Length` is nonzero.
4. Serialize and parse legal bodyless `204` and `304` responses without waiting for body bytes.
5. Allow a per-exchange client response deadline: bounded for ordinary calls and explicitly infinite for a future long poll. A fully received server request no longer uses the incomplete-message timer while its response context is retained.
6. Add the smallest platform-neutral terminal-listener notification seam. The shared MiniHttp entry must learn when a native `HttpRequestServer` stops unexpectedly, mark all attached APIs stopped, cancel their contexts, and drain callbacks; polling `IsStopped` does not establish that boundary.

Keep all additions platform-neutral and do not introduce another HTTP parser.

## Request, response, and connection policy

- Implement `GET`, `HEAD`, `POST`, and `OPTIONS`. Accept unknown browser extension fields unless they affect framing, connection, content, CORS, or dispatch.
- Treat bodies as bytes. Flatten transfer-decoded client response chunks into `windows_http::HttpResponse::body` and preserve the existing Windows-visible status/body/cookie/content-type result shape.
- The reused `windows_http::HttpRequest` fields for method, query, body, content type, accept types, cookie/extra fields, and response timeout are supported. Reject `secure = true`, nonempty credentials, and `keepAliveOnStop = true`; Socket client `Stop` remains a hard cancellation boundary. Resolve/connect/send phase timeout fields do not control an already-created socket client; document that only the response deadline maps to an exchange.
- Generate exactly one `Host` field from the constructor authority. Reject a caller-supplied conflicting `Host` and send `Accept-Encoding: identity`.
- Serialize client queries in FIFO order on the one `HttpRequestClient` connection. Support callback-reentrant submission after the prior response is reserved for delivery. `Stop` rejects new work and completes every accepted callback at most once.
- HTTP error statuses are successful `HttpResponse` values. Reserve `HttpError` for connection, framing, timeout, cancellation, unsupported coding, and local programming errors.
- Server callers construct raw response status, reason, ordered fields, body chunks, and content metadata. Flatten data chunks into one fixed-length body; reject trailers, application `Transfer-Encoding`, and contradictory `Content-Length`. Do not provide any file or MIME mapping.
- `HEAD` sends representation fields but no body. `204` and `304` send no body. Add `Date`, `Cache-Control: no-store`, and `Access-Control-Allow-Origin: *` unless the response already provides an equivalent valid value.
- Keep physical connections persistent for sequential exchanges, with no pipelining. Honor `Connection: close` after the current response. Accept multiple physical connections concurrently so one retained response does not block unrelated work.
- Catch application exceptions. Send `500 Internal Server Error` only if the response context is still pending; never send a second response after application code completed it.

## CORS, `OPTIONS`, and errors

When `respondToOptions` is true, process preflight before the user callback. A supported preflight returns `200` with `Content-Length: 0` or a legal bodyless `204`, plus wildcard non-credentialed CORS, `Allow`, supported methods, and `Accept, Content-Type` as supported request fields. Unsupported preflight methods or fields receive non-2xx. Origin-form `OPTIONS` follows the selected API's setting. `OPTIONS *` has no selected prefix: answer it automatically when any active API for the matching authority has `respondToOptions = true`, and otherwise return `501 Not Implemented`.

Return parseable errors with the status policy in `TODO_SocketHttp_MiniHttpApi.md`, CORS after dispatch, and `Connection: close` whenever the stream is unsafe to reuse. If a peer disconnect, fatal parse failure, or failed socket write prevents a response, close and report failed delivery rather than pretending success.

## Cross-platform shared tests

Create a new `Test/Source/TestInterProcess_AsyncSocket_MiniHttpApi.cpp`; this file does not exist yet, and the coverage must not be folded into or renamed from an existing test file. Follow the pattern in `TestInterProcess_AsyncSocket.cpp` and `TestInterProcess_HttpRequest.cpp`: define the complete platform-neutral MiniHttp scenarios exactly once in a shared runner such as `RunMiniHttpApiTestCases<TNativeServer, TNativeClient>()`, parameterized by both the native `IAsyncSocketServer` and `IAsyncSocketClient` implementation types. Only the guarded platform header includes and the invocation of that shared runner may vary by platform.

| Guard | Native server | Native client |
| --- | --- | --- |
| `VCZH_MSVC` | `windows_socket::AsyncSocketServer` | `windows_socket::AsyncSocketClient` |
| `VCZH_GCC && VCZH_APPLE` | `macos_socket::AsyncSocketServer` | `macos_socket::AsyncSocketClient` |
| `VCZH_GCC && !VCZH_APPLE` | `linux_socket::AsyncSocketServer` | `linux_socket::AsyncSocketClient` |

Under each guard, invoke the same shared test cases with the matching server/client pair. Do not copy, omit, or alter behavioral cases by platform. Because the public `SocketHttpServerApi` intentionally constructs its native server internally, route the private test listener-factory seam through the shared harness's `TNativeServer`; do not add a public server-injection constructor. Construct `SocketHttpClientApi` with `Ptr<IAsyncSocketClient>(new TNativeClient(port))`. Windows-only HTTP.sys/WinHTTP interoperability cases remain additional separately guarded tests, not replacements for any shared case.

The principal shared integration scenario uses port `38900`:

- Derive at least two small `SocketHttpServerApi` test classes with different prefixes on the same port, and create one `SocketHttpClientApi` from `Ptr<IAsyncSocketClient>(new TNativeClient(38900))` plus authority `localhost:38900`.
- Verify trailing-slash normalization, exact prefix matches, descendant matches, segment-boundary rejection, root fallback, nested longest-prefix selection, duplicate normalized-prefix rejection, and `404` for an unmatched prefix.
- Alternate requests between different prefixes on one persistent client connection. This proves dispatch occurs per request and that both APIs share one listener.
- Stop one disjoint-prefix API and prove the other remains available. Stop the final API and prove the shared server drains and releases the port.
- Exercise concurrent startup on one port and the in-progress handoff/bind-race join path through a deterministic barrier-controlled private listener-factory seam; do not rely on scheduler timing or sleeps. Also cover a genuinely occupied port failing after the bounded policy.
- Cover immediate and externally deferred responses, actual write-completion notification, cancellation, disconnect/stop races, multiple response chunks flattened to fixed length, trailer/`Transfer-Encoding` rejection, and no callback after hard stop.
- Complete sequential binary `POST`, `GET`, `HEAD`, ordinary `OPTIONS`, and CORS preflight exchanges. Verify automatic `Host`, identity encoding, fixed framing, exact raw bodies, CORS, `Date`, cache policy, and connection reuse.
- Cover `400`, `404`, `405`, `408`, `413`, `414`, `415`, `417`, `431`, `500`, `501`, and `505` through native exchanges or a deterministic fake seam when the public client cannot construct malformed input.
- Verify FIFO client submissions, callback-reentrant submission, rejection of `keepAliveOnStop = true`, and most-derived server destruction after explicit `Stop`. The test must not construct `HttpRequestClient`; it proves `SocketHttpClientApi` constructs and owns it internally.
- Add focused portability coverage for moved `windows_http::HttpRequest`, `HttpResponse`, `HttpError`, UTF-8 helpers, URL encode/decode, and common route constants.

Use manual-reset events, bounded waits, RAII cleanup, callback-thread failure recording, and no sleeps. Do not create a temporary document root or test a MIME table in this product task.

## Mandatory Windows HTTP interoperability

Mirror the established structure at the end of `TestInterProcess_HttpRequest.cpp`: that file runs its shared native socket scenario on every platform, then adds `VCZH_MSVC`-only cases for `windows_http::HttpClientApi` against `HttpRequestServer` and `HttpRequestClient` against `windows_http::HttpServerApi`. In the new `TestInterProcess_AsyncSocket_MiniHttpApi.cpp`, run the shared MiniHttp runner first and then add the following two tests under `VCZH_MSVC` in the same `TEST_FILE`. They are additional interoperability coverage and must not replace or weaken the Windows invocation of the shared runner.

These two directions independently check the new server and client against the Windows HTTP APIs implemented through WinHTTP and HTTP.sys.

### `windows_http::HttpClientApi` to `SocketHttpServerApi`

Use port `38901` and prefix `http://localhost:38901/vlppos-mini-http/`.

- Start a derived `SocketHttpServerApi` and send a supported `POST` through `windows_http::HttpClientApi(L"localhost", 38901)`.
- Verify normalized prefix dispatch, exact target/query, mixed-case field lookup, fixed body framing, content type, and body bytes in the socket request context.
- Respond with a non-default status, reason, content type, and binary body. Verify the Windows client-visible status/body/content type and clean shutdown.

### `SocketHttpClientApi` to `windows_http::HttpServerApi`

Use port `38902` and HTTP.sys prefix `http://localhost:38902/vlppos-mini-http/`.

- Start a small derived `windows_http::HttpServerApi`, then construct `SocketHttpClientApi` from `Ptr<IAsyncSocketClient>(new windows_socket::AsyncSocketClient(38902))` and `localhost:38902`.
- Send a fixed-length request below the prefix. Verify HTTP version, verb, target/query, exact `Host`, custom/content fields, body, and `Accept-Encoding: identity` in `OnHttpRequestReceived`.
- Reply through `windows_http::HttpServerApi::SendResponse`, then verify the reusable Windows-shaped `HttpResponse` result and exact raw body bytes.
- Signal from the HTTP.sys callback but call `HttpServerApi::Stop` on the test thread because it drains pending callbacks.

Fully qualify raw async-socket and `windows_http` request/response names where they collide.

## Source and project integration

- Create `TestInterProcess_AsyncSocket_MiniHttpApi.cpp` as a new source file and add it, `NetworkProtocolHttp.h/.cpp`, `AsyncSocket_HttpServerApi.h/.cpp`, and `AsyncSocket_HttpClientApi.h/.cpp` explicitly to `Test/UnitTest/UnitTest/UnitTest.vcxproj`; wildcard entries are forbidden.
- Put common HTTP protocol files under `Common\InterProcess`, Socket API files under `Common\InterProcess\AsyncSocket`, and the test under `Source Files\TestInterProcess` in `UnitTest.vcxproj.filters`.
- Update includes in Windows HTTP sources, `NetworkProtocol.Windows.h`, release-generation inputs, and tests for the common header. Do not hand-edit generated `Release` outputs.
- Common source entries flow into Unix builds through the MSBuild project. Do not edit generated `Test/Linux/vmake.txt` or `Test/Linux/makefile` by hand.
- Keep all MiniHttp product tests in the one requested test file. If a prerequisite raw-request signature changes, mechanically update existing HTTP request tests rather than creating another MiniHttp test file.

## Explicitly out of scope

- Built-in file access, document roots, file-to-content mapping, MIME suffix mapping, uploads, downloads, directory listings, or static-site policy.
- The standalone `MiniHttpServer` project and live Chrome/Firefox/Safari workflow; implement them only in `TODO_Task_MiniHttpApi_TestApp.md` after this task is complete.
- `INetworkProtocolServer`, `INetworkProtocolClient`, GUID logical connections, `/VlppInterProcess/Connect`, `/Request/{guid}`, `/Response/{guid}`, and the two-lane long-poll adapter.
- TLS/HTTPS, HTTP/2, HTTP/3, redirects, proxies, automatic credentials/authentication, multipart forms, WebSocket, Server-Sent Events, response compression, response chunk generation, trailers, ranges, conditional caching, and server-side scripting.
- Refactoring the async socket and HTTP request layers beyond the minimal structured errors, response semantics, deadline, bind-failure, and shared-helper seams required above.

## Verification and acceptance

Follow `.github/copilot-instructions.md`, `Project.md`, and all referenced coding, multithreading, source-file, build, unit-test, CLI, and native-dialog guidance.

- Establish a clean Debug x64 baseline before product changes.
- Build `Test/UnitTest/UnitTest.sln` through `.github/Scripts/copilotBuild.ps1` for Debug/Release and Win32/x64. Require zero errors and resolve new warnings.
- Run only `TestInterProcess_AsyncSocket_MiniHttpApi.cpp` in Debug x64 through `.github/Scripts/copilotExecute.ps1`, then run the complete Debug x64 UnitTest suite.
- Inspect `Build.log` and `Execute.log` for passing summaries and no Debug CRT memory-leak dump.
- On each platform, execute the same shared MiniHttp harness with that platform's native `AsyncSocketServer`/`AsyncSocketClient` pair. Also execute both Windows interoperability directions on Windows. Report only platforms actually run.
- Perform a textual cross-platform audit of common headers, platform guards, registry synchronization, prefix parsing, callback lifetime, and common UTF-8/query helpers.
- Record the final public API, registry/bind-race design, shared listener evidence, client ownership, common HTTP refactor, request-layer extensions, response-context lifecycle, and all verification evidence in `.github/TaskLogs/Copilot_Investigate.md`.
- Remove temporary diagnostics. Commit only intentional implementation, tests, project metadata, and investigation changes, then push the current branch.

Acceptance requires the exact `SocketHttp(Server|Client)Api` names, Windows-like prefix construction, same-port shared listener behavior, client-owned `HttpRequestClient`, the common HTTP refactor, no filesystem API, one new shared MiniHttp test file running identical cases with the native server/client pair on Windows, Linux, and macOS, complete Windows interoperability coverage, all four Windows builds clean, the full Debug x64 suite green, and no memory leaks.
