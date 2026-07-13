# Http Service on TCP Socket

Implements:
- socket api layer
- text network protocol on socket api layer
- http api layer on socket api layer
- text network protocol on http api layer on socket api layer
  - compatible with windows http api implementation

Files are based in `Source/InterProcess`.

## Refactoring for Preparation

Http(Server|Client)(Api)?.Windows.(h|cpp) moved from `Windows` to `Windows/HTTP` folder, using `vl::inter_process::windows_http` namespace.
NamedPipe.Windows.(h|cpp) using `vl::inter_process::named_pipe` namespace.

## IAsyncSocket(Server|Client)

Interface in `AsyncSocket/AsyncSocket.h`
Implementations in `(Windows|Linux|macOS)/AsyncSocket.(windows|linux|macos).(h|cpp)`
Using namespace `vl::inter_process::async_tcp_socket(::(windows|linux|macos)_socket)?`

- Binary async-only interface implemented in:
  - Windows
  - Linux
  - macOS
  - unit test
- Focus on async binary data accessing, pattern like `read_some`, which it push data to users, users can't request for a specific length.
- Connect to current machine (127.0.0.1) only with user-specified port

### Interface proposal:

The design is similar to `INetworkProtocol(Server|Client|Connection|Callback)`

```C++
class AsyncSocketBuffer : public Object
{
public:
	collections::Array<vuint8_t>                data;
};

class IAsyncSocketConnection;

class IAsyncSocketCallback : public virtual Interface
{
public:
	// The buffer is borrowed and is valid only during this callback.
	// One callback represents one arbitrary read_some result.
	virtual void                                OnRead(const vuint8_t* buffer, vint size) = 0;

	// Called after the complete buffer passed to WriteAsync has been sent.
	virtual void                                OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer) {}

	virtual void                                OnError(const WString& error, bool fatal) {}
	virtual void                                OnConnected() {}
	virtual void                                OnDisconnected() {}
	virtual void                                OnInstalled(IAsyncSocketConnection* connection) = 0;
};

class IAsyncSocketConnection : public virtual Interface
{
public:
	virtual void                                InstallCallback(IAsyncSocketCallback* callback) = 0;

	// Continuously perform read_some and deliver every result through OnRead.
	virtual void                                BeginReadingLoopUnsafe() = 0;

	// Send the whole buffer in order. The implementation handles partial OS writes.
	virtual void                                WriteAsync(Ptr<AsyncSocketBuffer> buffer) = 0;

	virtual void                                Stop() = 0;
};

class IAsyncSocketClient : public virtual Interface
{
public:
	virtual IAsyncSocketConnection*             GetConnection() = 0;
	virtual void                                WaitForServer() = 0;
	virtual ClientStatus                        GetStatus() = 0;
};

class IAsyncSocketServer : public virtual Interface
{
public:
	virtual WaitForClientResult                 OnClientConnected(IAsyncSocketConnection* connection) = 0;
	virtual void                                Start() = 0;
	virtual void                                Stop() = 0;
	virtual bool                                IsStopped() = 0;
};
```

The connection is an ordered full-duplex byte stream. It deliberately does not expose packets, TCP segments or a requested read length.

- `BeginReadingLoopUnsafe` starts a continuous callback-driven `read_some` loop, matching `INetworkProtocolConnection`. After it is called, the implementation keeps exactly one read outstanding and schedules the next read after the current `OnRead` callback returns. The consumer cannot request a byte count, pause between protocol states or decide what one callback contains.
- `OnRead` may contain any positive number of bytes chosen by the implementation. It can split or combine protocol elements arbitrarily. The callback must consume the bytes or copy any required remainder before returning.
- Only one write may be outstanding. `WriteAsync` retains the supplied `Ptr<AsyncSocketBuffer>` until `OnWriteCompleted`, sends all bytes in order, and hides platform-specific partial writes. A user that has multiple buffers maintains its own write queue and submits the next buffer from its state machine.
- One read and one write may be outstanding simultaneously. Callbacks can run on any thread, so the callback and connection state must be thread-safe.
- `OnDisconnected` represents EOF or loss of the peer. A fatal `OnError` is followed by disconnection. A nonfatal error leaves the connection usable according to the operation-specific contract.
- `InstallCallback` accepts one callback and uses `nullptr` to uninstall it, matching `INetworkProtocolConnection`.
- `Stop` is the shutdown boundary. It cancels pending operations and waits until callbacks that could access the connection owner have finished before returning.

This small contract is sufficient for the buffered state machine described in `Efficient TCP Socket Async Reading`: the connection issues efficient block reads continuously, while the user-owned state machine preserves surplus bytes and parses as much as possible whenever `OnRead` pushes another arbitrary block. Because the next read is scheduled only after the callback returns, callback execution provides basic backpressure without exposing read scheduling to the consumer.

## IHttpRequest(Server|Client) on IAsyncSocket(Server|Client)

Cross platform request parser/constructor
Interface in `AsyncSocket/HttpRequest.h`
Implementations in `AsyncSocket/Http(Server|Client).(h|cpp)`
Using namespace `vl::inter_process::async_tcp_socket`

### HTTP Request Data Structure:

```C++
struct HttpVersion
{
	vint                                        major = 1;
	vint                                        minor = 1;
};

struct HttpField
{
	// Field names are ASCII and case-insensitive. Store them in lowercase.
	WString                                     name;

	// Field values are HTTP octets, not Unicode text in general.
	collections::Array<vuint8_t>                value;
};

struct HttpBodyChunk
{
	// Chunk framing is removed. This array contains only chunk data.
	collections::Array<vuint8_t>                data;
};

struct HttpBody
{
	collections::List<HttpBodyChunk>            chunks;
	collections::List<HttpField>                trailers;
};

class HttpRequest : public Object
{
public:
	HttpVersion                                 version;
	WString                                     method;
	WString                                     requestTarget;
	collections::List<HttpField>                headers;
	HttpBody                                    body;
};
```

`HttpRequest` is a parsed, buffered HTTP/1.1 request containing only transmitted message data. Connection state, socket objects, TLS state, timeouts, keep-alive policy, response callbacks and routing behavior do not belong in it.

- `method` keeps the original case and is not an enum, because extension methods are allowed.
- `requestTarget` keeps the exact percent-encoded request target. It is not limited to a path and query because origin-form, absolute-form, authority-form and asterisk-form are all possible.
- `headers` is an ordered list instead of a dictionary. Repeated field lines are possible, and the order of repeated values can be significant.
- Header names and other strictly ASCII protocol elements use `WString` after validation. A generic field value uses `Array<vuint8_t>` because bytes `0x80` through `0xFF` have no universal Unicode interpretation.
- A request without a body has no chunks. A fixed-length body is stored as one chunk. A chunked body is stored as one item per HTTP chunk. Socket-read boundaries are never represented as body chunks.
- `HttpBodyChunk::data` contains arbitrary binary data. Chunk size lines and framing CRLFs are not stored.
- Trailer fields are stored separately from header fields. They appear only after the zero-sized last chunk and are not part of the body data.
- `Content-Encoding`, such as gzip, is not decoded here. The body contains the representation bytes after HTTP transfer framing has been removed.

The structure is move-only because Vlpp `Array` and `List` are move-only. An asynchronous interface should transfer it with `HttpRequest&&` or share an immutable completed request with `Ptr<HttpRequest>`.

### HTTP Request Body Parsing:

```C++
enum class HttpRequestBodyParsingResult
{
	Succeeded,
	Incomplete,
	Invalid,
};

HttpRequestBodyParsingResult ParseHttpRequestBodyToChunks(
	const vuint8_t*                            buffer,
	vint                                       availableBytes,
	HttpBody&                                  output,
	vint&                                      consumedBytes
	);
```

The request-line and headers must be parsed before selecting a body parser:

- Without `Transfer-Encoding` or `Content-Length`, construct an empty `HttpBody`.
- With a valid `Content-Length: N`, wait for exactly `N` bytes and store them as one `HttpBodyChunk`. `N == 0` produces no chunks.
- With `Transfer-Encoding` whose final coding is `chunked`, call `ParseHttpRequestBodyToChunks` on the buffered bytes beginning immediately after the header section.

`ParseHttpRequestBodyToChunks` is a helper used by the cross-platform HTTP request parser, not something application code should normally call. Its contract is:

- `Succeeded`: `output` contains all nonzero chunks and trailer fields. `consumedBytes` is the exact encoded size through the final empty line after the trailers. Bytes after that position belong to a possible next request and must remain buffered.
- `Incomplete`: more socket data is required. The caller preserves the buffered bytes and retries after another read.
- `Invalid`: the chunk-size line, chunk-data terminator or trailer section is malformed, overflows an integer, or violates configured limits. The request must be rejected.

The function repeatedly parses a hexadecimal chunk size, copies exactly that many data bytes, and requires the following `\r\n`. A zero size ends the data chunks; field lines after it are parsed into `trailers` until the final empty line. Chunk extensions should be validated and ignored by the initial implementation. They are transfer metadata and are not required to access the body.

For example, the encoded body:

```text
7\r\nHello, \r\n6\r\nworld!\r\n0\r\nDigest: value\r\n\r\n
```

produces two chunks containing `Hello, ` and `world!`, plus one `Digest` trailer. The size lines, zero chunk and framing CRLFs are consumed but do not appear in `HttpBody`.

### Interface proposal:

The design is similar to `INetworkProtocol(Server|Client|Connection|Callback)`

```C++
class HttpResponse : public Object
{
public:
	HttpVersion                                 version;
	vint                                        statusCode = 200;
	WString                                     reason;
	collections::List<HttpField>                headers;
	HttpBody                                    body;
};

class IHttpRequestConnection;

class IHttpRequestCallback : public virtual Interface
{
public:
	// A server-side connection receives requests.
	virtual void                                OnReadRequest(Ptr<HttpRequest> request) {}

	// A client-side connection receives responses.
	virtual void                                OnReadResponse(Ptr<HttpResponse> response) {}

	// Completes the single SendRequest or SendResponse currently in progress.
	virtual void                                OnWriteCompleted() {}

	virtual void                                OnError(const WString& error, bool fatal) {}
	virtual void                                OnConnected() {}
	virtual void                                OnDisconnected() {}
	virtual void                                OnInstalled(IHttpRequestConnection* connection) = 0;
};

class IHttpRequestConnection : public virtual Interface
{
public:
	virtual void                                InstallCallback(IHttpRequestCallback* callback) = 0;

	// Continuously read requests on the server or responses on the client.
	virtual void                                BeginReadingLoopUnsafe() = 0;

	// Client-side operation. Only one request/response exchange is active at a time.
	virtual void                                SendRequest(Ptr<HttpRequest> request) = 0;

	// Server-side operation. Responds to the most recently delivered request.
	virtual void                                SendResponse(Ptr<HttpResponse> response) = 0;

	virtual void                                Stop() = 0;
};

class IHttpRequestClient : public virtual Interface
{
public:
	virtual IHttpRequestConnection*             GetConnection() = 0;
	virtual void                                WaitForServer() = 0;
	virtual ClientStatus                        GetStatus() = 0;
};

class IHttpRequestServer : public virtual Interface
{
public:
	virtual WaitForClientResult                 OnClientConnected(IHttpRequestConnection* connection) = 0;
	virtual void                                Start() = 0;
	virtual void                                Stop() = 0;
	virtual bool                                IsStopped() = 0;
};
```

`IHttpRequestConnection` owns the cross-platform HTTP/1.1 state machine on one `IAsyncSocketConnection`. It receives the continuous sequence of arbitrary socket `OnRead` callbacks and turns them into complete logical HTTP messages.

- On a server connection, the continuous reading loop produces `OnReadRequest` whenever a complete request has been parsed. The server calls `SendResponse` for each delivered request.
- On a client connection, the client calls `SendRequest` and the continuous reading loop eventually produces the corresponding `OnReadResponse`. The next request is not sent until the current response body is complete.
- The initial implementation supports one in-flight request/response exchange per connection and does not support HTTP pipelining. This is sufficient for persistent sequential communication on the same machine and removes request identifiers and response reordering from the interface.
- `BeginReadingLoopUnsafe` continuously translates arbitrary socket callbacks into complete HTTP messages. When a request or response completes, surplus bytes remain buffered and are considered for the next message when the HTTP state machine permits it.
- `SendRequest` and `SendResponse` serialize the data structures, generate or validate HTTP framing, and use one or more socket writes internally. `OnWriteCompleted` means the entire HTTP message has been written.
- Chunking, `Content-Length`, header limits, body limits and trailer parsing belong to this cross-platform layer. URL routing, cookies, content decoding, authentication and application behavior remain above it.
- HTTP keep-alive does not add another socket operation. The HTTP state machine decides whether the already-running loop may continue parsing messages from the same open connection.
- `HttpResponse` is the response-side equivalent of `HttpRequest`. Its body follows the same rules: binary chunks with transfer framing removed and trailers stored separately.
- Installation, connection notification, client status, accepting/rejecting clients and shutdown intentionally follow `NetworkProtocol.h`.

## INetworkProtocol(Server|Client) on IAsyncSocket(Server|Client)

- INetworkProtocolServer / INetworkProtocolClient:
  - Text service based on socket.
  - Text block encoded in length in bytes + non-zero-terminated utf-8 string.
  - unit test (shared)

## SocketHttp(server|client) based on IHttpRequest(Server|Client)

- SocketHttpServer / SocketHttpClient
  - On Windows, use socket vs http api in unit test.
  - Test app hosts http service in two different ports
    - JS from one service calls another service
    - Test against Windows(Chrome), Ubuntu(firefox), macOS(safari)
  - Multiple server on one port share the same IHttpRequestServer.
    - A spin lock protects a global map pointer.
    - each item is refcount protected, released automatically.
    - the whole map is refcount protected, released automatically.
    - If creating socket server fails because of port is occupied:
      - server should take a look at the map again to see if one has been created.
      - if not created retry, in total 5 times.
      - creating socket server should not hold the spin lock.

## INetworkProtocol(Server|Client) based on SocketHttp(Server|Client)

- On Windows, use socket vs http api in unit test (shared)
- On any platform, use socket vs socket in unit test (shared)

## Determine the boundary of an HTTP request

TCP is an ordered byte stream and does not preserve application message boundaries. A socket read can return part of a request, multiple requests, or the end of one request plus the beginning of the next one. The HTTP parser must therefore keep a per-connection receive buffer and determine boundaries from HTTP framing, independently of socket read boundaries.

This section describes HTTP/1.1. Parse the request line and header fields as octets until the empty line (`\r\n\r\n`). Do not convert the entire incoming stream to Unicode before finding the protocol delimiters. After parsing the fields, determine the request body framing:

- If `Transfer-Encoding` is present, its final transfer coding must be `chunked` for a request.
- Otherwise, a valid `Content-Length: N` declares exactly `N` body octets.
- Otherwise, the request has no body and ends at the empty line after the header fields.

Requests are never delimited by TCP EOF. EOF or a timeout before declared framing is complete makes the request incomplete. `Connection: close` controls connection reuse after the response; it does not delimit a request body.

### Chunk Based

Chunked transfer coding allows the sender to stream a body whose total size is not known when the header section is sent. It is selected by a header whose final transfer coding is `chunked`, most commonly:

```http
Transfer-Encoding: chunked
```

A normal data chunk has this wire format:

```text
<hexadecimal-size>[;<chunk-extensions>]\r\n
<exactly size octets of chunk-data>\r\n
```

The complete grammar is conceptually:

```text
chunked-body    = *chunk last-chunk trailer-section CRLF
chunk           = chunk-size [ chunk-ext ] CRLF
                  chunk-data CRLF
chunk-size      = 1*HEXDIG
last-chunk      = 1*("0") [ chunk-ext ] CRLF
trailer-section = *( field-line CRLF )
```

The size is hexadecimal. A chunk with size `A` contains 10 data octets; a size of `10` contains 16 data octets. The size line, extensions, framing CRLFs and trailers are not included in the size.

The zero-sized `last-chunk` carries no data. It is followed by zero or more trailer fields and a final empty line. Therefore:

```text
0\r\n\r\n
```

is the common ending without trailers, while:

```text
0\r\n
Digest: value\r\n
\r\n
```

contains one trailer. The message is complete only after the final empty line, not immediately after `0\r\n`.

#### Unicode Encoding

Chunk size counts encoded octets, not Unicode scalars, code points, UTF-16 code units or characters. Encode the text first and then count the produced bytes. For example, `é` is one Unicode scalar but has the two UTF-8 bytes `C3 A9`, so a chunk containing only that character begins with `2\r\n`.

Character encoding is representation metadata and does not affect parsing of request lines, headers, chunk sizes or trailers. A textual body should declare its media type and charset when the media type requires one:

```http
Content-Type: text/plain; charset=utf-8
```

`Content-Encoding`, such as `gzip`, describes a content coding rather than a character encoding. On receive, process the layers in this order:

```text
HTTP byte stream
  -> remove chunk framing
  -> undo any earlier Transfer-Encoding codings in reverse order
  -> undo Content-Encoding codings in reverse order
  -> interpret the result using Content-Type and its charset
```

Consequently, when compressed or otherwise coded data is chunked, each chunk size counts the coded bytes literally present in that chunk, not the decoded text size. If `Content-Type` has no charset, the media type definition decides the encoding; HTTP has no universal fallback charset. If `Content-Type` is absent, the content may be treated as `application/octet-stream`.

The body charset does not apply to HTTP metadata:

- Field names and chunk-extension names are ASCII tokens.
- Field values are normally constrained to ASCII. Legacy bytes `0x80` through `0xFF` are `obs-text` and should be treated as opaque bytes, not assumed to be UTF-8.
- Chunk-extension values have no default charset and can be removed or recoded by intermediaries, so they are unsuitable for important application metadata.
- Trailer fields use the same field syntax as header fields.
- HTTP does not use UTF-7 as a general metadata encoding. For non-ASCII metadata, use the encoding defined by that specific field. Fields that explicitly support RFC 8187 can use percent-encoded UTF-8, such as `filename*=UTF-8''caf%C3%A9.txt`. A custom field should define an ASCII-safe encoding such as percent-encoded UTF-8, quoted Base64 or an unpadded base64url token.

#### Escaping of Chunk Body (including (CR)?LF)

Chunk data is not escaped. It can contain any octet, including NUL, CR, LF, `\r\n`, `0\r\n\r\n` or bytes that are not valid text. These sequences have no framing meaning while the parser is in `ChunkData` state.

After parsing a nonzero chunk size `N`, the parser must:

1. Consume exactly `N` octets without searching them for delimiters.
2. Require the next two octets to be the framing `\r\n`.
3. Parse the next chunk-size line.

For example, if the two data octets are themselves CR and LF, the encoded chunk is:

```text
2\r\n
\r\n
\r\n
```

The first CRLF terminates the size line, the second is the two-octet data, and the third terminates the chunk. Correctness comes from counting bytes, not from looking for a delimiter inside the body.

#### Binary Chunk

Binary content is transmitted directly and does not require Base64. For the five data octets:

```text
00 FF 0D 0A 41
```

the complete chunked body, including the terminal chunk, is:

```text
35 0D 0A | 00 FF 0D 0A 41 | 0D 0A | 30 0D 0A 0D 0A
```

The parts are:

```text
"5\r\n"
[00 FF 0D 0A 41]
"\r\n"
"0\r\n\r\n"
```

Even though the data contains `0D 0A`, those bytes do not terminate the chunk. Suitable request fields are:

```http
Content-Type: application/octet-stream
Transfer-Encoding: chunked
```

### Non-Chunk Based

An HTTP request is not itself a chunk. Chunking is only one possible framing for its optional body. The non-chunked HTTP/1.1 forms are:

| Form | Fields | Boundary |
| --- | --- | --- |
| No body | Neither `Transfer-Encoding` nor `Content-Length` | The empty line ending the header section |
| Fixed-length body | Valid `Content-Length: N` without `Transfer-Encoding` | Exactly `N` body octets after the header section |

`Content-Length: 0` is the fixed-length form with an empty body. If the size is known before sending the header section, `Content-Length` is simpler and generally preferred over chunked transfer coding.

Request framing is independent of method semantics. A parser must consume a framed body even if the method normally has no useful body semantics. JSON, binary data and `multipart/form-data` describe the content inside a fixed-length or chunked body; they do not provide HTTP message boundaries. In particular, multipart boundary markers do not replace `Content-Length` or chunked framing.

Treat ambiguous framing as an error to prevent request smuggling:

- Reject a request containing both `Transfer-Encoding` and `Content-Length`, then close the connection after responding.
- Reject invalid, overflowing or conflicting `Content-Length` values.
- Reject a request whose final transfer coding is not `chunked`.
- Enforce configured limits for request line, header block, body, chunk-size line, chunk extensions and trailer block.

HTTP/2 is a different protocol even though it can run over TCP. It uses length-prefixed binary frames and an `END_STREAM` flag; it does not use HTTP/1.1 chunked transfer coding. An HTTP/1.1 parser must not attempt to parse an HTTP/2 connection.

### Efficient TCP Socket Async Reading

Do not issue one asynchronous socket operation per byte. `BeginReadingLoopUnsafe` keeps one `read_some` operation outstanding per connection and pushes every completed block to `OnRead`. The consumer cannot request a size or control when the next read happens. It incrementally consumes each callback and preserves any incomplete protocol data in its own per-connection buffer.

One read completion has no protocol meaning. All of the following are valid:

- A request line or header name is split across reads.
- `\r\n\r\n`, a chunk-size line or a framing CRLF is split across reads.
- One logical chunk spans many reads.
- One read contains several chunks.
- One read contains the end of one request and part or all of the next pipelined request.

A practical buffered reader has:

- One reusable buffer, for example 8 to 32 KiB initially. Buffer size is a throughput and memory choice, not a correctness rule or TCP packet size.
- `begin` and `end` indices, or a ring/segmented buffer, so consuming data does not erase and move the remaining bytes after every parse operation.
- One serialized receive loop per connection.
- Bounded delimiter searches that remember the previous scan position, avoiding repeated scans from the beginning.
- Explicit `consume(N)` behavior that preserves all surplus bytes for the next parser state or request.

Useful logical operations are:

```text
read_until(delimiter, limit)
stream_exactly(byteCount, sink)
consume(byteCount)
```

They are implemented using repeated buffered `read_some` operations. `read_until` may receive bytes beyond its delimiter; those bytes must remain buffered rather than being discarded. `stream_exactly` should pass available spans to the application incrementally so a large body or HTTP chunk does not require an allocation of the advertised size.

Use an incremental state machine resembling:

1. `Headers`
   - Search for `\r\n\r\n` within the configured header limit.
   - Parse the request line and fields as octets.
   - Select no body, `FixedBody`, or `ChunkSizeLine` framing.
2. `FixedBody(remaining)`
   - Consume or stream `min(available, remaining)` octets.
   - Complete the request when `remaining` reaches zero.
3. `ChunkSizeLine`
   - Read a bounded CRLF-terminated line.
   - Parse hexadecimal digits before any validated chunk extension, checking for integer overflow and configured body limits.
   - Move to `ChunkData` for a nonzero size, or `Trailers` for zero.
4. `ChunkData(remaining)`
   - Consume or stream `min(available, remaining)` octets without scanning them.
   - Move to `ChunkDataCRLF` when `remaining` reaches zero.
5. `ChunkDataCRLF`
   - Require exactly `\r\n`, even if it is split across reads.
   - Return to `ChunkSizeLine`.
6. `Trailers`
   - Parse bounded field lines until an empty line.
7. `Complete`
   - Deliver the request, reset to `Headers`, and immediately parse any unconsumed bytes as the next request.

The callback-driven receive loop is conceptually:

```text
BeginReadingLoopUnsafe()

OnRead(block):
    append block to the per-connection buffer
    while parser can make progress from buffered bytes:
        consume bytes and dispatch parser events
    compact or grow the bounded buffer within configured limits
    return

the socket implementation schedules the next read_some after OnRead returns
```

Returning from `OnRead` allows the next read, so callback execution provides basic backpressure without a public read-control API. This project uses bounded, buffered requests and a small number of local connections; exceeding a configured header/body buffer limit is a fatal protocol error instead of allowing unbounded buffering. A zero-byte platform read becomes `OnDisconnected`: it is a normal connection end only when no request is partially parsed; while waiting for headers, a declared fixed body, chunk data, chunk framing or trailers, it makes the request incomplete. Header/body idle timeouts are also required to prevent a stalled local peer from retaining a connection indefinitely.

### References

- [RFC 9112: HTTP/1.1 message parsing](https://www.rfc-editor.org/rfc/rfc9112.html#section-2.2)
- [RFC 9112: HTTP/1.1 message body length](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3)
- [RFC 9112: Chunked transfer coding](https://www.rfc-editor.org/rfc/rfc9112.html#section-7.1)
- [RFC 9113: HTTP/2 message framing](https://www.rfc-editor.org/rfc/rfc9113.html#section-8.1)
- [RFC 9110: HTTP field values](https://www.rfc-editor.org/rfc/rfc9110.html#section-5.5)
- [RFC 9110: Representation data and metadata](https://www.rfc-editor.org/rfc/rfc9110.html#section-8)
- [RFC 8187: Non-ASCII HTTP field parameters](https://www.rfc-editor.org/rfc/rfc8187.html#section-3.2)
- [RFC 9293: TCP segmentation and application buffer boundaries](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.7)

## Async Socket Implementations

### Windows

<!-- Explain IOCP -->
<!-- Starting an Socket Sync, or if it forced us Async -->
<!-- Waiting for a client Async -->
<!-- Reading/Writing data Async>

### Linux

<!-- Explain io_uring -->
<!-- Starting an Socket Sync, or if it forced us Async -->
<!-- Waiting for a client Async -->
<!-- Reading/Writing data Async>

### macOS

<!-- Explain Framework.Network -->
<!-- Starting an Socket Sync, or if it forced us Async -->
<!-- Waiting for a client Async -->
<!-- Reading/Writing data Async>

## Must-Have features to let Firefox/Chrome/Safari interact with a minimized Http service

<!-- summarization of must-have features according -->

### HTTP service as RESTful API

### HTTP service hosting a website

### XSS and Options verb

### How to tell the browser if anything is not supported

<!-- one direction: can't decode gzip -->
<!-- another direction: force browser to only use unicode, so we don't need to deal with others -->

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
`- send lane: sequential POST /Response/{guid} exchanges (proposed adapter)
```

This separation is required because an HTTP/1.1 connection with one in-flight exchange cannot submit `/Response/{guid}` while its `/Request/{guid}` response is still pending. A socket-backed client should therefore compose at least two `IHttpRequestConnection` objects: one for the long poll and one reusable send connection. Serializing that send connection is the proposed adapter policy for deterministic submission order; the existing Windows client instead starts independent asynchronous `/Response/{guid}` requests and does not provide this ordering guarantee. A larger connection pool is possible, but unbounded parallel sends are unnecessary.

A socket-backed server must keep the GUID-to-logical-connection map above all accepted `IHttpRequestConnection` objects. A pending long poll also retains the particular HTTP connection/request context on which its eventual response must be sent.

The receive and send lanes complete independently. The legacy transport defines no total callback order between a long-poll body, a piggybacked `/Response/{guid}` body and concurrently issued `/Response/{guid}` operations. Callbacks can also run on different threads. Higher layers must synchronize their state and must not infer a cross-lane ordering that is absent from the wire protocol.

There is no HTTP disconnect route. `HttpServerConnection::Stop` removes the GUID and cancels its pending long poll; stopping the whole server does the same for every GUID. Merely observing EOF on a physical HTTP connection is insufficient to remove the logical connection. The current protocol has no heartbeat or wire-level reclamation of an abandoned GUID.

### Compatible Client Behavior

1. `WaitForServer` sends `GET /Connect` and waits for its completion. A valid success contains the two semicolon-separated relative paths. Only then does the client enter the connected state and call `OnConnected`.
2. `BeginReadingLoopUnsafe` starts one `/Request/{guid}` long poll with an infinite receive timeout. After every successful reply, start the replacement long poll before delivering a nonempty body to `OnReadString`; this avoids a receive gap while application code handles the message.
3. `SendString` encodes one nonempty string and submits it through `/Response/{guid}`. A nonempty body in the HTTP response is another server-to-client message and is also delivered to `OnReadString`.
4. For the new adapter, serialize the send lane built on the proposed one-in-flight `IHttpRequestConnection`. This deliberately adds deterministic client submission order while the independent receive lane remains pending; it is not behavior guaranteed by the old WinHTTP implementation.
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

There may be only one pending long poll per GUID. If a newer `/Request/{guid}` arrives, cancel the older HTTP exchange and retain the newer one. With the proposed one-in-flight HTTP interface, the compatibility layer can cancel it by stopping the old poll's physical connection. A well-behaved client normally avoids this overlap, but replacement is part of the Windows server behavior.

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

- Route constants and intent: [NetworkProtocol.Windows.h](Source/InterProcess/Windows/NetworkProtocol.Windows.h)
- Windows client state machine, validation and retries: [HttpClient.Windows.cpp](Source/InterProcess/Windows/HttpClient.Windows.cpp)
- Windows server routing, queues and piggybacking: [HttpServer.Windows.cpp](Source/InterProcess/Windows/HttpServer.Windows.cpp)
- Windows HTTP body and response helpers: [HttpClientApi.Windows.cpp](Source/InterProcess/Windows/HttpClientApi.Windows.cpp) and [HttpServerApi.Windows.cpp](Source/InterProcess/Windows/HttpServerApi.Windows.cpp)
- Knowledge-base summary: [KB_VlppOS_InterProcessNetworkProtocolsAndChannels.md](.github/KnowledgeBase/KB_VlppOS_InterProcessNetworkProtocolsAndChannels.md)
