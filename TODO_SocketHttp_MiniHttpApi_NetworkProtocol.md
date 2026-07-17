# INetworkProtocol(Server|Client) based on SocketHttp(Server|Client)Api

Implementations in `AsyncSocket/AsyncSocket_Http(Server|Client).(h|cpp)`
Using namespace `vl::inter_process::async_tcp_socket`

- On Windows, use socket vs http api in unit test (shared)
- On any platform, use socket vs socket in unit test (shared)

## Http(Server|Client).Windows.(h|cpp) Request Convention

This is a compatibility contract for the existing Windows `HttpServer` and `HttpClient`. It is a private text-message transport implemented with HTTP, not a REST API and not the contract of the generic `IHttpRequest*` layer. The dedicated `INetworkProtocol*`-over-HTTP wrapper is responsible for constructing and interpreting these requests.

The legacy route names describe the long-poll protocol, not ordinary HTTP terminology:

- `/Request/{guid}` asks the server for the next server-to-client message.
- `/Response/{guid}` submits one client-to-server message and may receive one server-to-client message in its HTTP response.

Use either an empty `{baseUrl}` or one with a leading `/` and no trailing `/`; the current constructors concatenate it without normalizing it. Let `{guid}` be the opaque token returned by the server. The Windows server registers `http://localhost:{port}{baseUrl}/`; a socket client may connect to `127.0.0.1`, but should send `Host: localhost:{port}` for HTTP.sys URL-prefix compatibility. Paths and their case must match exactly.

### Common Wire Rules

- Each logical message is one nonempty `WString` encoded directly as UTF-8 bytes, without a BOM, terminator, JSON quoting or other text encoding. Despite the media type below, the body is not required to be JSON.
- Do not use embedded NUL characters. The legacy UTF-8 reader reconstructs a zero-terminated string and cannot preserve them safely.
- A body size is always its UTF-8 byte count, not its number of UTF-16 code units or Unicode scalars.
- The exact legacy media type is `application/json; charset=utf8`. Successful responses for all three routes must have status `200` and this exact `Content-Type` value. The Windows client compares the value literally, including spelling and spacing.
- The Windows client sends `Accept: application/json; charset=utf8` on all three routes. The Windows server does not validate `Accept`, but a compatible client should send it.
- A `/Response/{guid}` request must have `Content-Type: application/json; charset=utf8`, a fixed `Content-Length` greater than zero and exactly that many body bytes. The Windows body reader requires `Content-Length`, so chunked transfer coding is not accepted for this request.
- `/Connect` and `/Request/{guid}` requests have no body and need no `Content-Type`. A socket implementation should send `Content-Length: 0` on the empty POST for unambiguous framing.
- A successful `/Response/{guid}` reply may have an empty body. Empty successful response bodies mean "no logical message" and are not delivered to `OnReadString`. Because a socket server already has the complete reply body, it should normally frame an empty reply with `Content-Length: 0`.
- Use plain HTTP without authentication, cookies or `Content-Encoding`. The Windows transport decodes every nonempty successful body directly as UTF-8 and does not decompress it first.
- The dedicated wrapper never constructs binary content or trailers and always gives `/Response/{guid}` a fixed-length request body. Its client-side HTTP parser must nevertheless accept ordinary legal response framing produced by HTTP.sys; `Content-Length` is not a validation requirement for responses.

### Routes

| Purpose | HTTP request | Successful HTTP response | Logical effect |
| --- | --- | --- | --- |
| Connect | `GET {baseUrl}/VlppInterProcess/Connect`, empty body | `200`, exact legacy content type, UTF-8 body `/VlppInterProcess/Request/{guid};/VlppInterProcess/Response/{guid}` | Create one GUID-scoped logical connection. |
| Receive | `POST {baseUrl}/VlppInterProcess/Request/{guid}`, empty body | Held until possible, then `200`, exact legacy content type and zero or one UTF-8 message | Long poll for one server-to-client message. |
| Send | `POST {baseUrl}/VlppInterProcess/Response/{guid}`, exact legacy content type and one nonempty fixed-length UTF-8 message | Immediate `200`, exact legacy content type and zero or one UTF-8 message | Deliver one client-to-server message; optionally piggyback one server-to-client message. |

The two paths returned by `/Connect` intentionally exclude `{baseUrl}`. For example, with `{baseUrl}` equal to `/Demo`, a successful body could be:

```text
/VlppInterProcess/Request/01234567-89ab-cdef-0123-456789abcdef;/VlppInterProcess/Response/01234567-89ab-cdef-0123-456789abcdef
```

The client splits at the first semicolon and prepends `/Demo` to both paths. The GUID and returned path components should otherwise be treated as opaque.

An abridged message exchange is:

```http
GET /Demo/VlppInterProcess/Connect HTTP/1.1
Host: localhost:8765
Accept: application/json; charset=utf8

HTTP/1.1 200 OK
Content-Type: application/json; charset=utf8

/VlppInterProcess/Request/{guid};/VlppInterProcess/Response/{guid}
```

After connecting, the receive lane maintains:

```http
POST /Demo/VlppInterProcess/Request/{guid} HTTP/1.1
Host: localhost:8765
Accept: application/json; charset=utf8
Content-Length: 0

```

While that exchange remains pending, sending the four-byte ASCII message `ping` uses another HTTP exchange:

```http
POST /Demo/VlppInterProcess/Response/{guid} HTTP/1.1
Host: localhost:8765
Accept: application/json; charset=utf8
Content-Type: application/json; charset=utf8
Content-Length: 4

ping
```

The response to either POST carries at most one direct UTF-8 server message. It must not contain a JSON string such as `"pong"` unless those quote characters are intentionally part of the logical message.

### Logical Connection and Physical Connections

The GUID identifies the logical `INetworkProtocolConnection`; a TCP connection does not. The connect request, pending long poll and message submissions may all use different persistent or newly established TCP connections. Closing one physical connection must not automatically delete the GUID state.

```text
logical connection {guid}
|- receive lane: one pending POST /Request/{guid}
`- send lane: sequential POST /Response/{guid} exchanges (portable adapter)
```

This separation is required because an HTTP/1.1 connection with one in-flight exchange cannot submit `/Response/{guid}` while its `/Request/{guid}` response is still pending. The portable socket-backed client therefore composes two `SocketHttpClientApi` instances: one for the long poll and one reusable control/send connection. Serializing that send connection is the adapter policy for deterministic submission order; the existing Windows client instead starts independent asynchronous `/Response/{guid}` requests and does not provide this ordering guarantee. A larger connection pool is possible, but unbounded parallel sends are unnecessary.

A socket-backed server must keep the GUID-to-logical-connection map above all accepted `IHttpRequestConnection` objects. A pending long poll also retains the particular HTTP connection/request context on which its eventual response must be sent.

The receive and send lanes complete independently. The legacy transport defines no total callback order between a long-poll body, a piggybacked `/Response/{guid}` body and concurrently issued `/Response/{guid}` operations. Callbacks can also run on different threads. Higher layers must synchronize their state and must not infer a cross-lane ordering that is absent from the wire protocol.

There is no HTTP disconnect route. `HttpServerConnection::Stop` removes the GUID and cancels its pending long poll; stopping the whole server does the same for every GUID. Merely observing EOF on a physical HTTP connection is insufficient to remove the logical connection. The current protocol has no heartbeat or wire-level reclamation of an abandoned GUID.

### Compatible Client Behavior

1. `WaitForServer` sends `GET /Connect` and waits for its completion. A valid success contains the two semicolon-separated relative paths. Only then does the client enter the connected state and call `OnConnected`.
2. `BeginReadingLoopUnsafe` starts one `/Request/{guid}` long poll with an infinite receive timeout. After every successful reply, start the replacement long poll before delivering a nonempty body to `OnReadString`; this avoids a receive gap while application code handles the message.
3. `SendString` encodes one nonempty string and submits it through `/Response/{guid}`. A nonempty body in the HTTP response is another server-to-client message and is also delivered to `OnReadString`.
4. The portable adapter serializes the send lane through one `SocketHttpClientApi`. This deliberately adds deterministic client submission order while the independent receive lane remains pending; it is not behavior guaranteed by the old WinHTTP implementation.
5. `Stop` cancels the long poll and reports disconnection. The Windows implementation allows already-started `/Response` submissions to finish during shutdown; this is unrelated to the HTTP `keep-alive` header.

For the existing Windows client, a transport error, any status other than `200`, or any successful response whose content type is not the exact legacy value is a failed attempt:

| Operation | Retry behavior |
| --- | --- |
| `/Connect` | At most three immediate attempts. Attempts one and two report nonfatal local errors; attempt three is fatal and stops the client. |
| `/Request/{guid}` | Retry immediately and indefinitely while running, without reporting a local error. |
| `/Response/{guid}` | Retry the same body for at most three immediate attempts. Attempts one and two report nonfatal local errors; attempt three is fatal and stops the client. |

The legacy helper defaults to infinite name-resolution timeout, 60 seconds for connecting and 30 seconds each for sending and receiving. `/Request/{guid}` overrides the receive timeout to infinite. A new implementation may centralize timeout policy, but the long poll must not be subjected to the ordinary response timeout.

There is no retry delay, message identifier, acknowledgement identifier or deduplication. If the server processes a `/Response/{guid}` request but its HTTP response is lost, retrying can deliver the client message more than once. Likewise, a server message has no application-level acknowledgement after it is placed in a successful long-poll response. This transport is not exactly-once.

### Compatible Server Behavior

#### `GET /Connect`

Create a fresh GUID and logical connection, add it to the shared map and call `OnClientConnected`. If accepted, reply with the canonical pair of relative paths. If rejected, remove the GUID and reply `404 Connection rejected`. The implementation creates a new logical connection for every accepted call; it does not enforce the old source comment claiming that `/Connect` may be called only once. A retry whose previous successful response was lost can therefore leave an abandoned logical connection.

#### `POST /Request/{guid}`

Look up the GUID and retain this HTTP exchange as the connection's one pending long poll. If an outbound string is already queued, remove the oldest string and respond immediately with it. Otherwise, the application layer leaves the request pending without setting a finite timeout.

There may be only one pending long poll per GUID. If a newer `/Request/{guid}` arrives, cancel the older HTTP exchange and retain the newer one. The portable one-in-flight HTTP helper cancels it by stopping the old poll's physical connection. A well-behaved client normally avoids this overlap, but replacement is part of the Windows server behavior.

When application code calls `SendString` outside inbound `/Response` dispatch, immediately satisfy the pending long poll if one exists. If that response cannot be sent because the physical connection was closed or aborted, clear the stale poll and put the string back in the queue. If no poll exists, queue the string FIFO until a later poll.

#### `POST /Response/{guid}`

Validate the fixed-length body convention, decode the complete body as one UTF-8 string and deliver it once through `OnReadString`. If no callback is installed yet, queue the inbound string and replay it after callback installation.

Application code may synchronously call `SendString` while handling this inbound message. Collect such strings instead of completing the independent long poll: return the first one as this POST's HTTP response and queue the remainder. If the callback produces no immediate string, one already queued outbound string may be returned instead. The HTTP response is still successful and uses the exact legacy content type when it has no body.

Unknown GUIDs, wrong methods and unknown paths receive `404`; a server that is stopping also answers new work with `404`. Error bodies and content types are not part of the client contract because every non-`200` response is treated as failure.

### CORS Behavior Exposed by the Windows Server

The Windows client does not use CORS, but matching the complete Windows server behavior is useful for browser callers. Every ordinary response, including errors, contains:

```http
Access-Control-Allow-Origin: *
```

An `OPTIONS` request under the registered URL prefix is handled before route dispatch and returns `200` with:

```http
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type
```

### Sources of Truth

- Route constants and intent: [NetworkProtocolHttp.h](Source/InterProcess/NetworkProtocolHttp.h), completed by the common MiniHttp refactor
- Windows client state machine, validation and retries: [HttpClient.Windows.cpp](Source/InterProcess/Windows/HttpClient.Windows.cpp)
- Windows server routing, queues and piggybacking: [HttpServer.Windows.cpp](Source/InterProcess/Windows/HttpServer.Windows.cpp)
- Windows HTTP body and response helpers: [HttpClientApi.Windows.cpp](Source/InterProcess/Windows/HttpClientApi.Windows.cpp) and [HttpServerApi.Windows.cpp](Source/InterProcess/Windows/HttpServerApi.Windows.cpp)
- Knowledge-base summary: [KB_VlppOS_InterProcessNetworkProtocolsAndChannels.md](.github/KnowledgeBase/KB_VlppOS_InterProcessNetworkProtocolsAndChannels.md)
