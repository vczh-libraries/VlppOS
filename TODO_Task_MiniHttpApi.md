investigate repro

# Minimized HTTP API on async sockets

Implement the cross-platform minimized HTTP application layer described by [TODO_SocketHttp_MiniHttpApi.md](./TODO_SocketHttp_MiniHttpApi.md) on top of the completed `HttpRequestServer`, `HttpRequestClient`, and `IHttpRequestConnection` layer in `Source/InterProcess/AsyncSocket`.

The public server/client pair is `SocketHttpServer` and `SocketHttpClient` in `vl::inter_process::async_tcp_socket`. This task covers a small loopback HTTP/1.1 origin server and matching native client: REST-style route dispatch, deferred responses, static files, CORS, browser-compatible method/error policy, and byte-oriented request/response convenience APIs. It does not implement the later `INetworkProtocol*` compatibility adapter from [TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md](./TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md).

## Authoritative design and corrections

Treat `TODO_SocketHttp_MiniHttpApi.md` as authoritative for externally observable MiniHttp behavior, with the following corrections and clarifications:

- Keep all common product types in `vl::inter_process::async_tcp_socket` and all common headers free of Windows, Linux, or macOS implementation headers.
- Keep the source document's public names `SocketHttpServer` and `SocketHttpClient`. “MiniHttp API” in this task refers to these two classes and their supporting route, request, response, and response-context types.
- Use `AsyncSocket_HttpServerApi.h/.cpp` for `SocketHttpServer`, routing, response contexts, static hosting, and server-side helpers. Use `AsyncSocket_HttpClientApi.h/.cpp` for `SocketHttpClient` and client-side helpers. Shared private implementation may be factored without duplicating policy, but do not create platform-specific product variants.
- `SocketHttpServer` takes a `Ptr<IAsyncSocketServer>` in its constructor. It constructs its own `HttpRequestServer` bridge internally and owns that bridge as a direct value field, preferably inside its `Impl`. The bridge can be a private forwarding subclass because `HttpRequestServer::OnClientConnected` is virtual. Do not expose inheritance from `HttpRequestServer` as the MiniHttp public API.
- `SocketHttpClient` takes a `Ptr<IAsyncSocketClient>` in its constructor and constructs its own `HttpRequestClient` internally. Own the `HttpRequestClient` as a direct value field, preferably inside its `Impl`; do not accept a preconstructed `HttpRequestClient`.
- `IAsyncSocketClient` does not reveal its host or port, but HTTP/1.1 requires one `Host` authority. Give `SocketHttpClient` an explicit immutable authority alongside the supplied client, preferably a constructor such as `SocketHttpClient(Ptr<IAsyncSocketClient> client, const WString& authority)`. An equally explicit per-request authority is acceptable only if the investigation explains why it is safer. Never guess the authority from a platform implementation.
- Assume as constructor preconditions that `IAsyncSocketServer::Start` and `IAsyncSocketClient::WaitForServer` have not already been called on the supplied objects. The MiniHttp wrappers own the subsequent start/wait/stop lifecycle through their internal HTTP request wrappers.
- Constructor injection supersedes the old proposal for a process-global port map, spin lock, same-port server sharing, five bind retries, and a nonexistent `IHttpRequestServer`. Do not implement that registry or retry policy. The caller creates and supplies the native socket object and therefore controls port selection and bind failure.
- Reuse `HttpRequest`, `HttpResponse`, ordered `HttpField` values, framing, buffering, timeout, sequential-exchange, and `Connection: close` behavior from the completed request layer. Do not parse or serialize HTTP a second time in the MiniHttp layer.
- Browser automation and the standalone two-port browser test application are not part of this task. Exercise browser-relevant semantics through common socket tests and the two mandatory Windows HTTP API interoperability tests.
- Do not modify the existing Windows `windows_http::HttpClientApi`, `windows_http::HttpServerApi`, `windows_http::HttpClient`, or `windows_http::HttpServer` except where a test consumes their existing public APIs.

## Public API design gate

Before implementation, record the exact public signatures in `.github/TaskLogs/Copilot_Investigate.md`. The surface must be small and must include the following capabilities even if supporting type names differ:

- `SocketHttpServer` construction from `Ptr<IAsyncSocketServer>`, `Start`, idempotent hard-drain `Stop`, and `IsStopped`.
- Route registration before `Start`, including a path matcher capable of supporting exact routes and dynamic or prefix routes, an explicit allowed-method set, and a callback. Exact matches take precedence over prefix/dynamic matches. Registration and document-root mutation after `Start` may be rejected synchronously instead of supporting concurrent route-table mutation.
- A configured document root for optional static-file fallback.
- A completed MiniHttp request view that retains the original `Ptr<HttpRequest>`, exact raw target, separated raw path/query or asterisk target, decoded route path, validated authority, ordered fields, and flattened byte body. Keep the raw request accessible when an application needs exact field/chunk information.
- A one-shot response context delivered to route callbacks. It retains the request and physical HTTP connection and has an atomic lifecycle equivalent to `Pending -> Sending -> Completed` or `Pending/Sending -> Cancelled`.
- The response context can respond immediately, be retained and completed later, or be cancelled. Exactly one terminal operation wins. Cancelling without a response closes the physical connection.
- The response context exposes delivery completion or failure, not merely submission. Successful completion is tied to the matching `IHttpRequestCallback::OnWriteCompleted`; error, disconnect, cancellation, or server stop reports failure. This is needed by the later long-poll adapter to requeue a message whose response was not delivered.
- `SocketHttpClient` construction from `Ptr<IAsyncSocketClient>` plus explicit authority, `WaitForServer`, `GetStatus`, idempotent hard-drain `Stop`, and one request operation with a completion callback or callback interface.
- Client requests support `GET`, `HEAD`, `POST`, and `OPTIONS`, ordered extra fields, content type, and arbitrary byte bodies. Client results expose status, reason, ordered fields, and exact flattened response bytes.
- Preserve the underlying one-exchange-at-a-time contract. Reject overlapping client submissions synchronously, or serialize them with a documented bounded queue. A callback-reentrant next sequential request must be supported after the previous response is reserved for delivery.
- Byte APIs are primary. UTF-8 text and JSON conversion helpers are conveniences and must report invalid UTF-8 or unsupported charset instead of guessing.

Do not leave route matching, response-context completion, client error delivery, authority configuration, or document-root ownership implicit. These are public compatibility decisions and must be settled in the proposal before coding.

## Required request-layer compatibility work

Composition must remain the architecture, but the current `HttpRequestConnection` contract lacks several HTTP semantic seams required by the MiniHttp source design. Extend the existing request layer only where necessary; keep the changes platform-neutral and reuse its single byte parser/serializer.

1. **Structured terminal request failures.** Today malformed start lines, unsupported versions/framing, raw size limits, and receive timeout collapse to fatal `OnError` followed by disconnect, so a composed server has no opportunity to send `400`, `408`, `413`, `414`, `431`, `501`, or `505`. Add the smallest structured server-side failure seam that categorizes these cases and permits at most one final error response followed by close when the stream is still safe to write. Do not duplicate or weaken the raw parser. If the peer is already gone or the write fails, close without pretending a response was delivered.
2. **Header-complete policy for `Expect`.** An unsupported `Expect` value must produce `417 Expectation Failed` and close without deadlocking while the client waits for `100 Continue`. Let the server reject it after a complete header section and before waiting indefinitely for the declared body. Interim `100 Continue` generation remains out of scope.
3. **`HEAD` response semantics.** Associate each client response with its request method. A `HEAD` response ends after its fields even when its representation `Content-Length` is nonzero. Server serialization emits the same representation fields as the corresponding `GET` without body octets and without requiring the supplied body length to match transmitted bytes.
4. **Bodyless final statuses.** Correctly serialize and parse final `204` and `304` responses without a message body. A `204` must not require `Content-Length` or `Transfer-Encoding`; a `304` may carry representation metadata but no body. The MiniHttp server may use `200` with `Content-Length: 0` for preflight, but its client and response API must still handle legal bodyless statuses.
5. **Per-exchange response deadline.** After a request has been completely received, the raw header/body timeout is cancelled. Allow the MiniHttp client to choose an ordinary bounded response deadline or an explicitly disabled/infinite deadline for a deferred long poll. Do not put transport timer objects in `HttpRequest`, and keep the default bounded behavior for ordinary requests.

Put all new behavior coverage for these extensions in `TestInterProcess_AsyncSocket_MiniHttpApi.cpp`. Existing HTTP request tests may be mechanically adjusted if a prerequisite signature changes, but do not create another MiniHttp test file.

## Request validation and dispatch

`SocketHttpServer` is a plain HTTP/1.1 loopback origin server, not a general Internet-facing server.

- `GET`, `HEAD`, `POST`, and `OPTIONS` are the complete implemented method set. A method in this set that is unavailable on a matched target receives `405 Method Not Allowed` with the target-specific `Allow` field. A method not implemented anywhere, including `PUT`, `DELETE`, `PATCH`, `CONNECT`, and `TRACE`, receives `501 Not Implemented`.
- Require exactly one syntactically valid, nonempty `Host` field on every HTTP/1.1 request. Reject a missing, duplicate, comma-combined, or invalid authority with `400 Bad Request` and close. Unknown browser-generated fields are accepted and ignored unless they affect framing, connection, content, CORS, or dispatch policy.
- Use one authority spelling consistently in tests. Use `localhost:<port>` when interoperating with HTTP.sys, because `localhost`, `127.0.0.1`, and `[::1]` are different browser origins.
- Accept origin-form targets for normal routes and the asterisk form only for `OPTIONS *`. Preserve the exact percent-encoded target. Split the query without treating it as a file name. Reject fragments, invalid target syntax, or other unsupported target forms with `400`.
- API route matching happens before static-file fallback. Preserve both raw and decoded forms for applications. A route callback can respond synchronously, retain its context, or cancel it.
- Generic fields and bodies remain bytes. Field-name lookup is ASCII case-insensitive while repeated-field order remains available through the raw request.
- Media-type and charset parsing is ASCII case-insensitive and accepts parameters in any order. The exact legacy spelling `application/json; charset=utf8` is only a future GacUI compatibility requirement; the generic MiniHttp API accepts equivalent valid UTF-8 declarations.
- Reject unsupported request media type or charset and any request `Content-Encoding` with `415 Unsupported Media Type`. Ignore `Accept-Encoding` and produce identity responses.
- Invoke no route, response-completion, disconnect, or cancellation callback while holding a connection, route-map, response-context, or retained-context lock.
- Catch application exceptions. If the context is still pending, generate `500 Internal Server Error`; if the application already completed it, preserve the completed response and report the exception through the repository's testable error path without sending a second response.

## Response policy and connection reuse

- Produce exactly one final response for every request delivered to MiniHttp dispatch unless application code explicitly cancels the context.
- Responses that permit content use fixed `Content-Length`; response chunking and trailers are not generated by this minimized server. A nonempty response also has `Content-Type`.
- `HEAD` returns the same status and representation fields as `GET` but transmits no body. `204` and `304` transmit no body. Do not generate a body for any bodyless response even if application code supplied one; reject that programming error or normalize it consistently before sending.
- Add an IMF-fixdate `Date` field and `Cache-Control: no-store` to dynamic and static responses unless an explicitly documented application policy supplies an equivalent value.
- Add `Access-Control-Allow-Origin: *` to every ordinary response that reached MiniHttp dispatch, including `404`, `405`, `415`, and `500`.
- Keep HTTP/1.1 physical connections persistent for sequential exchanges. There is one active request/response exchange per connection and no pipelining. Accept multiple physical connections concurrently so one deferred response does not block unrelated browser requests.
- Honor `Connection: close` from either side only after the current response boundary. Every error response after which the server will close includes `Connection: close`.
- A disconnect or `SocketHttpServer::Stop` cancels every pending/sending response context, drains its completion/cancellation callbacks, stops accepted HTTP connections, and only then releases route/application state and the directly owned `HttpRequestServer` bridge.
- `SocketHttpClient::Stop` prevents new requests, stops and drains its directly owned `HttpRequestClient`, and delivers at most one terminal result to every accepted request callback.

## Static website hosting

Support one document-root folder configured before server start.

- `GET /` and `HEAD /` select `index.html`. A target ending in `/` may select `index.html` below that directory. Other targets select a regular file below the root; the query is never part of the file name.
- Percent-decode the path as UTF-8, translate URL `/` separators to platform paths, normalize it, and require the result to remain below the configured root. Reject invalid UTF-8, NUL, encoded `/` or `\`, and any `..` traversal or normalized escape.
- Serve the file's exact bytes without round-tripping through `WString`.
- API routes take precedence over static files. Missing files and directories without an index return `404`; never generate a directory listing.
- Static `GET` returns `200`, exact `Content-Length`, `Content-Type`, `Date`, `Cache-Control: no-store`, and CORS. Static `HEAD` returns the same representation fields without file bytes.
- Concurrent asset requests are independent physical connections and may complete concurrently.

Use at least this case-insensitive suffix mapping:

| Suffix | Content-Type |
| --- | --- |
| `.html`, `.htm` | `text/html; charset=utf-8` |
| `.css` | `text/css; charset=utf-8` |
| `.js`, `.mjs` | `text/javascript; charset=utf-8` |
| `.json`, `.map` | `application/json` |
| `.txt` | `text/plain; charset=utf-8` |
| `.svg` | `image/svg+xml` |
| `.png` | `image/png` |
| `.jpg`, `.jpeg` | `image/jpeg` |
| `.gif` | `image/gif` |
| `.webp` | `image/webp` |
| `.ico` | `image/vnd.microsoft.icon` |
| `.woff` | `font/woff` |
| `.woff2` | `font/woff2` |
| `.ttf` | `font/ttf` |
| `.wasm` | `application/wasm` |
| anything else | `application/octet-stream` |

JSON and map files contain UTF-8 bytes but do not need a charset parameter. Correct JavaScript and CSS media types are mandatory; do not rely on browser MIME sniffing.

## CORS and `OPTIONS`

Use one deliberately permissive, non-credentialed CORS policy for the loopback service.

- Handle preflight before ordinary route dispatch. A preflight contains `Origin` and `Access-Control-Request-Method`, with optional `Access-Control-Request-Headers`.
- A supported preflight returns either `200` with `Content-Length: 0` or legal bodyless `204`, plus:
  - `Access-Control-Allow-Origin: *`
  - `Access-Control-Allow-Methods: GET, HEAD, POST, OPTIONS`
  - `Access-Control-Allow-Headers: Accept, Content-Type`
  - `Allow: GET, HEAD, POST, OPTIONS`
- A route-aware dispatcher may narrow both method lists. If the requested method or any requested field is unsupported for that target, return non-2xx so the browser does not send the actual request.
- Do not send `Access-Control-Allow-Credentials`. Do not implement credentials, `Authorization`, arbitrary application preflight fields, `Access-Control-Max-Age`, or `Access-Control-Expose-Headers`.
- An `OPTIONS` request without `Access-Control-Request-Method` is ordinary HTTP, not preflight. `OPTIONS *` returns the server-wide `Allow` list. Origin-form `OPTIONS` returns the target-specific list. Both responses are empty.
- CORS does not cover Chrome Local Network Access or public-site-to-loopback permission policy; those deployment rules and browser UI are outside this task.

## Error responses

Return an HTTP response whenever the request has been parsed far enough to write one safely. Error bodies default to UTF-8 `text/plain; charset=utf-8`, include CORS after dispatch, and include `Connection: close` whenever the stream will not be reused.

| Condition | Response |
| --- | --- |
| Malformed request line, fields, framing, or target | `400 Bad Request`, then close |
| Missing, duplicate, or invalid `Host` | `400 Bad Request`, then close |
| Incomplete-request timeout | `408 Request Timeout`, then close |
| Body exceeds its limit | `413 Content Too Large`, then close when unread bytes remain |
| Target exceeds its limit | `414 URI Too Long`, then close |
| Header section exceeds its limit | `431 Request Header Fields Too Large`, then close |
| No API route or static file | `404 Not Found` |
| Implemented method not allowed on the target | `405 Method Not Allowed` with target-specific `Allow` |
| Method or transfer coding not implemented anywhere | `501 Not Implemented`, then close for unsafe framing |
| Unsupported media type, charset, or content coding | `415 Unsupported Media Type` |
| Unsupported `Expect` value | `417 Expectation Failed`, then close |
| Unsupported HTTP version | `505 HTTP Version Not Supported`, then close |
| Application failure before response completion | `500 Internal Server Error` |

If a fatal parser error, peer disconnect, or failed socket write makes a response impossible, close and report delivery failure rather than invoking a success callback. Never reuse a stream after abandoning unread request bytes.

## MiniHttp client behavior

- Construct `GET`, `HEAD`, `POST`, and `OPTIONS` requests through the MiniHttp API rather than asking callers to assemble raw framing.
- Generate exactly one `Host` field from the configured authority. Reject caller attempts to add a conflicting second `Host`.
- Generate fixed `Content-Length` whenever body size is known, including `0` for an explicitly bodyless `POST` or `OPTIONS` when useful for interoperability.
- Send `Accept-Encoding: identity`. A non-identity response `Content-Encoding` is an unsupported response error; do not expose coded bytes as if they were decoded content.
- Accept the legal fixed, chunked, `HEAD`, `204`, and `304` response forms supplied by the request layer. Flatten decoded transfer chunks into exact response bytes while retaining ordered raw fields for callers that need them.
- HTTP error statuses are successful HTTP results, not transport errors. Deliver status, reason, fields, and body to the request callback. Reserve the client error path for connection, framing, timeout, cancellation, unsupported coding, and local programming errors.
- Redirects, cookies, authentication, automatic content decoding, TLS, proxies, and retry policy remain application features and are not automatic MiniHttp client behavior.
- An ordinary request uses a bounded response timeout. The caller can explicitly disable that response deadline for a deferred long poll. Server-side deferred contexts have no automatic route deadline; the route owns that policy.

## Deferred response and shutdown tests

The later GacUI long-poll adapter depends on lifecycle behavior even though its GUID routes and `INetworkProtocol*` objects are out of scope here.

- Retaining a response context after the route callback returns must keep the request and physical connection alive without keeping the public server owner accessible after hard stop.
- A response completed from another thread sends exactly once and reports completion only after the HTTP message write completes.
- Racing `Respond`, `Cancel`, disconnect, and `Stop` produces one terminal result and no callback after the hard-drain boundary.
- Cancelling a pending context without sending stops that physical connection so the peer cannot wait forever or reuse a request whose response was abandoned.
- A deferred server response is not subject to the raw incomplete-request timeout, because the request has already been fully received.
- The MiniHttp client can opt out of its response deadline for a long poll and still cancel it promptly through `Stop`.

## Cross-platform shared tests

Create all MiniHttp coverage in exactly `Test/Source/TestInterProcess_AsyncSocket_MiniHttpApi.cpp`. Follow `TestInterProcess_HttpRequest.cpp`: define each common scenario once in platform-neutral helpers and bind only the concrete socket types under existing platform guards.

| Guard | Native server/client |
| --- | --- |
| `VCZH_MSVC` | `windows_socket::AsyncSocketServer` / `windows_socket::AsyncSocketClient` |
| `VCZH_GCC && VCZH_APPLE` | `macos_socket::AsyncSocketServer` / `macos_socket::AsyncSocketClient` |
| `VCZH_GCC && !VCZH_APPLE` | `linux_socket::AsyncSocketServer` / `linux_socket::AsyncSocketClient` |

The principal shared integration scenario is MiniHttp server versus MiniHttp client. Instantiate the same helper for all three rows; do not copy its body. On port `38900`:

- Construct `SocketHttpServer` directly from `Ptr<IAsyncSocketServer>(new TNativeServer(38900))` and `SocketHttpClient` directly from `Ptr<IAsyncSocketClient>(new TNativeClient(38900))` plus `localhost:38900`. The test must not construct `HttpRequestServer` or `HttpRequestClient`; it proves the MiniHttp wrappers create and own them internally.
- Start the listener before queuing the blocking client wait. Use manual-reset events, bounded waits, RAII cleanup signals, callback-thread failure recording, and no sleeps.
- Complete multiple sequential exchanges on one persistent connection, including a nonempty binary `POST` with an exact encoded target/query, a normal `GET`, `HEAD`, ordinary `OPTIONS`, and a CORS preflight.
- Assert automatic `Host`, fixed body framing, `Accept-Encoding: identity`, raw and decoded target information, ignored browser extension fields, exact binary bodies, status/reason, ordered response fields, CORS, `Date`, `Cache-Control`, whole-message completion ordering, and connection reuse.
- Exercise immediate and externally deferred responses. Verify completion after the matching write, response-context cancellation, disconnect/stop races, and no callback after stop returns.
- Exercise API-route precedence, exact versus prefix/dynamic route priority, method-specific `Allow`, `404`, `405`, `415`, `417`, and `500` responses. Use deterministic raw/fake socket seams in the same test file for malformed/oversized/timeout cases that the Mini client cannot generate.
- Create a temporary document root under the unit-test output folder. Cover `/` index selection, a nested JSON/map asset, a binary image/font-style asset, query removal, `HEAD`, the complete MIME lookup table, API precedence, missing files, missing directory index, invalid UTF-8, encoded separators, NUL, and traversal/root escape.
- Verify `Connection: close` on a final dedicated exchange, because that connection cannot be reused afterward.

Use a small deterministic fake connection/server only where native segmentation or the public Mini client cannot reproduce a boundary, malformed request, response-context race, or raw-error category. Keep those tests platform-neutral and in the same file.

## Mandatory Windows HTTP interoperability

MiniHttp server versus MiniHttp client is necessary but insufficient because both new layers could agree on the same application-policy mistake. Add two extra tests under `VCZH_MSVC`, following the completed request/response interoperability pattern.

### `windows_http::HttpClientApi` to `SocketHttpServer`

Use port `38901`.

- Construct `SocketHttpServer` with `Ptr<IAsyncSocketServer>(new windows_socket::AsyncSocketServer(38901))`, register the test route, and start it first.
- Construct `windows_http::HttpClientApi(L"127.0.0.1", 38901)` and send a supported `POST` with an encoded target/query, mixed-case custom field, explicit content type, and nonempty body.
- In the MiniHttp route callback, assert the method, exact raw target, normalized lookup, generated body framing, and exact body. Do not assert the entire WinHTTP-generated field collection or its global order.
- Return a non-default fixed-length response with exact body and content type. Through `windows_http::HttpResponse`, assert status, flattened body, and content type; assert a cookie only if the MiniHttp public response API deliberately supports `Set-Cookie` as an ordinary field. `HttpClientApi` does not expose arbitrary response fields, so validate CORS/Date/cache fields through the shared Mini client test instead.
- Wait for accept and query completion, call `HttpClientApi::Stop`, then hard-stop `SocketHttpServer`.

### `SocketHttpClient` to `windows_http::HttpServerApi`

Use port `38902`.

- Derive a small test server from `windows_http::HttpServerApi` using the prefix `http://localhost:38902/vlppos-mini-http/` and start HTTP.sys before connecting the socket client.
- Construct `SocketHttpClient` with `Ptr<IAsyncSocketClient>(new windows_socket::AsyncSocketClient(38902))` and authority `localhost:38902`.
- Send a supported request under that exact prefix with a nonempty fixed-length body, content type, and custom field.
- In `OnHttpRequestReceived`, inspect HTTP/1.1, verb, raw target/query, exact `Host`, custom/content fields, `Content-Length`, exact body, and `Accept-Encoding: identity`.
- Reply with `HttpServerApi::SendResponse` using a non-default status/reason, UTF-8 content type, and nonempty body.
- Assert that `SocketHttpClient` exposes the native response status, reason, content type field, and exact raw body bytes. Do not require a framing form that HTTP.sys does not guarantee.
- Signal completion from the HTTP.sys callback but call `HttpServerApi::Stop` only from the test thread, because stop waits for pending callbacks.
- Fully qualify async-socket and `windows_http` request/response names wherever they collide.

These are exactly two extra Windows tests in addition to the one shared MiniHttp-to-MiniHttp scenario.

## Source and project integration

- Add `AsyncSocket_HttpServerApi.h/.cpp`, `AsyncSocket_HttpClientApi.h/.cpp`, and `TestInterProcess_AsyncSocket_MiniHttpApi.cpp` explicitly to `Test/UnitTest/UnitTest/UnitTest.vcxproj`; wildcard entries are forbidden.
- Put the four product files under `Common\InterProcess\AsyncSocket` and the test under `Source Files\TestInterProcess` in `UnitTest.vcxproj.filters`.
- If implementation analysis justifies an additional common product file, document it before creation and register it explicitly in both project files. Do not scatter MiniHttp policy across platform files.
- No solution change is expected. Common source entries flow into Unix builds from the MSBuild project.
- Do not edit generated `Test/Linux/vmake.txt`, `Test/Linux/makefile`, generated `Release` outputs, or `Import` dependencies by hand.
- If the local untracked `.vcxproj.user` filter skips the new test, add `/F:TestInterProcess_AsyncSocket_MiniHttpApi.cpp` locally for execution and do not commit that user file.
- Keep all new tests in `TestInterProcess_AsyncSocket_MiniHttpApi.cpp`, including deterministic helper, lifecycle, static-file, and prerequisite compatibility coverage.

## Explicitly out of scope

- The process-global same-port server registry and bind retry proposal.
- `INetworkProtocolServer`, `INetworkProtocolClient`, GUID logical connections, `/VlppInterProcess/Connect`, `/Request/{guid}`, `/Response/{guid}`, two-lane long-poll transport, and changes to existing channel adapters. Those belong to `TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md`.
- A live Chrome, Firefox, or Safari test harness, a two-port browser test application, Chrome Local Network Access permission UI, or public-site deployment.
- TLS/HTTPS, HTTP/2, HTTP/3, redirects, proxies, credentials, cookies as an automatic client feature, authentication, multipart forms, directory listings, WebSocket, Server-Sent Events, server-side scripting, response compression, response chunk generation, response trailers, or representation auto-decoding.
- Range responses, media seeking, `ETag`, `Last-Modified`, conditional request generation, and automatic `304`. Ignore `Range`, send the complete representation with `200`, use `Cache-Control: no-store`, and optionally advertise `Accept-Ranges: none`.
- Compression. Ignore request `Accept-Encoding` on the server and send identity bytes; the MiniHttp client requests identity and rejects a non-identity response coding.
- Refactoring or replacing the completed async-socket and HTTP request layers beyond the minimal common semantic seams listed in this task.

## Verification and acceptance

Follow `.github/copilot-instructions.md`, `Project.md`, and the referenced coding, multithreading, source-file, build, execution, unit-test, and native-dialog guidance.

- Establish a clean Debug x64 baseline before product changes.
- Build `Test/UnitTest/UnitTest.sln` through `.github/Scripts/copilotBuild.ps1` for Debug/Release and Win32/x64. All four configurations must report zero errors; resolve new warnings.
- Run only `TestInterProcess_AsyncSocket_MiniHttpApi.cpp` through `.github/Scripts/copilotExecute.ps1` in Debug x64, ensuring the local filter includes it when necessary.
- Run the complete Debug x64 UnitTest suite after the focused file passes.
- Inspect the ends of `.github/Scripts/Build.log` and `.github/Scripts/Execute.log`; require the expected passing summaries and no Debug CRT memory-leak dump after the test summary.
- Execute the shared scenario and both Windows interoperability directions on Windows. Acceptance requires all three to pass; MiniHttp server versus MiniHttp client alone is insufficient.
- Keep the same shared scenario bound under the Windows, Linux, and macOS guards, but do not run or claim Linux/macOS verification in this task. The user will run `TestInterProcess_AsyncSocket_MiniHttpApi.cpp` on Linux and macOS separately to detect platform issues.
- Perform a textual cross-platform audit of common headers, guards, file APIs, path normalization, time/date generation, and callback synchronization. Do not include Windows types or headers outside `VCZH_MSVC` test blocks.
- Record the final public API, direct-field ownership, prerequisite request-layer extensions, route/static/CORS policies, response-context lifecycle, and all Windows evidence in `.github/TaskLogs/Copilot_Investigate.md`.
- Remove all temporary diagnostics. Commit only intentional implementation, tests, project metadata, and investigation changes, then push the current branch.

Acceptance requires the complete source-design behavior retained by this task, the exact test file and five-way source topology requested above, direct internal ownership of `HttpRequestServer`/`HttpRequestClient`, all three Windows executions passing, all four Windows builds clean, the full Debug x64 suite green, and no memory leaks.
