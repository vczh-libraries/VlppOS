# !!!LEARNING!!!

# Orders

- `vl::inter_process` shutdown must finish callbacks before returning [6]
- Use `HttpClientApi` and `HttpServerApi` for reusable Windows HTTP transport code [3]
- `NetworkProtocolLocalChannelClient` owns server-local behavior [2]
- `NetworkProtocolChannel` queues should stay grouped for `BatchWrite` [2]
- `NetworkProtocolChannel` uses explicit positive sender and receiver ids [2]
- Async-socket concrete server and client constructors stay port-only across platforms [2]
- Do not invoke inter-process user callbacks while holding internal locks [2]
- Template `NetworkProtocolChannelServer` over the protocol server base [1]
- Use `IChannelServer::OnClientConnected` `localClient` to identify local clients [1]
- Start `INetworkProtocolServer` / `IChannelServer` after construction [1]
- Keep `IChannelServer` delivery-only; use local clients for server speech [1]
- Keep channel protocol declarations separate from implementation headers [1]
- Group `SpinLock`-protected fields with coverage comments [1]
- `HttpServerConnection` queues pending outbound `/Request` responses [1]
- Split remote channel errors from local transport errors [1]
- `NetworkPackage` first section preserves null client ids and normalizes empty extras [1]
- `vl::inter_process` Windows transports use feature-specific nested namespaces [1]
- `Thread::Wait` completion belongs to the native thread entry point [1]
- Socket HTTP adapters keep async sockets caller-injected [1]
- Share Socket HTTP listeners by exact server identity and segment-bounded prefixes [1]
- `HttpRequestClient` owns the fatal HTTP 404 policy [1]
- Keep active TUI state inside the `TUI` lifecycle [1]
- Measure TUI character width through platform APIs [1]
- `TuiCharInfo::code` carries one native `wchar_t` unit [1]
- Convert TUI native units to scalars in the consumer [1]
- Synchronize Windows TUI buffer geometry with the visible viewport [1]
- Store semantic TUI state and derive each frame [1]
- Use one general information overlay for TUI help and errors [1]

# Refinements

## `NetworkProtocolChannel` queues should stay grouped for `BatchWrite`

For channel delivery, keep queued packages grouped by target id so each group can be passed directly into `BatchWrite`. Avoid collecting a flat list and regrouping it later when the `Group` structure naturally maintains the batches the transport writer needs.

When broadcasts carry a blocked-receiver list, treat the receiver id and blocked id list as part of the grouping key so each batch preserves its delivery exclusions through serialization and local delivery.

## `NetworkProtocolChannel` uses explicit positive sender and receiver ids

Channel messages carry both sender and receiver semantics. Ordinary channel chat should use real positive client ids for both sides; do not use `AdminClientId` as the sender for normal server-originated messages. If server-side behavior needs to speak on the channel, connect it as a local client and send from the assigned id. Validate both sender and receiver client ids, and keep public names such as `receiverClientId` aligned with the actual delivery direction.

Channel APIs should derive the sender id from the owning channel client instead of accepting a caller-supplied sender id. This keeps `SendToClient` / `BroadcastFromClient` from trusting duplicate sender arguments and makes local and remote clients follow the same ownership rule.

## Start `INetworkProtocolServer` / `IChannelServer` after construction

For `vl::inter_process`, transport and channel servers should initialize handles and state in constructors, but begin named-pipe or HTTP accept callbacks only from `Start()`. Channel servers should reject remote and local clients before they are started, and callers should start the channel layer and transport layer only after concrete server construction is complete.

## Template `NetworkProtocolChannelServer` over the protocol server base

`NetworkProtocolChannelServer<TPackage, TSerialization, TServerBase>` should inherit the concrete protocol server base (for example `NamedPipeServer` or `HttpServer`) and `IChannelServer<TPackage>`, forwarding constructor arguments to `TServerBase`. The channel layer should own channel bookkeeping, local-client routing, and message dispatch, while the transport base owns listening, network connection lifetime, and transport shutdown.

When the constructor has overloads for serializer context plus transport arguments, disambiguate the context-aware overload before forwarding the remaining arguments to the transport base so a serializer context is not accidentally treated as a transport constructor argument.

## Use `IChannelServer::OnClientConnected` `localClient` to identify local clients

`IChannelServer::OnClientConnected` receives a final `IChannelClient<TPackage>* localClient` argument. It is non-null only for server-side local clients and null for network clients, so channel-server implementations and tests should use this callback argument instead of inferring locality from connection order or extra registration flags.

## `vl::inter_process` shutdown must finish callbacks before returning

Named-pipe and HTTP protocol stop paths must cancel pending work, unregister waits, close active handles, and wait for final callbacks before returning. Unit tests should call `Stop()` as the boundary instead of adding sleeps to hope that callbacks finish later.

For WinHTTP requests, track request lifetime before `WinHttpSendRequest` and release it from `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` for successfully submitted requests; handle-closing is the reliable final callback boundary. For registered waits, use owned wait contexts plus atomic exchange/compare-exchange so `Stop()` can either unregister a not-yet-started wait or wait for an already-started callback before handles and buffers are destroyed.

For named-pipe connections, cancel pending overlapped pipe I/O before waiting for callbacks to drain when the remote side can close first. `NetworkProtocolChannelClient` destruction should also check whether the transport connection is already disconnected before trying to stop it.

An `IAsyncSocketServerCallback` passed non-owningly to `IAsyncSocketServer::Start` must remain valid until the server reaches its callback-drain boundary. An external `Stop()` waits for already-entered accept callbacks; a callback-reentrant stop prevents later callbacks and defers self-dependent cleanup so it does not deadlock, with a later idempotent external stop completing finalization before the callback owner is released.

## Keep `IChannelServer` delivery-only; use local clients for server speech

Keep `IChannelServer` focused on accepting clients and delivering protocol messages. Do not expose a `GetChannel` escape hatch or let `NetworkProtocolChannelServer` own user channel objects for ordinary chat. When the server needs to participate as a speaker, create a server-side local channel client, register it through `ConnectLocalClient`, and let it send/receive through the same channel contract as other clients.

## Keep channel protocol declarations separate from implementation headers

Keep non-template inter-process channel declarations such as `INetworkProtocol*` in `NetworkProtocol.h`, non-template package serialization in `ChannelPackage.(h|cpp)`, and template channel implementations in focused `Channel*Impl.h` headers. `NetworkProtocolChannel.h` should act as the channel-facing umbrella that keeps the protocol overview comment and includes the implementation headers. Each split header still needs its own required includes instead of relying on the umbrella include order.

## `NetworkProtocolLocalChannelClient` owns server-local behavior

Keep local-client state and local-server branches out of `NetworkProtocolChannelClient`. Put the server-side local connection pointer, local wait/disconnect handling, local send routing, and local error broadcasting in `NetworkProtocolLocalChannelClient`; override the local branch there and call the base method for common client work. `NetworkProtocolChannelServer::ConnectLocalClient` can require this subtype (for example with `dynamic_cast`) so no extra lock is needed just to protect a `localServer` field on ordinary network clients.

When both network and local channel clients need shared maps, serialization context, ids, status, and batch receiving, extract a common base that owns only those shared concerns. Keep transport-only members such as `INetworkProtocolClient`, callbacks, and wait events in the network client, and let the local client's `WaitForServer` be a no-op because `IChannelServer::ConnectLocalClient` is the local connection boundary.

## Group `SpinLock`-protected fields with coverage comments

For classes that use `SpinLock`, group every field protected by a given lock directly under that lock and add a short comment naming the coverage. This makes it clear which state must be accessed while holding each lock, especially in inter-process protocol and test helper code with multiple asynchronous paths.

## `HttpServerConnection` queues pending outbound `/Request` responses

When HTTP inter-process channel delivery can produce multiple outbound responses back-to-back, queue pending `/Request` responses instead of storing one overwriteable pending response. This preserves sequences such as the connection response followed immediately by the first channel message.

## Use `HttpClientApi` and `HttpServerApi` for reusable Windows HTTP transport code

Keep Windows HTTP API ownership in non-copyable/non-movable RAII structs. `HttpClientApi` should own the WinHTTP session/connection for one host and port and complete async requests through a single `Func<void(Variant<HttpResponse, HttpError>)>` path. HTTP status codes such as 404 are successful `HttpResponse` values; only underlying client/API failures are `HttpError`.

`HttpServerApi` should own one `http://host:port/prefix/.../` prefix, initialization/finalization, async request receive loop, optional OPTIONS handling, and generic UTF-8 request/response helpers. Use `GetUtf8Body(PHTTP_REQUEST)` for shared complete-body reading and validation, and use `SendResponseUtf8(...)` for the common JSON UTF-8 success response shape. Pass HTTP response status/message/body/content-type as `HttpServerResponse` instead of a loose tail of arguments. Higher-level inter-process protocol classes should keep only route handling and connection state.

## Do not invoke inter-process user callbacks while holding internal locks

When installing an HTTP connection callback, move queued strings to a local list while holding the `SpinLock`, then release the lock before calling `OnInstalled` or replaying queued `OnReadString` callbacks. This avoids reentrancy and ordering races where a `/Response` callback can observe user callbacks before installation has completed.

The same rule applies to native accept paths: capture the installed callback while synchronized, release the server lock, and only then invoke `IAsyncSocketServerCallback::OnClientConnected`.

## Split remote channel errors from local transport errors

Use `IChannelClient::OnReadError` only for errors broadcast by `IChannelServer::BroadcastError`. Route lower-level connection, request, response, named-pipe, and HTTP transport failures through `OnLocalError`; if the failure closes the client, report the fatal local error before disconnecting. HTTP `/Connect` and `/Response` failures should retry a bounded number of times and report each failed attempt locally, while long-poll `/Request` failures can retry silently as long as the client is still running.

## `NetworkPackage` first section preserves null client ids and normalizes empty extras

`NetworkPackage` serializes the first section as `clientId,extraClientId1,...`. A null `clientId` with extras starts with a comma, while a null `clientId` without extras stays empty, so deserialization can distinguish broadcast markers from extra id lists. Empty `extraClientIds` and null `extraClientIds` should normalize to null after deserialization.

## `vl::inter_process` Windows transports use feature-specific nested namespaces

Keep concrete named-pipe types from `NamedPipe.Windows.*` in `vl::inter_process::named_pipe` and concrete Windows HTTP types from `Http(Server|Client)(Api)?.Windows.*` in `vl::inter_process::windows_http`. Keep the VlppOS knowledge base, generated Release surface, and downstream consumers aligned with these public namespace locations.

## Async-socket concrete server and client constructors stay port-only across platforms

Keep Windows, Linux, and macOS concrete async-socket server/client constructors shaped around `vint port` as the sole constructor argument where possible. A common constructor contract lets `TestServer<TServerBase>` and platform-neutral factories instantiate every implementation without platform-specific glue.

## `Thread::Wait` completion belongs to the native thread entry point

On GCC platforms, the common native `Thread` entry path must publish `Thread::Stopped` and signal completion for every derived class after `Run()` returns. `ProceduredThread` and `LambdaThread` should invoke only their supplied work; putting completion in those wrappers leaves arbitrary custom subclasses permanently blocked in `Thread::Wait()`.

Publish `Thread::Running` before `pthread_create` and restore `Thread::NotStarted` if creation fails, so a fast thread cannot publish `Stopped` and then have `Thread::Start()` overwrite it. Capture any auto-delete policy before calling virtual `Run()`, publish completion before deletion, and never read the object after waiters can resume.

## Socket HTTP adapters keep async sockets caller-injected

Requiring `Ptr<IAsyncSocketServer>` or `Ptr<IAsyncSocketClient>` in Socket HTTP constructors is an intentional composition boundary. These adapters must not select a platform implementation or create their transport internally, and endpoint facts such as the port should come from the injected socket instead of a duplicate constructor argument.

`SocketHttpClient` should consume the exact injected client for its first physical lane. Concurrent lanes and recovery connections come from `IAsyncSocketClient::CreateSameEndpointClient()`; require each clone to be distinct, ready, and on the same port, and treat violations as terminal contract failures instead of retrying indefinitely.

## Share Socket HTTP listeners by exact server identity and segment-bounded prefixes

Multiple `SocketHttpServerApi` objects may share a listener only when they receive the same `IAsyncSocketServer` object. Key shared dispatch state by exact server identity rather than port, start the listener once, keep registrations independently stoppable, and stop it after the final prefix is removed.

Normalize a trailing slash from each origin-form prefix and route only the exact prefix or slash-delimited descendants. Preserve longest-prefix selection, but never let a textual near-match such as `/ABC/defghi` match `/ABC/def`.

## `HttpRequestClient` owns the fatal HTTP 404 policy

Keep `HttpRequestConnection` permissive so it can parse and deliver an ordinary 404 response. `HttpRequestClient`, which owns the physical socket client, applies the stricter policy: classify 404 as `HttpResponseFailure::NotFound`, skip normal response delivery, report the structured failure, and stop the connection.

Map that failure through `SocketHttpClientApi` as `SocketHttpClientErrorCode::ResponseNotFound`. The high-level client treats it as one fatal local error on `/Connect`, `/Request`, or `/Response` and does not retry the endpoint.

## Keep active TUI state inside the `TUI` lifecycle

Do not place per-run TUI state in `BEGIN_GLOBAL_STORAGE_CLASS`. Store it behind a `TUI`-owned `Impl`, allocate it on the inactive-to-active `Start` transition, and deallocate it after `Stop` cleanup. A shared `Impl` declaration can be inherited by platform-specific implementations when they need extra state.

Keep only state that intentionally survives runs, such as listener registrations and a scoped test-backend binding, outside the active `Impl`.

## Measure TUI character width through platform APIs

Do not maintain a generated Unicode-width table or its generator for `TUI::MeasureChar`. Delegate width classification to the operating system in the platform implementation—for example, Win32 character-type APIs on Windows and `wcwidth` on POSIX—while keeping common buffer and drawing rules in shared code.

## `TuiCharInfo::code` carries one native `wchar_t` unit

Keep TUI character events aligned with native `wchar_t` consumers. On Windows, enqueue each UTF-16 code unit from `KEY_EVENT_RECORD` unchanged, so a supplementary scalar produces independent high- and low-surrogate callbacks in native event order. On Linux and macOS, retain incremental UTF-8 decoding and enqueue each completed scalar as one UTF-32 `wchar_t`.

Do not combine or replace Windows surrogates in the backend or shared dispatcher. Repeat counts and modifiers apply to each native unit, and the existing stop contract may suppress a queued low surrogate after its high-surrogate callback. Keep `TuiPixel::c`, width measurement, and drawing APIs scalar-based with `char32_t`.

## Convert TUI native units to scalars in the consumer

Scalar-oriented consumers of `TuiCharInfo` should use `vl::encoding::UtfConversion<wchar_t>::To32`. Under `VCZH_WCHAR_UTF16`, retain at most one high surrogate across callbacks, combine only adjacent high/low units in the character-unit stream, and reprocess a valid current unit after handling an unmatched pending surrogate so input and controls are not swallowed. Under `VCZH_WCHAR_UTF32`, convert one unit directly and validate it before passing it to scalar-only APIs.

Do not use `UtfToUtfReaderBase` or `UtfStringToStringReader` across callbacks; those pull readers assume their input is already available, treat zero as end-of-input, and cannot wait for or resynchronize an incomplete pair.

## Synchronize Windows TUI buffer geometry with the visible viewport

While TUI owns a Windows console, keep the active screen buffer and zero-origin window synchronized to the visible viewport in Win32-safe resize order. Recheck after input and timer wakeups so viewport-only changes generate resize events and shrinking does not leave a vertical scrollbar; startup-only buffer shrinking is insufficient.

Save the original buffer size and window rectangle before entering either VT or classic output mode. Restore the alternate-screen state, cursor, modes, buffer size, and window geometry on normal shutdown and exceptions, with partial-startup rollback owned by `WindowsTuiBackend::Start`. Account for the terminal host's asynchronous alternate-screen transition before the final geometry restoration.

## Store semantic TUI state and derive each frame

Keep durable callback state semantic: completed scalar input, accumulated commands in submission order, optional information content, and cursor phase. Do not cache the active `TuiPixel*`, terminal dimensions, wrapped rows, translated coordinates, or a painted canvas because resize invalidates or changes them.

Rebuild each frame from semantic state. Replay commands from logical coordinates into a temporary interior buffer, clip there, copy the bounded result into the terminal buffer, and draw protected borders and command UI last.

## Use one general information overlay for TUI help and errors

Represent modal playground information as an ordered `List<U32String>` instead of an error-specific structure. Parse failures populate two items for the original command and reason; `HELP` populates only the concise accepted command shapes. Use the same wrapping, centering, clipping, cursor hiding, and Enter-to-dismiss behavior for both.

Handle exact case-insensitive `HELP` and `EXIT` controls before painting-command parsing and never add them to painting history. `EXIT` is the only application-controlled quit action; ordinary characters such as `q`/`Q` and Escape do not stop the playground.
