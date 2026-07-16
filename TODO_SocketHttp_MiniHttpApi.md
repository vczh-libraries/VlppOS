# SocketHttp(Server|Client)Api based on HttpRequest(Server|Client)

Implement the public APIs in `Source/InterProcess/AsyncSocket/AsyncSocket_HttpServerApi.h/.cpp` and `Source/InterProcess/AsyncSocket/AsyncSocket_HttpClientApi.h/.cpp`, using namespace `vl::inter_process::async_tcp_socket`.

This is a minimized HTTP/1.1 application layer for loopback inter-process use. The main public class names are `SocketHttpServerApi` and `SocketHttpClientApi`. They should be as easy to use as `vl::inter_process::windows_http::HttpServerApi` and `vl::inter_process::windows_http::HttpClientApi`, while reusing the completed async-socket HTTP request layer.

## Verified design constraints

- A server API is identified by an absolute URL prefix, not by a caller-supplied socket object. Multiple `SocketHttpServerApi` instances for different prefixes on the same port share one internal `Ptr<HttpRequestServer>` and therefore one native listener.
- A client API does not need sharing. Its constructor receives a `Ptr<IAsyncSocketClient>` and constructs and owns one `HttpRequestClient` from it.
- Prefix dispatch happens for every completed HTTP request, not when a TCP connection is accepted. One persistent connection may request one prefix and then another prefix in the next sequential exchange.
- The product API contains no document-root, file-to-content, suffix-to-MIME, upload, or download abstraction. A caller constructs response bytes and supplies status, reason, fields, and content metadata in code.
- Browser-facing CORS, `OPTIONS`, error, framing, deferred-response, and shutdown behavior remains product behavior because it is required by GacJS and the later `INetworkProtocol*` compatibility adapter.

## Public class definitions

The exact private layout may change during implementation, but the public surface should follow this design:

```cpp
namespace vl::inter_process::async_tcp_socket
{
	class SocketHttpServerApi;
	class SocketHttpServerApiDispatcher;

	class SocketHttpRequestContext : public Object
	{
		friend class SocketHttpServerApiDispatcher;

		class Impl;
		Ptr<Impl>							impl;

		SocketHttpRequestContext(Ptr<Impl> _impl);

	public:
		~SocketHttpRequestContext();

		SocketHttpRequestContext(const SocketHttpRequestContext&) = delete;
		SocketHttpRequestContext(SocketHttpRequestContext&&) = delete;
		SocketHttpRequestContext& operator=(const SocketHttpRequestContext&) = delete;
		SocketHttpRequestContext& operator=(SocketHttpRequestContext&&) = delete;

		Ptr<HttpRequest>					GetRequest();
		WString								GetRelativePath();
		WString								GetQuery();

		bool								Respond(
			Ptr<HttpResponse> response,
			Func<void(bool)> completion = {}
			);
		bool								Cancel();
	};

	class SocketHttpServerApi : public Object
	{
		class Impl;
		Ptr<Impl>							impl;

	protected:
		virtual void						OnHttpRequestReceived(
			Ptr<SocketHttpRequestContext> context
			) = 0;
		virtual void						OnHttpServerStopping();

	public:
		SocketHttpServerApi(
			const WString& urlPrefix,
			bool respondToOptions
			);
		virtual ~SocketHttpServerApi();

		SocketHttpServerApi(const SocketHttpServerApi&) = delete;
		SocketHttpServerApi(SocketHttpServerApi&&) = delete;
		SocketHttpServerApi& operator=(const SocketHttpServerApi&) = delete;
		SocketHttpServerApi& operator=(SocketHttpServerApi&&) = delete;

		void								Start();
		void								Stop();
		bool								IsStopped();
		WString								GetUrlPrefix();
	};

	class SocketHttpClientApi : public Object
	{
		class Impl;
		Ptr<Impl>							impl;

	public:
		SocketHttpClientApi(
			Ptr<IAsyncSocketClient> client,
			const WString& authority
			);
		~SocketHttpClientApi();

		SocketHttpClientApi(const SocketHttpClientApi&) = delete;
		SocketHttpClientApi(SocketHttpClientApi&&) = delete;
		SocketHttpClientApi& operator=(const SocketHttpClientApi&) = delete;
		SocketHttpClientApi& operator=(SocketHttpClientApi&&) = delete;

		void								WaitForServer();
		ClientStatus						GetStatus();
		void								HttpQuery(
			const windows_http::HttpRequest& request,
			Func<void(Variant<
				windows_http::HttpResponse,
				windows_http::HttpError
				>)> callback
			);
		void								Stop();

		static WString						UrlEncodeQuery(const WString& query);
		static WString						UrlDecodeQuery(const WString& query);
	};
}
```

`SocketHttpRequestContext` is created only by the server implementation; callers cannot default-construct, copy, or move it. `GetRequest` returns the completed byte-oriented `async_tcp_socket::HttpRequest`, preserving the exact request target, ordered fields, chunks, and trailers. Prefix and request paths are percent-decoded as UTF-8 after rejecting NUL and encoded `/` or `\`; `GetRelativePath` returns that decoded path as `/` for an exact prefix request or `/...` for a descendant. `GetQuery` returns the raw query substring without the leading `?`. These conveniences must not discard or rewrite the exact raw target.

`Respond` is one-shot. It returns `true` only when it wins the transition from pending to sending. Its optional completion callback runs exactly once with `true` only after the matching `IHttpRequestCallback::OnWriteCompleted`; write failure, disconnect, cancellation, or stop reports `false`. `Cancel` returns `true` only when cancellation wins and closes that physical connection so the peer cannot wait forever or reuse an abandoned exchange. A context may be retained and completed from another thread.

The server response remains an `async_tcp_socket::HttpResponse`. This lets callers directly provide status, reason, ordered fields, body chunks, and `Content-Type` without any filesystem policy. `Respond` flattens supplied data chunks into one fixed-length body, rejects trailers and application `Transfer-Encoding`, and rejects a contradictory `Content-Length`; it never generates chunked framing. Small byte/UTF-8 construction helpers may be added if they make the common case as convenient as `windows_http::HttpServerApi::SendResponse` and `SendResponseUtf8`, but byte APIs remain authoritative.

Every most-derived `SocketHttpServerApi` destructor must call `Stop` before destroying fields that `OnHttpRequestReceived`, response completion, or `OnHttpServerStopping` can access. The base destructor performs only a final safety stop; it is too late to invoke derived callbacks safely.

The client authority is explicit, for example `localhost:8888`, because `IAsyncSocketClient` does not expose the host or port needed for the HTTP/1.1 `Host` field. `SocketHttpClientApi` owns its `HttpRequestClient`, serializes accepted `HttpQuery` calls in FIFO order on its one physical connection, and permits a callback to enqueue the next query reentrantly. A future long-poll transport uses two client API instances so one infinite request does not block the send lane.

## Shared server and URL prefix contract

`SocketHttpServerApi` mirrors the Windows server constructor: callers provide only the URL prefix and `respondToOptions`. Its implementation parses the port and uses a platform-guarded private factory to construct `windows_socket::AsyncSocketServer`, `linux_socket::AsyncSocketServer`, or `macos_socket::AsyncSocketServer`. Platform headers stay out of the public common header.

Normalize and validate prefixes as follows:

- Require plain `http://`, a loopback authority with an explicit port in `1..65535`, and an optional absolute path. Reject credentials, query, and fragment components. HTTPS is outside scope.
- Remove a trailing `/` from the offered prefix. Repeated trailing slashes normalize to no trailing slash; `http://localhost:8888/` therefore becomes the root prefix `http://localhost:8888`.
- Match the request `Host` authority and path. Exclude the query from prefix matching and compare paths case-sensitively.
- A path prefix `/PREFIX` reacts to exactly `/PREFIX` and `/PREFIX/...`. It never reacts to `/PREFIX2`, `/PREFIX-suffix`, or another non-segment continuation.
- The root prefix reacts to all origin-form paths for that authority.
- Reject duplicate normalized prefixes. Overlapping prefixes are allowed, and the longest matching prefix wins. This decision is made again for every request on a persistent connection.
- A request that matches no active prefix receives `404 Not Found`. Stopping one API removes only its prefix; other APIs sharing the port continue to work.

Use a private process-wide registry keyed by port. Each entry owns one specialized `Ptr<HttpRequestServer>` multiplexer and the active prefix registrations. The global `SpinLock` protects only registry lookup, insertion, removal, and reference bookkeeping. Never create, bind, start, stop, drain, or invoke callbacks while holding that lock.

Starting the first API on a port installs a reference-counted in-progress registry entry, releases the registry lock, and creates and starts the native listener. Other starters retain that entry and wait on its completion signal outside the `SpinLock`; waiting for the same in-process creation does not consume a bind attempt. After an address-in-use failure, recheck the registry: join a successfully published server or retry with a fresh in-progress generation, for at most five actual bind attempts total. This handoff prevents a loser from exhausting five immediate retries before the winner publishes. Unrelated port occupation remains a real failure after the final attempt.

`Stop` is idempotent and hard-draining. It unregisters the API prefix, cancels and drains contexts dispatched to that API, and calls `OnHttpServerStopping` without registry or dispatcher locks held. The final API removes the registry entry under the lock and then stops and drains the shared `HttpRequestServer` outside the lock. Add the smallest terminal-listener notification seam to the request/server layer so an unexpected native listener failure marks and drains every API attached to that shared entry; `IsStopped` polling alone is not a sufficient callback-lifetime boundary.

The specialized shared server installs one callback on each accepted `IHttpRequestConnection`. That callback parses sequential requests and selects an API for each request. Binding a whole connection to the prefix selected by its first request is incorrect.

## Common HTTP value-type refactor

The existing `windows_http::HttpRequest`, `windows_http::HttpResponse`, and `windows_http::HttpError` are sufficiently portable for `SocketHttpClientApi::HttpQuery` with a small refactor:

- Move their declarations from `Source/InterProcess/Windows/HttpClientApi.Windows.h` to new `Source/InterProcess/NetworkProtocolHttp.h` while preserving the `vl::inter_process::windows_http` namespace and existing qualified names.
- Change `HttpError::errorCode` from Windows-only `DWORD` to `vuint32_t`.
- Move `HttpRequest::SetBodyUtf8` and `HttpResponse::GetBodyUtf8` from Windows API calls to a platform-neutral implementation in `NetworkProtocolHttp.cpp` or equivalent common code.
- Share the URL encode/decode implementation between the Windows and socket client APIs instead of duplicating policy.
- Move `HttpServerUrl_Connect`, `HttpServerUrl_Request`, and `HttpServerUrl_Response` unchanged from `Source/InterProcess/Windows/NetworkProtocol.Windows.h` to `Source/InterProcess/NetworkProtocolHttp.h`.
- Make `NetworkProtocol.Windows.h` include `../NetworkProtocolHttp.h` and remove its copies of the three constants. This common header must not include Windows, Linux, or macOS implementation headers.

The reused request type contains WinHTTP-shaped options. `SocketHttpClientApi` supports method, query, body, content type, accept types, cookie/extra fields, and the response deadline. It rejects `secure = true`, nonempty credentials, and `keepAliveOnStop = true`; hard-draining `Stop` must remain bounded by cancellation rather than waiting indefinitely for an in-flight request. Name-resolution, connection, and send phase timeouts remain owned by the injected socket implementation; only the response timeout maps to a socket HTTP exchange. The reused response intentionally retains the existing Windows-visible status/body/cookie/content-type shape; exact raw ordered response fields remain available only in the lower request layer.

## HTTP request and response behavior

- Support `GET`, `HEAD`, `POST`, and `OPTIONS`. The selected API handles these methods and must answer an unsupported operation with `405 Method Not Allowed` and `Allow`; a method not implemented anywhere receives `501 Not Implemented` before application dispatch.
- Require exactly one valid `Host` field on HTTP/1.1 requests. Accept normal and future browser extension fields; field names are ASCII case-insensitive.
- Accept origin-form targets and `OPTIONS *`. Preserve the raw target and raw query, decode prefix and request paths as UTF-8, and reject invalid UTF-8, NUL, and encoded `/` or `\` before prefix comparison.
- Treat request and response bodies as bytes. UTF-8 conversion is a convenience, not the storage model.
- Produce one final response per request unless the application explicitly cancels its context. Flatten response chunks into one body, reject response trailers and `Transfer-Encoding`, and use exact fixed `Content-Length` when content is permitted.
- `HEAD` sends the same status and representation fields as `GET` but no body. `204` and `304` never transmit a body.
- Add `Date`, `Cache-Control: no-store`, and `Access-Control-Allow-Origin: *` when application metadata does not already provide an equivalent valid value. Reject contradictory framing fields instead of emitting an invalid response.
- Keep connections persistent for sequential exchanges and honor `Connection: close` after the current response. HTTP pipelining is not supported, but multiple physical connections are accepted concurrently.
- Header/body receive timeouts apply only while an incomplete request is being parsed. A retained response context has the application's deadline; the later GacUI long poll has no ordinary response timeout.
- Invoke no application, completion, cancellation, disconnect, or stopping callback while an internal registry, prefix, connection, or context lock is held.

## CORS and `OPTIONS`

The browser requirement sometimes called “XSS” here is CORS: different localhost ports are different origins. Use one permissive, non-credentialed loopback policy.

When `respondToOptions` is true, handle a browser preflight before `OnHttpRequestReceived`. A supported preflight returns `200` with `Content-Length: 0` or a legal bodyless `204`, and includes:

```http
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, HEAD, POST, OPTIONS
Access-Control-Allow-Headers: Accept, Content-Type
Allow: GET, HEAD, POST, OPTIONS
```

Reject unsupported requested methods or fields with a non-2xx response. Do not send `Access-Control-Allow-Credentials`, and do not implement credentials, `Authorization`, `Access-Control-Max-Age`, or `Access-Control-Expose-Headers` automatically. An `OPTIONS` request without `Access-Control-Request-Method` is ordinary HTTP. An origin-form target follows the selected prefix API's `respondToOptions` setting. Because `OPTIONS *` has no prefix, the shared server answers it automatically when any active API for the matching authority has `respondToOptions = true`; if none does, return `501 Not Implemented`.

## Errors and unsupported features

Return an HTTP response whenever parsing progressed far enough to write one safely. Use `400`, `408`, `413`, `414`, `415`, `417`, `431`, `500`, `501`, or `505` for the corresponding malformed request, timeout, limit, unsupported media/expectation, application, method/framing, or version condition. Include CORS after dispatch and `Connection: close` whenever the stream cannot be reused. If the peer is already gone or a fatal framing error makes a response unsafe, close without reporting successful delivery.

Do not implement TLS/HTTPS, HTTP/2 or HTTP/3, redirects, proxy behavior, automatic authentication, multipart forms, directory listings, WebSocket, Server-Sent Events, compression, response chunk generation, trailers, ranges, conditional caching, server-side scripting, or any file access in these APIs.

The standalone physical-folder and browser scenario belongs to [TODO_Task_MiniHttpApi_TestApp.md](./TODO_Task_MiniHttpApi_TestApp.md). The later GacUI `INetworkProtocol*` adapter remains in [TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md](./TODO_SocketHttp_MiniHttpApi_NetworkProtocol.md).
