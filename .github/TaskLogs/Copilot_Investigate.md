# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

investigate repro

# HTTP/1.1 request and response transmission on async sockets

Implement the cross-platform buffered HTTP/1.1 request/response layer described by [TODO_SocketHttp_HttpRequest.md](./TODO_SocketHttp_HttpRequest.md) on top of `IAsyncSocketServer`, `IAsyncSocketClient`, and `IAsyncSocketConnection` from `Source/InterProcess/AsyncSocket/AsyncSocket.h`.

The goal is a small HTTP wire codec and asynchronous request/response transport. A client sends an `HttpRequest`, a server receives and parses it, the server sends an `HttpResponse`, and the client receives and parses it. This task is not the minimized HTTP application/service layer from [TODO_SocketHttp_MiniHttpApi.md](./TODO_SocketHttp_MiniHttpApi.md).

## Authoritative design and corrections

Treat `TODO_SocketHttp_HttpRequest.md` as the authoritative data, framing, buffering, and callback contract, with these corrections and clarifications:

- Keep everything in `vl::inter_process::async_tcp_socket`.
- Keep `IHttpRequestCallback` and `IHttpRequestConnection` as interfaces.
- Do not create `IHttpRequestServer` or `IHttpRequestClient`. Implement subclassable concrete classes named `HttpRequestServer` and `HttpRequestClient`, exposing the server/client operations proposed in the document.
- `HttpRequestServer` owns a `Ptr<IAsyncSocketServer>` supplied to its constructor. `HttpRequestClient` owns a `Ptr<IAsyncSocketClient>` supplied to its constructor. Do not make either class a platform-specific template and do not construct a native socket implementation internally.
- This task assumes the `IAsyncSocketServerCallback` API in [TODO_Task_SocketCallback.md](./TODO_Task_SocketCallback.md) has already been implemented and committed. Do not fold that three-platform socket refactor into the HTTP implementation commit.
- Assume, as a constructor precondition that does not need runtime verification, that `IAsyncSocketServer::Start` has not been called and `IAsyncSocketClient::WaitForServer` has not been called before the object is passed to the HTTP wrapper.
- Use the file layout expressed by `AsyncSocket_HttpRequest(Server|Client)?.(h|cpp)`: `AsyncSocket_HttpRequest.h/.cpp` for shared connection, parsing, field, body-framing, serialization, buffering, and lifecycle implementation; `AsyncSocket_HttpRequestServer.h/.cpp` for `HttpRequestServer`; and `AsyncSocket_HttpRequestClient.h/.cpp` for `HttpRequestClient`. `HttpRequest` and `HttpResponse` are deliberately similar; do not maintain separate copies of the same parser or serializer logic, and keep their common implementation in `AsyncSocket_HttpRequest.cpp`.
- Do not route HTTP data through `NetworkProtocolConnection`, `NetworkProtocolServer`, `NetworkProtocolClient`, channels, or their text framing. They are unrelated to HTTP wire syntax and the prerequisite callback task has already migrated the affected adapter.

Define the documented `HttpVersion`, `HttpField`, `HttpBodyChunk`, `HttpBody`, `HttpRequest`, `HttpResponse`, `HttpRequestBodyParsingResult`, `ParseHttpRequestBodyToChunks`, `IHttpRequestCallback`, and `IHttpRequestConnection` in `Source/InterProcess/AsyncSocket/HttpRequest.h`. Declare the two concrete wrappers in their focused `AsyncSocket_HttpRequestServer.h` and `AsyncSocket_HttpRequestClient.h` headers, with shared implementation declarations in `AsyncSocket_HttpRequest.h`. Keep all four headers self-contained and free of Windows, Linux, or macOS implementation headers.

## Async-socket callback prerequisite

Use the completed callback seam from `TODO_Task_SocketCallback.md` directly:

- `HttpRequestServer` implements or owns a retained `IAsyncSocketServerCallback` adapter and supplies it to `IAsyncSocketServer::Start`.
- The callback converts each accepted `IAsyncSocketConnection` into an `IHttpRequestConnection`, retains it through the native callback lifetime, and forwards the HTTP-level accept/reject decision.
- Keep the non-owning socket callback alive until the supplied server's hard-drain `Stop` returns, including reentrant stop handling.
- Do not reintroduce native-server inheritance, add another callback installation API, or modify the Windows, Linux, macOS, or NetworkProtocol implementations in this task.

## HTTP message model and wire parsing

Implement the data model exactly as described in `TODO_SocketHttp_HttpRequest.md`:

- Preserve the original method case and exact percent-encoded request target.
- Store parsed field names as validated lowercase ASCII `WString` values.
- Preserve field order and repeated fields; do not collapse them into a dictionary.
- Keep field values, body data, and trailers as bytes. Do not assume generic field values or bodies are UTF-8.
- A fixed-length body is zero or one `HttpBodyChunk`; a chunked body retains one item per nonzero HTTP chunk. Keep trailers separate from headers and remove all transfer framing from stored body data.

Parse the wire as octets. Do not convert the incoming stream to Unicode before locating and validating the HTTP delimiters and ASCII protocol elements. Require HTTP CRLF framing and determine message boundaries from HTTP syntax, never from `OnRead` or TCP boundaries.

Support the initial HTTP/1.1 framing forms required by the design:

- no body when a request has neither `Transfer-Encoding` nor `Content-Length`;
- fixed-length bodies declared by a valid `Content-Length`;
- chunked bodies when the final transfer coding is `chunked`;
- response bodies explicitly framed by `Content-Length` (including `0`) or chunked transfer coding for the supported request/response exchange;
- chunk extensions that are syntactically validated and ignored;
- arbitrary binary chunk data, trailer fields, and surplus bytes belonging to a later sequential message.

Reject ambiguous or unsafe framing: `Transfer-Encoding` together with `Content-Length`, invalid/overflowing/conflicting `Content-Length`, a final request or response transfer coding other than `chunked`, an otherwise unframed response, malformed start lines or fields, invalid chunk sizes/extensions/data terminators/trailers, arithmetic overflow, and configured size-limit violations. Use named platform-neutral internal defaults for the bounded request line, header block, body, chunk-size line/extensions, trailer block, and header/body idle timeout; document the chosen values in the investigation and cover representative limit failures. A timeout while a message is incomplete is a fatal protocol error. Do not add a public MiniHttp configuration API in this task.

Malformed incoming protocol is reported through `IHttpRequestCallback::OnError` with `fatal == true` before disconnection; terminate the connection when parsing cannot safely continue. Invalid caller-supplied outbound structures are programming errors and should fail synchronously in the normal repository style rather than emitting a partially valid message.

For this first simple layer, do not expand response parsing into interim `1xx` sequences, upgrade/tunnel semantics, successful `CONNECT`, or EOF-delimited response bodies. Every supported response, including an empty response, uses `Content-Length` or chunked framing. HTTP method policy and other special application response behavior belong to MiniHttp.

## Chunk helper contract

Implement `ParseHttpRequestBodyToChunks` with the exact `Succeeded`, `Incomplete`, and `Invalid` contract from the design document.

- On success, return all nonzero chunks and trailers and set `consumedBytes` to the encoded size through the final empty trailer line.
- Leave any suffix after `consumedBytes` untouched for the next message.
- Return `Incomplete` for a valid prefix that needs more bytes.
- Return `Invalid` for malformed/overflowing chunk syntax or configured-limit violations.
- Count exact octets; CR, LF, NUL, `0xFF`, and delimiter-looking sequences inside chunk data have no framing meaning.

Use the same field and chunk parsing implementation from the full request/response state machine rather than creating a second interpretation of chunked syntax.

## Serialization and exchange state

Serialize valid HTTP/1.1 request/status lines, ordered fields, fixed bodies, chunked bodies, and trailers. Share request/response field and body serialization, differing only in the start line and direction-specific state.

- Honor and validate explicit `Content-Length` or `Transfer-Encoding` fields.
- When framing is not supplied, generate `Content-Length` for a single buffered fixed body and `Transfer-Encoding: chunked` when multiple logical chunks or trailers must be represented. Emit an empty response with `Content-Length: 0`; an empty request may use the document's request-specific no-body form or an explicit zero length.
- Never expose socket read boundaries as chunks or socket write boundaries as HTTP semantics.
- Respect the socket's one-outstanding-write rule. Queue or aggregate internal writes as needed, retain every `AsyncSocketBuffer` until its completion, and preserve wire order.
- `IHttpRequestCallback::OnWriteCompleted` means the entire `SendRequest` or `SendResponse` message has completed, not merely one internal socket buffer.

Keep one request/response exchange in flight per connection and reject invalid operation ordering. Preserve unconsumed input, but do not dispatch a pipelined next request while the previous response is outstanding. After a response is complete, reset the state and permit another sequential exchange on the same persistent connection.

Recognize `Connection: close` case-insensitively as connection-reuse state rather than body framing. Finish the current request/response exchange, do not dispatch or send another exchange on that connection, and close it at the direction-appropriate boundary. Do not use EOF to delimit a declared or otherwise incomplete message.

## Callback, lifetime, and shutdown requirements

The connection adapter installs itself on the underlying `IAsyncSocketConnection`, continuously consumes arbitrary positive `OnRead` blocks after `BeginReadingLoopUnsafe`, and exposes the documented HTTP callback interface.

- Forward connected, disconnected, and local error behavior in the documented order.
- Invoke `OnReadRequest` only on server-side connections and `OnReadResponse` only on client-side connections.
- Do not call user callbacks or native `WriteAsync` while holding an HTTP state lock.
- Make parser, exchange, write-queue, callback-installation, and stop state safe for callbacks running on arbitrary threads and for reentrant callback behavior.
- Arm or refresh the receive-idle deadline only while a header or declared body is incomplete. Cancel and drain its pending work when the message completes or the connection stops. Cover timeout behavior through a test-controlled timing seam or another bounded deterministic mechanism; do not sleep for the production timeout.
- Retain each accepted HTTP connection for as long as the native socket can call its non-owning callback.
- `Stop` and destruction must be idempotent hard-drain boundaries: stop native work, finish callbacks that can access owners, detach callbacks safely, and then release adapters and supplied socket objects. Do not use sleeps to approximate callback completion.

## Cross-platform shared tests

Add focused coverage in `Test/Source/TestInterProcess_HttpRequest.cpp`. Follow the organization of `TestInterProcess_AsyncSocket.cpp` and `TestInterProcess.cpp`: define the HTTP scenarios once in platform-neutral helpers and bind only the concrete socket types under the existing platform guards.

| Guard | Native server/client |
| --- | --- |
| `VCZH_MSVC` | `windows_socket::AsyncSocketServer` / `windows_socket::AsyncSocketClient` |
| `VCZH_GCC && VCZH_APPLE` | `macos_socket::AsyncSocketServer` / `macos_socket::AsyncSocketClient` |
| `VCZH_GCC && !VCZH_APPLE` | `linux_socket::AsyncSocketServer` / `linux_socket::AsyncSocketClient` |

This is one shared scenario instantiated against the three supported platform implementations, not three copied test bodies. Use dedicated non-overlapping loopback ports above the ranges already occupied by `TestInterProcess.cpp` and `TestInterProcess_AsyncSocket.cpp`. Start the listener deterministically before the client waits; use bounded events/barriers and callback-thread failure recording instead of sleeps.

At minimum, cover:

1. `ParseHttpRequestBodyToChunks` directly with:
   - `7\r\nHello, \r\n6;ext=ok\r\nworld!\r\n0\r\nDigest: value\r\n\r\nNEXT`;
   - two chunks, a lowercase `digest` trailer, and `consumedBytes` stopping before `NEXT`;
   - representative truncations at the size line, chunk data, terminating CRLF, and trailer section returning `Incomplete`;
   - binary data containing CR/LF/NUL/`0xFF`;
   - representative invalid hexadecimal, overflow, malformed chunk terminator, and malformed trailer cases.
2. A shared `HttpRequestClient` to `HttpRequestServer` integration scenario with at least two sequential exchanges on the same connection:
   - a fixed-length `POST` with an exact encoded target such as `/echo/%2F?q=a%20b`, ordered repeated custom fields, and a binary body;
   - a non-default response such as `201 Created` with multiple chunked binary chunks and a trailer;
   - a second empty request/response using `Content-Length: 0` to prove state reset and persistent sequential use;
   - assertions for version, method/target, status/reason, lowercase names, repeated-field order, exact bytes, chunk boundaries, trailer separation, and whole-message write completion.

Do not rely only on native TCP segmentation to provide parser fragmentation coverage. Use a small test implementation or another deterministic seam for representative split and coalesced reads across a start line or header terminator, a chunk boundary, and a message suffix. Exhaustively trying every byte offset is not required.

## Mandatory Windows HTTP interoperability

Testing `HttpRequestServer` only against `HttpRequestClient` proves that the two new implementations agree with each other; it does not prove that either speaks HTTP/1.1 correctly. Add both Windows-only cross-tests under `VCZH_MSVC`:

### `windows_http::HttpClientApi` to `HttpRequestServer`

- Wrap `windows_socket::AsyncSocketServer` in the new `HttpRequestServer` and start it first.
- Construct `windows_http::HttpClientApi(L"127.0.0.1", port)` so WinHTTP targets the IPv4 loopback listener, then send an explicit request with a method, target, content type, custom mixed-case field, and nonempty body.
- Assert on the new server that the native client produced a valid HTTP/1.1 request, including the exact target, normalized custom field, generated body framing, and exact body bytes. Do not assert the complete header list or global order because WinHTTP adds fields.
- Send a non-default response from `HttpRequestServer`, preferably chunked with two chunks. Assert through the fields exposed by `windows_http::HttpClientApi`: status, flattened body, content type, and cookie when supplied.

### `HttpRequestClient` to `windows_http::HttpServerApi`

- Derive a small test server from `windows_http::HttpServerApi`, register a URL prefix such as `http://localhost:<port>/<prefix>/`, and start HTTP.sys before connecting the async-socket client.
- Wrap `windows_socket::AsyncSocketClient` in `HttpRequestClient` and send a target under that exact prefix. Set `Host` to exactly `localhost:<port>` so HTTP.sys matches the registered host, and include a nonempty fixed-length body; the test server may use `GetUtf8Body` when the body and content type are chosen accordingly.
- Inspect the HTTP version, verb, raw target, custom field, and body in `OnHttpRequestReceived`, then answer with `HttpServerApi::SendResponse` using a non-default status/reason and UTF-8 body.
- Assert that `HttpRequestClient` parses the native response's status, reason, normalized fields, and exact flattened body. Accept whichever valid explicit `Content-Length` or chunked framing HTTP.sys emits; do not require one particular choice when the API does not guarantee it.
- Signal completion from the HTTP.sys callback and call `HttpServerApi::Stop` from the test thread, not from inside `OnHttpRequestReceived`, because stop waits for pending callbacks.

Fully qualify `async_tcp_socket::HttpRequest` / `HttpResponse` and `windows_http::HttpRequest` / `HttpResponse` in these tests because the namespaces contain colliding type names.

## Source and project integration

- Register the four product headers (`HttpRequest.h` and the three `AsyncSocket_HttpRequest*.h` files) as `ClInclude`, the three `AsyncSocket_HttpRequest*.cpp` files as `ClCompile`, and `TestInterProcess_HttpRequest.cpp` as `ClCompile` explicitly in `Test/UnitTest/UnitTest/UnitTest.vcxproj`; wildcard entries are not allowed.
- Place product files under the existing `Common\InterProcess\AsyncSocket` filter and the new test under `Source Files\TestInterProcess` in `UnitTest.vcxproj.filters`.
- No solution change is expected. Common sources should flow into the Unix build from the MSBuild project; no `Test/Linux/vmake` change is expected unless inspection proves one necessary.
- Never edit generated `Test/Linux/vmake.txt`, `Test/Linux/makefile`, generated `Release` outputs, or `Import` dependencies by hand.
- Preserve unrelated working-tree changes and avoid refactoring the existing Windows `HttpServer` / `HttpClient` transport beyond using their API classes in tests.

## Explicitly out of scope

- Routing, REST dispatch, method allowlists, `Host` policy, CORS, browser behavior, static files, media-type policy, cookies/authentication semantics, content decoding/compression, redirects, multipart forms, and application-generated HTTP error pages.
- TLS/HTTPS, HTTP/2, HTTP/3, proxies, WebSocket, Server-Sent Events, and pipelining.
- The `SocketHttpServer` / `SocketHttpClient` layer and every behavior in `TODO_SocketHttp_MiniHttpApi.md`.
- Reusing or redesigning `NetworkProtocolConnection` and the channel layer.
- Hand-editing generated release or Unix build artifacts.

## Verification and acceptance

Follow `.github/copilot-instructions.md`, `Project.md`, and all referenced coding, source-file, build, execution, and native-dialog guidance.

- Build `Test/UnitTest/UnitTest.sln` through `.github/Scripts/copilotBuild.ps1` for Debug/Release and Win32/x64.
- Run `TestInterProcess_HttpRequest.cpp` through `.github/Scripts/copilotExecute.ps1` in Debug x64, ensuring it is included by the `.vcxproj.user` filter.
- Run the complete Debug x64 UnitTest suite.
- Inspect the ends of `.github/Scripts/Build.log` and `.github/Scripts/Execute.log`; require zero build errors, all selected tests passing, and no Debug CRT memory-leak report after the summary.
- Keep the common runner bound to all three platform implementations and, when Linux or macOS execution is available, build and run it from `Test/Linux` using the repository `build.sh` workflow. Do not claim unavailable platform runs.
- Record how `HttpRequestServer` uses the prerequisite callback API, together with parsing/framing choices, limits, tests, and verification evidence, in `.github/TaskLogs/Copilot_Investigate.md`.

Acceptance requires both Windows native interoperability directions to pass; new client versus new server alone is insufficient. All temporary diagnostics must be removed. Commit only the intentional implementation, tests, project metadata, and investigation record, then push the current branch.

# UPDATES

# TEST [CONFIRMED]

Confirm the missing HTTP layer structurally, then implement one focused test file that distinguishes a correct octet-framed HTTP/1.1 transport from two implementations that merely agree with each other.

The structural reproduction is:

- None of the seven requested product files or `Test/Source/TestInterProcess_HttpRequest.cpp` exists.
- Repository-wide source and test searches find no `HttpBodyChunk`, `ParseHttpRequestBodyToChunks`, `IHttpRequestConnection`, `HttpRequestServer`, or `HttpRequestClient` implementation outside the design/task documents.
- `UnitTest.vcxproj` and `UnitTest.vcxproj.filters` contain only the existing async-socket files and no HTTP-request codec, wrapper, or test entry.
- The prerequisite `IAsyncSocketServerCallback` seam is present, so this task is isolated to the new common HTTP layer, its wrappers, tests, and project metadata.

The problem is confirmed. All requested product/test paths are absent, the public symbols exist only in task/design Markdown, and no project entry can compile or execute the required HTTP behavior. The clean pre-change Debug x64 build reports zero warnings and zero errors. The complete baseline suite passes 13/13 test files and 128/128 test cases with no Debug CRT memory-leak output, establishing that the prerequisite async-socket implementation and existing interprocess tests are green before this layer is added.

Use these named internal defaults and exercise their boundaries without adding a public configuration API:

- request/status line: 8 KiB;
- header block: 64 KiB;
- decoded body: 16 MiB;
- chunk-size line including extensions: 4 KiB;
- trailer block: 64 KiB;
- incomplete-message idle timeout: 30 seconds.

`TestInterProcess_HttpRequest.cpp` will contain the following deterministic coverage:

1. Call `ParseHttpRequestBodyToChunks` directly with the specified two-chunk/trailer/suffix input and require two exact chunks, lowercase `digest`, the exact opaque trailer value, and `consumedBytes` immediately before `NEXT`. Check truncations of the size line, chunk data, data terminator, and trailer terminator as `Incomplete`; binary CR/LF/NUL/`0xFF` data as `Succeeded`; and invalid hex, overflow, malformed extensions/terminators/trailers, declared body-limit overflow, and chunk-line-limit overflow as `Invalid`.
2. Drive a server-side and client-side HTTP connection through a deterministic fake `IAsyncSocketConnection`. Split start lines, `\r\n\r\n`, chunk-size lines, chunk data terminators, and trailers across arbitrary `OnRead` calls; coalesce a completed message with the next-message suffix; and require exact message boundaries independent of socket reads. Cover malformed start lines/fields, conflicting or invalid `Content-Length`, `Transfer-Encoding` plus `Content-Length`, unsupported final transfer coding, unframed responses, request/header/trailer limits, and fatal error-before-disconnect ordering.
3. Exercise the 30-second production timeout through an internal test-controlled deadline seam: begin an incomplete header/body, fire the installed deadline deterministically without sleeping, and require one fatal protocol error followed by hard-drained disconnection. Completing or stopping the message must cancel and drain the deadline so it cannot call a destroyed adapter.
4. Verify outbound serialization with ordered repeated fields, fixed bodies, generated `Content-Length`, explicit compatible framing, generated chunked framing for multiple chunks/trailers, exact binary bytes, and synchronous failure for invalid methods/targets/status lines/fields, ambiguous framing, mismatched lengths, invalid trailers, overflow, and wrong exchange ordering. One socket write completion must produce one whole-message `IHttpRequestCallback::OnWriteCompleted`.
5. Register one shared native scenario against the Windows, Linux, and macOS async-socket bindings. On one persistent connection, send the specified fixed binary `POST /echo/%2F?q=a%20b` with ordered repeated fields, return `201 Created` with two binary chunks and a trailer, then complete a second empty request/response with `Content-Length: 0`. Require exact request/response models, state reset, whole-message write completion, and deterministic startup/shutdown without sleeps.
6. On Windows, cross `windows_http::HttpClientApi` into `HttpRequestServer` and require exact target/custom field/body plus a non-default chunked response observed through the native client. Cross `HttpRequestClient` into a small `windows_http::HttpServerApi` subclass and require HTTP.sys to observe the exact host/target/verb/custom field/body, then require the new client to parse the native non-default response. Stop HTTP.sys only from the test thread after its callback signals completion.
7. Cover `Connection: close`, callback installation, reentrant connection/server stop, server accept/reject, retained accepted adapters, idempotent external stop/destruction, no callback after a hard-drain return, and no write while holding the HTTP state lock.

Acceptance requires all focused cases above to pass in Debug x64, including both native Windows interoperability directions. Debug/Release Win32/x64 builds must report zero errors, the complete Debug x64 suite must pass, and the final logs must contain no Debug CRT memory-leak report. The shared test registration and common source/project entries must remain available to Linux and macOS, but only platforms actually available in this environment will be claimed as built or executed.

# PROPOSALS

- No.1 Add one bounded octet codec and retained async-socket adapters

## No.1 Add one bounded octet codec and retained async-socket adapters

Add the four self-contained public/common headers and three common implementation files requested by the task. `HttpRequest.h` owns only the move-only wire model, the chunk helper, and the two connection callback interfaces. `AsyncSocket_HttpRequest.h` declares the concrete shared connection adapter and its implementation-only construction/timing seams. `AsyncSocket_HttpRequestServer.h` and `AsyncSocket_HttpRequestClient.h` expose subclassable pointer-wrapping `HttpRequestServer` and `HttpRequestClient`; no HTTP server/client interface, native template, or platform implementation header is introduced.

Use one shared byte-oriented codec in `AsyncSocket_HttpRequest.cpp` for requests and responses. It will:

- validate token methods and field names, visible ASCII request targets, decimal status codes, strict `HTTP/<major>.<minor>` start lines, CRLF delimiters, opaque field values, and chunk extensions before converting validated ASCII identifiers to `WString`;
- preserve method case, exact request target, header/trailer order, repeated fields, opaque values, fixed body bytes, and every nonzero chunk boundary while normalizing only field names to lowercase;
- recognize all repeated/comma-separated `Content-Length` values and require one non-overflowing value, reject `Transfer-Encoding` together with `Content-Length`, and require the final transfer coding to be `chunked` whenever transfer coding is present;
- treat a request without either framing field as bodyless, require every supported response to have explicit fixed or chunked framing, and reject unsupported EOF, interim, upgrade, tunnel, and pipelined-response interpretations;
- share the field, transfer-coding, content-length, and chunk parser between the direct `ParseHttpRequestBodyToChunks` helper and the complete-message parser, returning `Incomplete` without changing the caller's output and returning `consumedBytes == 0` unless parsing succeeds;
- validate and ignore chunk extensions, copy exactly the declared binary octets, require each data CRLF, parse bounded trailers, reject framing-control trailers, and retain any suffix after the exact completed-message boundary;
- enforce named internal limits of 8 KiB per start line, 64 KiB per header block, 16 MiB decoded body, 4 KiB per chunk-size/extension line, and 64 KiB per trailer block, with checked arithmetic before every addition or allocation.

Serialize through the same validation/framing analysis. Explicit `Content-Length` or chunked transfer coding must agree with the supplied body representation. Without an explicit framing field, an empty or one-chunk buffered body uses `Content-Length` (with the request-specific empty form permitted), while multiple chunks or trailers generate `Transfer-Encoding: chunked`. Existing fields retain order and generated framing is appended. Each bounded message is assembled into one retained `AsyncSocketBuffer`, so the underlying one-write rule is satisfied directly and one native completion maps exactly to one HTTP-level `OnWriteCompleted`.

Implement a concrete shared connection adapter around a supplied `Ptr<IAsyncSocketConnection>` with distinct server/client direction state. Its synchronized state owns the receive buffer, installed non-owning HTTP callback, exchange phase, one outstanding write buffer, close-after-exchange flag, callback-depth/drain counters, and stop state. Native callbacks capture the required state under the lock, then parse, call user code, call `WriteAsync`, or call native `Stop` only after releasing it. A server delivers one request and gates parsing of any buffered suffix until its response completes; a client permits one request, waits for its explicitly framed response, and only then permits the next request. `Connection: close` is recognized as a case-insensitive comma token on either message and closes only at the completed exchange boundary.

Arm the receive-idle deadline only after a positive read leaves a started header or declared body incomplete, refresh it on later progress, and cancel it at message completion or stop. The production deadline implementation uses a retained cross-platform condition-variable worker state with a generation/deadline value rather than a sleeping callback or a platform timer. Stop signals cancellation and drains any entered timeout callback; a callback-thread-aware deferred join prevents self-deadlock. `AsyncSocket_HttpRequest.h` also supplies an implementation-level timeout-controller interface/factory accepted by the shared connection constructor, allowing the test to install, fire, cancel, and verify the 30-second deadline deterministically without exposing a MiniHttp configuration API.

Pattern callback lifetime on the existing `NetworkProtocolConnection` callback domain while keeping the HTTP parser independent. Installation calls `OnInstalled` and connected notification in documented order; fatal parse/timeout errors call `OnError(..., true)` before stopping the native connection; disconnect while a message is incomplete is also fatal before `OnDisconnected`. Reentrant `Stop` prevents new user callback entries and native operations immediately, but defers only the current callback's own drain. A later external stop or destruction completes the native hard drain, detaches its non-owning callback, cancels and drains the deadline, and releases the retained state and socket.

Implement `HttpRequestServer` with a retained composition bridge implementing `IAsyncSocketServerCallback`. It constructs and retains an HTTP connection for every offered native connection, invokes virtual `OnClientConnected` outside its lock, and returns the same accept/reject decision. Accepted and rejected adapters remain retained until the supplied native server's callback lifetime has drained, preventing either native connection from calling an expired raw callback. Server stop uses callback-depth-aware deferred finalization: nested stop prevents later accepts and may return without waiting for itself, while a queued/later external idempotent stop completes the supplied server's hard drain before bridge and connection release. Implement `HttpRequestClient` by owning the supplied native client and one shared client-direction connection, forwarding `WaitForServer`/`GetStatus`, and retaining both until connection and client stop have drained.

Add `TestInterProcess_HttpRequest.cpp` and explicit project/filter entries. The file will define deterministic fake socket/client/server objects for codec fragmentation, coalescing, ordering, deadline, callback, and hard-drain tests; define the two-exchange native scenario once and bind it to each platform under the existing guards; and add both required Windows cross-tests on ports 38801 and 38802. The WinHTTP side waits for `HttpQuery` completion before stopping its native client. The HTTP.sys side copies request data synchronously, uses the exact trailing-slash localhost prefix and matching `Host`, signals the test thread, and is stopped only from that test thread. Port 38800 is reserved for the shared new-client/new-server scenario. No URL reservation command, generated Unix artifact, release output, import dependency, native socket implementation, or existing Windows HTTP transport is changed.

### CODE CHANGE

The implementation, tests, project metadata, and verification evidence will be recorded here after this proposal is implemented.
