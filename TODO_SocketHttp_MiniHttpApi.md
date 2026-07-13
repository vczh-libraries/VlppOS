# SocketHttp(server|client) based on IHttpRequest(Server|Client)

Implementations in `AsyncSocket/AsyncSocket_Http(Server|Client)Api.(h|cpp)`
Using namespace `vl::inter_process::async_tcp_socket`

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

## Must-Have features to let Firefox/Chrome/Safari interact with a minimized Http service

This is an HTTP/1.1 origin server for loopback inter-process use, not a general Internet-facing web server. It must be sufficient for a browser to load a small website from one local port and call an API on another local port. That is the actual GacJS topology: the website is served from `http://localhost:8896`, while the GacUI core listens on `http://localhost:8888`.

The browser requirement sometimes called "XSS" in this task is actually **CORS**. XSS is a script-injection vulnerability; CORS is the opt-in mechanism that lets JavaScript read a response from a different origin. Different ports are different origins, so GacJS needs CORS even though both services use `localhost`.

### HTTP service as RESTful API

`SocketHttpServer` is a small request dispatcher usable for REST-style routes. The GacUI long-poll protocol built on it is stateful RPC rather than strictly RESTful, but it needs the same HTTP facilities.

- Support plain HTTP/1.1 over the loopback socket. `GET`, `HEAD`, `POST` and `OPTIONS` are the complete method set. `GET` and `HEAD` make it a usable origin server, `POST` carries API data, and `OPTIONS` handles CORS preflight. `PUT`, `DELETE`, `PATCH`, `CONNECT` and `TRACE` are outside the initial scope.
- Require exactly one valid `Host` field on an HTTP/1.1 request. Accept the normal browser-generated `Origin`, `Accept`, `Accept-Language`, `Accept-Encoding`, `Referer`, `User-Agent`, `Sec-Fetch-*`, `Priority` and other extension fields. Header names are case-insensitive. Unknown fields are ignored unless framing or connection semantics give them meaning; a request must not fail merely because a browser version added a field. Use one host spelling consistently because `localhost`, `127.0.0.1` and `[::1]` are different browser origins.
- Route using the request-target path and query while preserving the raw target for applications that need it. Browser requests use origin-form targets. Also recognize the asterisk-form target for `OPTIONS *`.
- Treat request and response bodies as bytes. UTF-8 text and JSON are convenience policies above the binary body. Generic media-type and charset comparisons are case-insensitive and tolerate parameter ordering; only the legacy GacUI compatibility wrapper requires the exact `application/json; charset=utf8` spelling.
- Return one final response for every request. Responses that permit content have an exact `Content-Length`; a response with content also has `Content-Type`. `HEAD` returns the same status and representation fields as `GET` but no body. `204` and `304` never contain a body. Generate `Date` and use `Cache-Control: no-store` for dynamic API responses.
- Keep HTTP/1.1 connections persistent for sequential exchanges and honor `Connection: close`. Only one request/response exchange is active on a connection, so HTTP pipelining is not supported. The server still accepts multiple physical connections concurrently because a long poll can remain pending while the same browser sends another request.
- The request callback must receive a one-shot response context that may be completed immediately, retained and completed later, or cancelled. It retains the request and physical connection until exactly one of those terminal actions. Cancelling without sending a final response closes that physical connection; it must not leave the peer waiting or reuse a stream whose request has no response. Deferred and cancellable responses are required by the existing `/Request/{guid}` long poll; a callback that can only return a response synchronously is insufficient. Header/body receive timeouts apply only while receiving and parsing an incomplete request. After a request is complete, its deferred-response deadline belongs to the route, and the GacUI long poll has no ordinary response timeout.
- A disconnect or `Stop` cancels all retained response contexts and drains their callbacks. Application callbacks are never invoked while an internal connection or request-map lock is held.

The minimized native client constructs `GET`, `HEAD`, `POST` and `OPTIONS` requests, sends `Host` and fixed `Content-Length` when it knows the body size, and exposes the response status, fields and raw body bytes. Its response parser accepts the legal framing forms required by the HTTP request layer, including fixed-length and chunked responses. Redirects, cookies, authentication and automatic representation decoding remain application features.

Compatibility with the existing GacUI transport additionally requires the routes documented in [Http(Server|Client).Windows request convention](TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md#httpserverclientwindowshcpp-request-convention): exact status `200` for success, the exact legacy JSON content type, bodyless connect/long-poll requests, UTF-8 message POSTs, and a deferred long-poll response. The browser client actually sends `GET` for connect, `POST` for both long-poll and message submission, and reads all successful bodies with `Response.text()`.

### HTTP service hosting a website

Static hosting is a route backed by one configured document-root folder. It serves bytes from that folder; it does not convert a file through `WString`.

- `GET /` and `HEAD /` select `index.html`. A target ending in `/` may select `index.html` in that directory. Other targets select the corresponding regular file below the document root. The query is not part of the file name and a URL fragment is never sent to the server.
- Percent-decode a path as UTF-8, convert URL `/` separators to platform paths, normalize it, and require the result to remain below the configured root. Reject invalid UTF-8, NUL, encoded separators and any `..` traversal. Root containment defines the document-root contract even for this intentionally unsecured local service.
- Missing files and directories without an index receive `404`; do not generate a directory listing. API routes take precedence over the static-file fallback.
- Serve the file's exact bytes with status `200`, `Content-Length`, `Content-Type`, `Date` and `Cache-Control: no-store`. `HEAD` sends the same fields without file bytes. Multiple asset requests may run concurrently on different connections.

The minimum suffix mapping is:

| Suffix | Content-Type |
| --- | --- |
| `.html`, `.htm` | `text/html; charset=utf-8` |
| `.css` | `text/css; charset=utf-8` |
| `.js`, `.mjs` | `text/javascript; charset=utf-8` |
| `.json`, `.map` | `application/json` (UTF-8 bytes) |
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

GacJS itself needs `.html`, `.css`, `.js`, `.json`, `.map` and `.ico`, including deeply nested JSON snapshot files. Correct JavaScript and CSS media types are important; browsers should not be expected to recover from an `application/octet-stream` response by MIME sniffing.

### CORS (not XSS) and the OPTIONS method

Use one deliberately permissive, non-credentialed CORS policy for this loopback service. Every ordinary response that reached HTTP dispatch, including `404`, `405`, `415` and `500`, contains:

```http
Access-Control-Allow-Origin: *
```

This lets browser JavaScript observe the actual status. Without this field, `fetch` reports a CORS network failure instead of exposing the HTTP error response.

A browser preflight is an `OPTIONS` request to the same target with `Origin`, `Access-Control-Request-Method`, and possibly `Access-Control-Request-Headers`. Handle it before ordinary route dispatch. The GacJS message POST uses `Content-Type: application/json; charset=utf8`; JSON is not a CORS-safelisted content type, so Firefox, Chrome and Safari preflight it. Answer a supported preflight with either `200` and `Content-Length: 0`, or `204` without `Content-Length` or `Transfer-Encoding`, and include:

```http
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, HEAD, POST, OPTIONS
Access-Control-Allow-Headers: Accept, Content-Type
Allow: GET, HEAD, POST, OPTIONS
```

These are the maximal lists for the intentionally permissive compatibility server; a route-aware dispatcher may narrow both method lists. The initial API does not support credentials, cookies, `Authorization`, or arbitrary application request fields. Therefore do not send `Access-Control-Allow-Credentials`; wildcard origin is valid only because browser requests use the default non-credentialed mode. `Access-Control-Max-Age` and `Access-Control-Expose-Headers` are unnecessary. If a preflight asks for a method or header unsupported by its target route, return a non-2xx response so the browser does not send the actual request.

An ordinary `OPTIONS` request without `Access-Control-Request-Method` is not a preflight. Answer `OPTIONS *` with the server-wide `Allow` list and answer an origin-form target with that route's `Allow` list, using an empty body in both cases.

The tested deployment keeps both the page and API on loopback origins. A public or other non-loopback website attempting to call a loopback service can additionally encounter Chrome Local Network Access permission rules; CORS headers alone do not define that broader deployment, so it is outside this minimized server's contract.

### How to tell the browser if anything is not supported

Return an HTTP response whenever the request has been parsed safely. Browser `fetch` resolves normally for HTTP error statuses, while network/framing failures and failed CORS checks reject the fetch. Error bodies are UTF-8 `text/plain; charset=utf-8` unless an application route chooses JSON, and all parseable cross-origin error responses include `Access-Control-Allow-Origin: *`. Every response after which the server will close the connection includes `Connection: close`. A size-limit response also closes the connection when unread body bytes or an incomplete header or target remain.

| Condition | Response |
| --- | --- |
| Malformed request line, fields, body framing or target | `400 Bad Request`, then close the connection |
| Missing, duplicate or invalid HTTP/1.1 `Host` | `400 Bad Request`, then close the connection |
| Request timeout | `408 Request Timeout`, then close the connection |
| Body, target or header section exceeds its configured limit | `413 Content Too Large`, `414 URI Too Long`, or `431 Request Header Fields Too Large` |
| No route or static file | `404 Not Found` |
| Implemented method is not allowed for this target | `405 Method Not Allowed` with a target-specific `Allow` field |
| Method is not implemented anywhere | `501 Not Implemented` |
| Unsupported request media type, non-UTF-8 text charset or request `Content-Encoding` | `415 Unsupported Media Type` |
| Unsupported `Expect` value | `417 Expectation Failed`, then close the connection |
| Unsupported transfer coding | `501 Not Implemented`, then close the connection |
| Unsupported HTTP version | `505 HTTP Version Not Supported`, then close the connection |
| Application failure | `500 Internal Server Error` |

Optional request preferences do not need error responses:

- Ignore browser `Accept-Encoding` and send identity bytes without `Content-Encoding`; gzip, deflate and Brotli response generation are not required. If the minimized native client talks to a different server, it requests `Accept-Encoding: identity` and reports a non-identity response coding as unsupported instead of decoding it incorrectly.
- Byte ranges are not required for HTML, CSS, JavaScript, JSON, ordinary images or fonts. Ignore `Range`, send the complete file with `200`, and optionally advertise `Accept-Ranges: none`. Seeking in large audio/video files is consequently outside scope.
- Do not generate `ETag` or `Last-Modified`; `Cache-Control: no-store` removes the need for conditional requests and `304` generation in the intended browser workflow.
- Ignore `Upgrade` and other unused extension fields. No TLS/HTTPS, HTTP/2 or HTTP/3, redirects, proxies, cookies, authentication, multipart form parsing, directory listing, WebSocket, Server-Sent Events, response chunking or trailers, content compression or server-side scripting is included.

The service supports Unicode by standardizing all application text, HTML, CSS and JavaScript on UTF-8. Binary request bodies and static files remain byte arrays. Other character encodings are rejected by UTF-8 helpers rather than guessed; the generic byte API remains available when content is not text.

References: [existing Windows server behavior](Source/InterProcess/Windows/HttpServerApi.Windows.cpp), [GacJS HTTP client](../GacJS/Gaclib/website/remote-protocol-http/src/index.ts), [GacJS website entry](../GacJS/Gaclib/website/entry/assets/index.html), [Fetch Standard CORS protocol](https://fetch.spec.whatwg.org/#cors-protocol), [RFC 9110 HTTP semantics](https://www.rfc-editor.org/rfc/rfc9110.html), [RFC 9112 HTTP/1.1](https://www.rfc-editor.org/rfc/rfc9112.html), [IANA media types](https://www.iana.org/assignments/media-types/media-types.xhtml), [Chrome Local Network Access](https://developer.chrome.com/blog/local-network-access).
