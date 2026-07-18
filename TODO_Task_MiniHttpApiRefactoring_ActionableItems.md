# Mini HTTP API refactoring actionable items

## Objective

Refactor the portable Mini HTTP Network Protocol implementation so that:

- layer 2 owns canonical HTTP analysis and byte-container operations;
- layer 3 owns convenient HTTP request/response presentation and construction;
- layer 4 contains Network Protocol policy and connection/message state only; and
- server/client and portable/Windows implementations do not repeat wire-contract information where their compatibility behavior matches.

The supporting analysis is in [TODO_Task_MiniHttpApiRefactoring.md](TODO_Task_MiniHttpApiRefactoring.md). This document records decisions and implementation work rather than repeating that reasoning.

## Decisions

1. Keep `HttpRequest` and `HttpResponse` mutable. Do not add cached framing, parsed length, or other derived members that could become stale after callers edit headers or bodies.
2. Promote the existing layer-2 framing analyzer to a public API and enrich its result with physical-field and comma-list cardinality.
3. Preserve the current strict server request contract:
   - `/Connect` and `/Request/{token}` accept no `Content-Length` or one plain-decimal `Content-Length: 0`;
   - `/Response/{token}` requires one physical `Content-Length` containing one positive decimal value;
   - transfer coding, trailers, duplicate fields, and comma-list lengths remain rejected by layer 4.
4. Preserve the current client response contract. Successful responses require status `200`, the selected exact content type, and a valid strict UTF-8 body, but may use any ordinary response framing accepted by layer 2. Do not add a `Content-Length` requirement or a new raw-query API in this refactoring.
5. Preserve binary Mini HTTP APIs. UTF-8 accessors and response builders are opt-in conveniences.
6. Preserve the two existing path policies:
   - layer 3 keeps its router decoding and trailing-prefix normalization;
   - layer 4 keeps the stricter Network Protocol `pchar` grammar, with empty/no-trailing-slash rules for the base prefix and nonempty/trailing-slash-allowed rules for returned endpoints.
   Shared UTF-8 and request-line primitives must not silently merge those policies.
7. Extend `NetworkProtocolHttp.h/.cpp` for cross-backend layer-4 facts. Do not initially create `AsyncSocket_HttpNetworkProtocolShared.h`.
8. Do not restructure the server/client state machines during the parsing-boundary refactor. Queue storage optimization happens only after behavior-preserving simplification is complete.

## Target ownership

| Layer | Owns after refactoring | Must not own |
| --- | --- | --- |
| Layer 2: `HttpRequest` | HTTP field/framing analysis, body operations, strict length-aware UTF-8 primitives, serializer-identical request-line validation | Network Protocol media type, routes, logical-message or path-prefix policy |
| Layer 3: `SocketHttp*Api` | Routing, response normalization, flat client projection, strict body conversion conveniences, byte/UTF-8 response construction | Tokens, Connect-pair syntax, retry/poll/message semantics |
| Layer 4: `SocketHttpServer` / `SocketHttpClient` | Exact Network Protocol rules, route operations, logical-message policy, connection and retry state | Decimal parsing, raw body flattening, Unicode codec implementation, request-line formula |
| Common layer 4: `NetworkProtocolHttp` | Cross-backend media type, routes, Connect-pair codec, base-path contract, common client request shapes | Async socket state or raw socket ownership |

# Phase 0: Lock compatibility behavior

Add characterization tests before deleting any current helper.

## Server request contract

- [x] Verify bodyless `/Connect` and `/Request/{token}` with no `Content-Length`.
- [x] Verify the same routes with one `Content-Length: 0`.
- [x] Reject positive, duplicate, comma-list, transfer-coded, trailer-bearing, or physically nonempty bodies on those routes.
- [x] For `/Response/{token}`, accept one positive plain-decimal length, including current leading-zero and surrounding-OWS behavior.
- [x] Reject missing, zero, duplicate, comma-list, conflicting, or transfer-coded lengths. Conflicting lengths are rejected by the canonical lower parser before layer 4 receives a request.
- [x] Reject missing, duplicate, or wrong `Content-Type`, trailers, malformed UTF-8, embedded NUL, and empty logical messages.

## Client response contract

- [x] Preserve status `200` and literal `application/json; charset=utf8` checks.
- [x] Preserve acceptance of ordinary legal fixed-length and chunked response framing.
- [x] Preserve the current flat first-content-type projection, including existing header/trailer behavior.
- [x] Preserve rejection of unsupported `Content-Encoding` in the flat client API.
- [x] Reject malformed UTF-8 and embedded NUL after successful HTTP parsing.
- [x] Preserve empty successful bodies as “no logical message.”
- [x] Preserve `/Connect` rejection for empty, malformed, or multiple-semicolon endpoint pairs.

## Configuration and path contract

- [x] Characterize empty and legal base prefixes plus trailing slash, query, fragment, backslash, raw non-ASCII, malformed escape, encoded separator, and NUL failures.
- [x] Characterize returned endpoint paths independently: empty is rejected, a trailing slash remains allowed by path validation, and invalid escaping/characters are rejected.
- [x] Verify server construction reserves a 36-character token for both POST targets.
- [x] Verify every independently observable layer-4 request-line boundary: the server's longest tokenized POST target, the client Connect GET target, and both actual returned POST targets at exactly `HttpRequestLineSizeLimit` and one byte beyond it. The server Connect GET exact boundary is not independently constructible because the longer tokenized POST gate fails first.

## State-machine contract

- [x] Keep existing FIFO, retry-count, receive-before-deliver, piggyback, callback-drain, and shutdown tests unchanged.
- [x] Keep Windows/portable interoperability tests unchanged.

Phase 0 verification (2026-07-18): Debug x64 full-solution build succeeded with zero warnings/errors; all 15 test files and 197 test cases passed with no leak report; the MiniHttpServer home/second-page/Assets-prefix UI, module/fetch/action state, SVG/CSS, console, rejected asset paths, clean exit, and released ports were verified.

# Phase 1: Expose canonical layer-2 capabilities

## Public framing analysis

Add the following public concepts to `Source/InterProcess/AsyncSocket/HttpRequest.h`; implement them in the existing `AsyncSocket_HttpRequest.cpp`:

```cpp
enum class HttpFramingKind
{
	None,
	ContentLength,
	Chunked,
};

enum class HttpFramingAnalysisResult
{
	Succeeded,
	Invalid,
	UnsupportedTransferCoding,
};

struct HttpFraming
{
	HttpFramingKind				kind = HttpFramingKind::None;
	vuint64_t					contentLength = 0;
	vint						contentLengthFieldCount = 0;
	vint						contentLengthValueCount = 0;
	bool						contentLengthValuesPlainDecimal = true;
	bool						connectionClose = false;
};

extern HttpFramingAnalysisResult AnalyzeHttpFraming(
	const collections::List<HttpField>& fields,
	HttpFraming& framing
	);
```

Required semantics:

- [x] Reset the output on entry; treat it as authoritative only when the result is `Succeeded`.
- [x] Require already validated, lowercase-normalized field names and use exact comparison; the analyzer does not fold caller-supplied mixed-case names.
- [x] Count physical `Content-Length` fields separately from comma-list values.
- [x] Report `Content-Length: 3, 3` as length `3`, one field, and two values.
- [x] Report two `Content-Length: 3` fields as length `3`, two fields, and two values.
- [x] Set `contentLengthValuesPlainDecimal` only when every physical value is one nonempty sequence of digits with no comma or OWS. Parsed wire fields have already had outer OWS removed; constructed fields have not.
- [x] Preserve the current distinction between invalid framing and unsupported transfer coding.
- [x] Make both parsing and serialization call this one public analyzer.
- [x] Keep the numeric `ParseContentLength` implementation private; do not expose a second parser.
- [x] Move the HTTP request-line, header, body, chunk-line, and trailer size-limit constants from `AsyncSocket_HttpRequest.h` to `HttpRequest.h`; the public analyzers/helpers and their callers must see the limits they enforce.

## Field, body, Unicode, and request-line helpers

Add the following protocol-neutral declarations to `HttpRequest.h` and implementations to `AsyncSocket_HttpRequest.cpp`:

```cpp
extern const HttpField* FindHttpField(
	const collections::List<HttpField>& fields,
	const WString& normalizedName
	);

extern vint CountHttpFields(
	const collections::List<HttpField>& fields,
	const WString& normalizedName
	);

extern HttpField CreateAsciiHttpField(
	const WString& name,
	const WString& value
	);

extern bool DecodeAsciiHttpFieldValue(
	const collections::Array<vuint8_t>& value,
	WString& text
	);

extern bool HttpFieldValueEqualsAscii(
	const collections::Array<vuint8_t>& value,
	const WString& expected
	);

extern bool TryGetHttpBodySize(
	const HttpBody& body,
	vint& size
	);

extern bool FlattenHttpBody(
	const HttpBody& body,
	collections::Array<vuint8_t>& bytes
	);

extern void SetHttpBodyBytes(
	HttpBody& body,
	collections::Array<vuint8_t>&& bytes
	);

extern bool EncodeStrictUtf8(
	const WString& text,
	collections::Array<vuint8_t>& bytes
	);

extern bool DecodeStrictUtf8(
	const vuint8_t* bytes,
	vint count,
	WString& text
	);

enum class HttpRequestLineValidationResult
{
	Succeeded,
	InvalidMethod,
	InvalidRequestTarget,
	TooLong,
};

extern HttpRequestLineValidationResult ValidateHttpRequestLine(
	const WString& method,
	const WString& requestTarget
	);
```

Required behavior:

- [x] `FindHttpField` returns the first exact match or `nullptr`; `CountHttpFields` counts all exact matches. Both require callers to supply the lowercase normalized name and perform no locale folding.
- [x] `CreateAsciiHttpField` lowercases and validates the field name and validates ASCII field-value characters; invalid input raises `CHECK_ERROR`.
- [x] `DecodeAsciiHttpFieldValue` rejects bytes above `0x7F` but otherwise preserves the explicit byte sequence. `HttpFieldValueEqualsAscii` returns `false` for non-ASCII expectations or unequal bytes.
- [x] Body size/flatten helpers count chunk bytes only, ignore trailer sizes, accept empty chunks as zero-byte container input, and fail safely on overflow or `HttpBodySizeLimit`; they do not claim the body is serializable.
- [x] `SetHttpBodyBytes` raises `CHECK_ERROR` before mutation when the input exceeds `HttpBodySizeLimit`; otherwise it clears chunks and trailers and uses zero chunks for empty input or one chunk otherwise. It does not add, remove, or reconcile enclosing `Content-Length` / `Transfer-Encoding` fields; serialization still rejects mismatched explicit framing.
- [x] Strict UTF-8 accepts empty input and preserves embedded `U+0000`; NUL rejection remains layer-4 policy.
- [x] Strict UTF-8 rejects unpaired surrogates, overlong forms, invalid continuations, truncation, encoded surrogates, and values above `U+10FFFF`.
- [x] `DecodeStrictUtf8` permits `bytes == nullptr` only when `count == 0` and rejects negative counts or a null pointer with a positive count. UTF-8 helpers do not apply `HttpBodySizeLimit`; container/protocol callers own that limit.
- [x] Body, ASCII-decoding, and strict UTF-8 output parameters remain unchanged when their helper fails. `AnalyzeHttpFraming` is the explicit exception: it resets its output on entry and the result is unusable unless it returns `Succeeded`.
- [x] Serialization uses `ValidateHttpRequestLine`; no other layer repeats the `method + target + 10` calculation.

## Layer-2 tests

Add focused tests to `Test/Source/TestInterProcess_HttpRequest.cpp`:

- [x] Every framing kind, repeated equal lengths, comma lists, OWS, conflicts, CL+TE, supported chunked, and unsupported transfer coding/parameters; directly assert `contentLengthValuesPlainDecimal` for digit-only, OWS, and comma forms.
- [x] Field lookup/count and ASCII construction/decoding, including duplicate names and invalid input.
- [x] Empty, binary, embedded-NUL, one-chunk, and multi-chunk body operations.
- [x] Valid UTF-8 boundaries and all malformed classes listed above.
- [x] Request-line limits at exactly the limit and one byte beyond it, plus invalid methods and targets.
- [x] Existing parser and serializer tests continue passing unchanged.

Phase 1 verification (2026-07-18): Debug/x64 full solution build succeeded with 0 warnings and 0 errors; all 15 test files and 207 test cases passed with no memory-leak report. MiniHttpServer browser verification passed for both pages, deterministic interaction, module/fetch/CSS/SVG state, exact `/Assets` routing, both required zero-byte 404 responses, a clean warning/error console, clean newline shutdown, released ports 8888/8889, and unreachable endpoints after exit.

# Phase 2: Make layer 3 convenient without losing binary control

## Server request-context API

Add protocol-neutral conveniences to `SocketHttpRequestContext` while retaining `GetRequest()` and `Respond(Ptr<HttpResponse>)` as the authoritative raw APIs:

```cpp
bool TryGetBodyUtf8(WString& body);

bool RespondStatus(
	vint statusCode,
	const WString& reason,
	Func<void(bool)> completion = {}
	);

bool RespondBytes(
	vint statusCode,
	const WString& reason,
	const WString& contentType,
	const collections::Array<vuint8_t>& body,
	Func<void(bool)> completion = {}
	);

bool RespondUtf8(
	vint statusCode,
	const WString& reason,
	const WString& contentType,
	const WString& body,
	Func<void(bool)> completion = {}
	);
```

Semantics and work:

- [x] `TryGetBodyUtf8` flattens and strictly decodes the complete body; empty text and NUL remain valid at layer 3.
- [x] It does not validate method, route, content type, framing shape, or logical-message policy.
- [x] Response conveniences construct a raw response and delegate to the existing `Respond`, retaining normalization, CORS, `Content-Length`, completion, and context-race behavior.
- [x] Empty `reason` keeps the existing default-reason behavior; nonempty reasons preserve layer-4 error text.
- [x] Empty `contentType` omits the field; a nonempty content type must be valid ASCII HTTP field text. A nonempty reason remains subject to the existing printable-ASCII reason validation. Byte and raw APIs remain fully binary capable.
- [x] Every convenience validates its status/reason/content-type/body arguments before calling `Respond`. `RespondUtf8` also validates Unicode and the encoded body limit. Invalid input raises `CHECK_ERROR` even if the context has already been consumed; for valid arguments, the returned `bool` describes only whether the response won lifecycle ownership.

## Client response convenience

- [x] Add `bool windows_http::HttpResponse::TryGetBodyUtf8(WString& body) const` beside `GetBodyUtf8`, implemented in `NetworkProtocolHttp.cpp` with the canonical strict codec so both portable and Windows consumers can distinguish malformed input from valid empty input.
- [x] Keep `SocketHttpClientApi::HttpQuery` and `windows_http::HttpResponse` source-compatible and intentionally flat.
- [x] Do not add `HttpQueryRaw` or raw framing requirements in this refactoring; the Network Protocol client must remain compatible with ordinary HTTP.sys response framing.
- [x] Keep `windows_http::HttpResponse::GetBodyUtf8` behavior unchanged for compatibility, but stop using it where malformed input must be distinguishable from valid empty input.

## Layer-3 internal cleanup

In `AsyncSocket_HttpServerApi.cpp`:

- [x] Replace private ASCII field conversion/construction and single-header loops with layer-2 helpers.
- [x] Replace response body-size/flatten loops with layer-2 body helpers.
- [x] Normalize and validate application-supplied response field names first, then run `AnalyzeHttpFraming` over that normalized view; never analyze the original mixed-case list with exact lowercase lookup.
- [x] Preserve response-normalization framing policy after analysis: reject every application `Transfer-Encoding` including legal `chunked`, require `contentLengthValuesPlainDecimal`, and continue accepting repeated equal physical length fields. This preserves rejection of comma lists and OWS in caller-constructed length values.
- [x] Preserve HEAD/204/304 body suppression and their current `Content-Length` rules exactly.
- [x] Reuse `DecodeStrictUtf8` inside path decoding while preserving the router's current raw-character and trailing-prefix behavior and its explicit rejection of NUL and encoded separators.
- [x] Keep Date, CORS, Cache-Control, routing, and response-normalization policy in layer 3.

In `AsyncSocket_HttpClientApi.cpp`:

- [x] Use layer-2 body helpers and ASCII field helpers only where the existing values are ASCII by contract.
- [x] Retain the current `CreateField` / `DecodeFieldValue` UTF-8 projection adapters for `WString` content type, cookie, and extra-field values; do not replace their intentionally permissive `wtou8` / `u8tow` behavior with ASCII or strict-body semantics in this refactoring.
- [x] Preserve callback ordering, reentrancy, timeout, stop, identity-coding, and first-content-type behavior.

## Layer-3 tests

Add focused tests to `Test/Source/TestInterProcess_AsyncSocket_MiniHttpApi.cpp`:

- [x] `TryGetBodyUtf8` with empty, non-ASCII, malformed, overlong, truncated, embedded-NUL, and a multibyte sequence split across chunk boundaries.
- [x] `RespondStatus`, `RespondBytes`, and `RespondUtf8` normalization, body bytes, content type, reason, and `Content-Length`, including mixed-case supplied field regressions through the raw API.
- [x] Preserve HEAD/204/304 suppression and length normalization; reject application `Transfer-Encoding: chunked` through the raw response API.
- [x] Invalid response Unicode raises before a pending context is consumed and leaves it reusable; invalid Unicode also raises before a consumed-context lifecycle check, while valid input on a consumed context returns `false`.
- [x] Invalid status, reason, content type, and oversized byte/text bodies follow the same validate-before-lifecycle precedence.
- [x] Existing raw/binary response behavior remains unchanged.
- [x] Client strict body conversion distinguishes malformed input from valid empty input.
- [x] Preserve flat response projection for non-ASCII field values, malformed field bytes, duplicate content types, and content type found only in a trailer.
- [x] Existing path-character acceptance, NUL/encoded-separator rejection, and trailing-prefix normalization remain unchanged, including a raw character accepted by the router but rejected by the stricter Network Protocol `pchar` policy.

Phase 2 verification (2026-07-18): Debug/x64 full solution build succeeded with 0 warnings and 0 errors; all 15 test files and 210 test cases passed with no memory-leak report. Regenerated `Release` outputs and compile-checked both packed and include-only common/Windows amalgamations. MiniHttpServer browser verification passed for both pages, deterministic interaction, module/fetch/CSS/SVG state, exact `/Assets` routing, both required zero-byte 404 responses, a clean warning/error console, clean newline shutdown, released ports 8888/8889, and unreachable endpoints after exit.

# Phase 3: Consolidate the layer-4 wire contract

Extend `Source/InterProcess/NetworkProtocolHttp.h/.cpp`; do not add an async-only shared component in this phase.

## Common definitions

Add the following common layer-4 surface, placing request factories after the `windows_http` value declarations as required by the header:

```cpp
constexpr const wchar_t* HttpNetworkProtocolContentType = L"application/json; charset=utf8";

extern WString CreateHttpNetworkProtocolConnectBody(
	const WString& requestPath,
	const WString& responsePath
	);

extern bool ParseHttpNetworkProtocolConnectBody(
	const WString& body,
	WString& requestPath,
	WString& responsePath
	);

extern bool ValidateHttpNetworkProtocolBaseUrl(const WString& baseUrl);
extern bool ValidateHttpNetworkProtocolEndpointPath(const WString& path);
extern bool IsValidHttpNetworkProtocolMessage(const WString& message);

extern windows_http::HttpRequest CreateHttpNetworkProtocolConnectRequest(
	const WString& target
	);

extern windows_http::HttpRequest CreateHttpNetworkProtocolReceiveRequest(
	const WString& target
	);

extern windows_http::HttpRequest CreateHttpNetworkProtocolSendRequest(
	const WString& target,
	const collections::Array<char>& body
	);
```

- [x] Add one `HttpNetworkProtocolContentType` constant for `application/json; charset=utf8`.
- [x] Add a canonical Connect-pair formatter and strict parser. The formatter rejects empty or semicolon-containing halves so its output round-trips. The strict parser requires exactly two nonempty paths separated by exactly one semicolon; path validation remains separate.
- [x] Add `ValidateHttpNetworkProtocolBaseUrl` for the empty-or-leading-slash, no-trailing-slash, RFC-style `pchar`, percent-escape, strict UTF-8, NUL, query/fragment, backslash, and encoded-separator contract.
- [x] Add a separate `ValidateHttpNetworkProtocolEndpointPath` for server-returned Connect paths. It requires a nonempty origin path and preserves the current allowance for a trailing slash; all other character/escape rules match the Network Protocol path grammar.
- [x] Keep the portable 8 KiB request-line budget out of these common path validators. Portable server/client call the public layer-2 `ValidateHttpRequestLine` separately at the exact targets listed below.
- [x] Add one logical-message predicate for nonempty/no-NUL `WString` values; strict Unicode and byte limits remain explicit caller inputs.
- [x] Add common Connect, receive-poll, and send-request constructors over `windows_http::HttpRequest`.
  - Connect: `GET`, exact Accept type, empty body.
  - Receive: `POST`, exact Accept type, explicit `Content-Length: 0`.
  - Send: `POST`, exact Accept and Content-Type, caller-supplied encoded body.
  - Factories set only wire shape. Each backend continues to set `receiveTimeout`, `keepAliveOnStop`, and other lifecycle/submission options afterward.

Required API behavior:

- [x] Invalid Connect formatter input raises `CHECK_ERROR`; strict parser output parameters remain unchanged on `false`.
- [x] Base/endpoint validators return `false` rather than throwing for invalid caller or wire input.
- [x] `IsValidHttpNetworkProtocolMessage` checks only nonempty/no-NUL text after strict conversion; callers separately enforce Unicode validity and transport byte limits.
- [x] Request factories copy the encoded body where applicable and do not validate base paths, endpoint paths, request-line budgets, timeouts, keep-alive, or retry policy.

## Cross-backend adoption

- [x] Replace portable and Windows copies of the media type and Connect-pair formatter with the common definitions.
- [x] Use the strict Connect-pair parser in the portable client. Do not migrate the legacy Windows client parser in this refactoring: it currently accepts the first semicolon without the portable client's empty/additional-semicolon checks, so migration would be a separate behavior change.
- [x] Reuse common request constructors where they preserve the Windows client's asynchronous behavior; keep backend-specific submission/lifetime code local.
- [x] Keep async-socket types out of `NetworkProtocolHttp.h`. Its `.cpp` may depend on the canonical layer-2 strict UTF-8 implementation for the new failure-bearing response method and path grammar; explicitly verify all supported VlppOS import/build variants link that implementation.
- [x] Reconsider `AsyncSocket_HttpNetworkProtocolShared.h/.cpp` only if several nontrivial async-specific policy functions remain duplicated after Phases 4 and 5.

## Common-contract tests

- [x] Connect formatter/parser round-trip plus empty, semicolon-containing, missing-delimiter, and multiple-delimiter cases.
- [x] Base-prefix and endpoint validators prove their empty/trailing-slash distinction and share all other invalid-character/escape cases.
- [x] Logical-message predicate covers empty, embedded NUL, non-ASCII, and ordinary values after strict conversion.
- [x] Request factories assert only method, target, Accept, Content-Type, explicit empty-poll framing, and encoded body; backend lifecycle fields are set and tested by each caller.
- [x] Legacy Windows malformed-Connect parsing behavior remains characterized and unchanged.

Phase 3 verification (2026-07-18): Debug/x64 full solution build succeeded with 0 warnings and 0 errors; all 15 test files and 215 test cases passed with no memory-leak report. Regenerated `Release` outputs and compile-checked both packed and include-only common/Windows amalgamations. MiniHttpServer browser verification passed for both pages, deterministic interaction, module/fetch/CSS/SVG state, exact `/Assets` routing, both required zero-byte 404 responses, a clean warning/error console, clean newline shutdown, released ports 8888/8889, and unreachable endpoints after exit.

# Phase 4: Simplify the server layer-4 adapter

Refactor `Source/InterProcess/AsyncSocket/AsyncSocket_HttpServer.cpp` so `Impl::OnRequest` reads as route and Network Protocol policy.

## Replace parsing/construction

- [x] Replace the local `ParseContentLength` with `AnalyzeHttpFraming`.
- [x] Preserve the stricter layer-4 form with `kind`, `contentLengthFieldCount == 1`, `contentLengthValueCount == 1`, and `contentLengthValuesPlainDecimal` checks.
- [x] Replace case folding, header counting, and ASCII comparison with layer-2 field helpers.
- [x] Replace body flattening and local UTF-8 decoding with `SocketHttpRequestContext::TryGetBodyUtf8`.
- [x] Replace raw success/error response construction with `RespondUtf8` and `RespondStatus`.
- [x] Replace media type, Connect-pair, and base-URL helpers with the common definitions.
- [x] Validate GET `{baseUrl}/VlppInterProcess/Connect` and POST `{baseUrl}/VlppInterProcess/Request|Response/{36-character token placeholder}` with layer-2 `ValidateHttpRequestLine` before registering the server prefix.
- [x] In `SocketHttpServer::SendString`, call `EncodeStrictUtf8`, require nonempty/no-NUL text, and enforce encoded bytes `<= HttpBodySizeLimit` before queue/state mutation. The encoded array may be discarded until Phase 6.
- [x] Reduce `HasEmptyBody` and `DecodeSubmittedMessage` to small predicates over analyzed framing, headers, decoded body, and layer-4 policy; inline them if extraction no longer carries useful information.

## Delete obsolete helpers

- [x] Remove `ServerFoldAscii`, `ServerAsciiEqualsIgnoreCase`, `ServerHexValue`, and the duplicated legal-path/target-size helpers after migration.
- [x] Remove `IsValidWString`, the local `DecodeStrictUtf8`, and `EncodeMessage` after all callers use layer-2/layer-3 facilities.
- [x] Remove `CreateAsciiField`, `FieldValueEquals`, `ParseContentLength`, and the body reconciliation loop.
- [x] Remove the local content-type constant and Connect-pair formatter.

## Keep local

- [x] Keep token generation/collision handling and `ExtractToken`.
- [x] Keep route dispatch, connection lookup/ownership, pending-poll management, piggyback selection, callbacks, queues, and shutdown state unchanged.

Phase 4 verification (2026-07-18): Debug/x64 full solution build succeeded with 0 warnings and 0 errors; all 15 test files and 215 test cases passed with no memory-leak report. Regenerated `Release` outputs and compile-checked both packed and include-only common/Windows amalgamations. MiniHttpServer browser verification passed for both pages, deterministic interaction, module/fetch/CSS/SVG state, exact `/Assets` routing, both required zero-byte 404 responses, a clean warning/error console, clean newline shutdown, released ports 8888/8889, and unreachable endpoints after exit.

# Phase 5: Simplify the client layer-4 adapter

Refactor `Source/InterProcess/AsyncSocket/AsyncSocket_HttpClient.cpp` without changing its two-lane state machine.

## Replace validation/construction

- [ ] Build Connect, receive-poll, and send exchanges through the common request constructors.
- [ ] Use `windows_http::HttpResponse::TryGetBodyUtf8` and apply only status, exact selected content type, NUL, and operation-specific message policy in layer 4. `/Connect` and delivered logical messages must be nonempty; an empty poll/send response still means no message.
- [ ] Use the common strict Connect-pair parser, `ValidateHttpNetworkProtocolBaseUrl` for constructor input, and `ValidateHttpNetworkProtocolEndpointPath` for both returned paths.
- [ ] In the constructor, validate GET `{baseUrl}/VlppInterProcess/Connect` with layer-2 `ValidateHttpRequestLine`; after Connect, validate both actual POST targets formed from `{baseUrl}` plus the returned paths.
- [ ] In `SocketHttpClient::SendString`, call `EncodeStrictUtf8`, require nonempty/no-NUL text, and enforce encoded bytes `<= HttpBodySizeLimit` before queue/state mutation. The encoded array may be discarded until Phase 6.
- [ ] Keep ordinary legal response framing accepted; do not inspect or require raw response `Content-Length`.
- [ ] Reduce `DecodeSuccessfulResponse` to a small protocol-result check.

## Delete obsolete helpers

- [ ] Remove local `HexValue`, legal-path character logic, `ValidateOriginPath`, and `ValidateRequestTarget`.
- [ ] Remove local `ValidateUtf8`, `ValidateWString`, `GetUtf8Size`, and `DecodeUtf8` after migration.
- [ ] Remove the local media-type constant and Connect-pair parser.
- [ ] Remove repeated Connect/poll/send request-shape construction.

## Keep local

- [ ] Keep operation-specific error messages and retry ownership.
- [ ] Keep the receive and send lanes, FIFO/head retry, replacement workers, receive-before-deliver ordering, callback draining, and shutdown behavior unchanged.
- [ ] Keep the rule that an empty successful body does not call `OnReadString`.

# Phase 6: Eliminate repeated message encoding

Perform this only after Phases 0-5 pass without behavioral changes.

- [ ] Change the client `SendItem` to store the already validated UTF-8 bytes instead of only the original `WString`; reuse the same bytes for every retry.
- [ ] Change queued server outbound work to store validated bytes when no later operation requires the original `WString`.
- [ ] Retain the validated arrays already produced before queue/state mutation instead of discarding and regenerating them.
- [ ] Use `RespondBytes` and the common send-request constructor to avoid re-encoding.
- [ ] Add tests proving retry sends identical bytes and invalid Unicode fails before queue/state mutation.

# Verification and completion gates

## Required source files

Expected production changes:

- `Source/InterProcess/AsyncSocket/HttpRequest.h`
- `Source/InterProcess/AsyncSocket/AsyncSocket_HttpRequest.h`
- `Source/InterProcess/AsyncSocket/AsyncSocket_HttpRequest.cpp`
- `Source/InterProcess/AsyncSocket/AsyncSocket_HttpServerApi.h/.cpp`
- `Source/InterProcess/AsyncSocket/AsyncSocket_HttpClientApi.h/.cpp`
- `Source/InterProcess/AsyncSocket/AsyncSocket_HttpServer.cpp`
- `Source/InterProcess/AsyncSocket/AsyncSocket_HttpClient.cpp`
- `Source/InterProcess/NetworkProtocolHttp.h/.cpp`
- relevant legacy Windows layer-4 files only where adopting common contract helpers is behavior-neutral.

Expected test changes:

- `Test/Source/TestInterProcess_HttpRequest.cpp`
- `Test/Source/TestInterProcess_AsyncSocket_MiniHttpApi.cpp`
- `Test/Source/TestInterProcess.cpp`

No new C++ source file is planned. If implementation evidence reverses that decision, update all affected project and filter files according to `.github/Guidelines/SourceFileManagement.md`.

## Verification after every phase

- [ ] Build `Test/UnitTest/UnitTest.sln` using `.github/Scripts/copilotBuild.ps1` from `Test/UnitTest`.
- [ ] Run the relevant UnitTest project through `.github/Scripts/copilotExecute.ps1` and confirm no filtered related test file, crash, or memory-leak report.
- [ ] Run the complete related HTTP request, Mini HTTP API, Network Protocol, and interoperability test files after the final phase.
- [ ] Run the corresponding Linux/macOS builds and tests when those environments are available because all changed product code is cross-platform.
- [ ] Run the MiniHttpServer browser verification after layer-3 response/routing changes.

## Documentation and cleanup

- [ ] Update `KB_VlppOS_InterProcessAsyncSocketBasedMiniHttpApi.md` with the new layer-2 and layer-3 public APIs.
- [ ] Update `KB_VlppOS_InterProcessNetworkProtocolsAndChannels.md` only for externally observable contract/API changes; do not document internal helper movement.
- [ ] Remove test-local field/body/strict-UTF helpers when the public layer-2 helpers replace them.
- [ ] Search for every removed helper name and every duplicated media-type literal before completion.
- [ ] Confirm layer-4 files retain no decimal `Content-Length` parser, strict UTF-8 codec, or request-line length formula.
- [ ] Confirm all existing state-machine tests pass without changing their expected ordering or retry counts.

## Definition of done

- [ ] Layer 2 has one canonical framing analyzer used by parsing, serialization, layer 3, and layer 4.
- [ ] Layer 3 exposes binary-safe and failure-bearing UTF-8 conveniences without embedding Network Protocol policy.
- [ ] Layer 4 expresses its strict request and logical-message rules using parsed values and shared helpers.
- [ ] Cross-backend Network Protocol wire facts have one owner in `NetworkProtocolHttp`.
- [ ] No cached derived framing data can become stale in mutable HTTP messages.
- [ ] No socket, retry, polling, queue-order, callback, or shutdown behavior changes unintentionally.
- [ ] All required builds, tests, browser verification, and documentation updates are complete.
