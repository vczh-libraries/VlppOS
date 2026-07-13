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
```

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

struct HttpRequestBodyChunk
{
	// Chunk framing is removed. This array contains only chunk data.
	collections::Array<vuint8_t>                data;
};

struct HttpRequestBody
{
	collections::List<HttpRequestBodyChunk>     chunks;
	collections::List<HttpField>                trailers;
};

class HttpRequest : public Object
{
public:
	HttpVersion                                 version;
	WString                                     method;
	WString                                     requestTarget;
	collections::List<HttpField>                headers;
	HttpRequestBody                             body;
};
```

`HttpRequest` is a parsed, buffered HTTP/1.1 request containing only transmitted message data. Connection state, socket objects, TLS state, timeouts, keep-alive policy, response callbacks and routing behavior do not belong in it.

- `method` keeps the original case and is not an enum, because extension methods are allowed.
- `requestTarget` keeps the exact percent-encoded request target. It is not limited to a path and query because origin-form, absolute-form, authority-form and asterisk-form are all possible.
- `headers` is an ordered list instead of a dictionary. Repeated field lines are possible, and the order of repeated values can be significant.
- Header names and other strictly ASCII protocol elements use `WString` after validation. A generic field value uses `Array<vuint8_t>` because bytes `0x80` through `0xFF` have no universal Unicode interpretation.
- A request without a body has no chunks. A fixed-length body is stored as one chunk. A chunked body is stored as one item per HTTP chunk. Socket-read boundaries are never represented as body chunks.
- `HttpRequestBodyChunk::data` contains arbitrary binary data. Chunk size lines and framing CRLFs are not stored.
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
	HttpRequestBody&                           output,
	vint&                                      consumedBytes
	);
```

The request-line and headers must be parsed before selecting a body parser:

- Without `Transfer-Encoding` or `Content-Length`, construct an empty `HttpRequestBody`.
- With a valid `Content-Length: N`, wait for exactly `N` bytes and store them as one `HttpRequestBodyChunk`. `N == 0` produces no chunks.
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

produces two chunks containing `Hello, ` and `world!`, plus one `Digest` trailer. The size lines, zero chunk and framing CRLFs are consumed but do not appear in `HttpRequestBody`.

### Interface proposal:

The design is similar to `INetworkProtocol(Server|Client|Connection|Callback)`

```C++
```

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
- INetworkProtocol(Server|Client) based on SocketHttp(Server|Client)
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

Do not issue one asynchronous socket operation per byte. Issue `read_some` into the free portion of a reusable per-connection buffer, then let an incremental parser consume as much buffered data as possible. Only issue another socket read when the parser reports that it needs more data.

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

The receive loop is conceptually:

```text
while connection is open:
    while parser can make progress from buffered bytes:
        consume bytes and dispatch parser events
    if application backpressure prevents progress:
        wait for the consumer
    else:
        compact or grow the bounded buffer if necessary
        await socket.read_some(free buffer tail)
```

Apply backpressure by waiting for the body consumer before reading without bound. A zero-byte socket read means EOF: it is a normal connection end only when no request is partially parsed; while waiting for headers, a declared fixed body, chunk data, chunk framing or trailers, it makes the request incomplete. Header/body idle timeouts are also required to prevent slow clients from retaining a connection indefinitely.

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
