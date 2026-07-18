# Mini HTTP API refactoring review

## Scope

This document reviews the boundary between:

- `SocketHttpServerApi` / `SocketHttpClientApi`, which are expected to present parsed HTTP requests and responses to users; and
- the `INetworkProtocolServer` / `INetworkProtocolClient` implementations built on those APIs, which are expected to interpret the Network Protocol over HTTP contract.

`IAsyncSocketServer` / `IAsyncSocketClient` and `HttpRequestServer` / `HttpRequestClient` are treated as established lower layers. This review contains the reasoning and ownership analysis. The resulting decisions, implementation order, and verification work are recorded in [TODO_Task_MiniHttpApiRefactoring_ActionableItems.md](TODO_Task_MiniHttpApiRefactoring_ActionableItems.md).

## Executive conclusion

Layer 4 is not generally reimplementing the HTTP parser. The lower request layer already parses the start line, headers, framing, and body, while the Mini HTTP APIs already perform routing and response normalization. Much of the remaining layer-4 code is also legitimate Network Protocol state: token ownership, long polling, piggyback responses, retry behavior, FIFO delivery, and shutdown coordination.

There is nevertheless a concentrated block of avoidable parsing and construction code in both portable layer-4 implementations. It exists for three reasons:

1. The Network Protocol compatibility contract requires stricter semantics than ordinary HTTP: exact status and media type, strict UTF-8 with no NUL, nonempty logical messages, fixed endpoint shapes, and a semicolon-delimited `/Connect` body.
2. The layer-3 APIs are asymmetric. `SocketHttpClientApi` exposes flattened `windows_http::HttpRequest` / `HttpResponse` values, but `SocketHttpRequestContext` exposes a raw `HttpRequest` and accepts a raw `HttpResponse`.
3. Useful parsing and construction primitives exist inside layer 2 and layer 3, but many are private and none offers a failure-bearing strict UTF-8 body conversion.

The important ownership distinction is therefore:

- HTTP grammar and generic byte/container operations belong below layer 4.
- User-oriented presentation of HTTP fields, bodies, paths, and responses belongs in layer 3.
- Exact Network Protocol rules and the connection/message state machine belong in layer 4.

The largest reduction would come from removing duplicate generic validation and raw body/header manipulation, not from moving the layer-4 state machines.

## Current boundary and the source of the complexity

### What layer 2 already guarantees

`AsyncSocket_HttpRequest.cpp` already does the expensive HTTP work:

- `ParseFieldLine` lowercases field names and trims optional whitespace (`AsyncSocket_HttpRequest.cpp:273-312`).
- The canonical `ParseContentLength` accepts and reconciles valid `Content-Length` values (`AsyncSocket_HttpRequest.cpp:332-360`).
- `AnalyzeFraming` rejects ambiguous `Content-Length` / `Transfer-Encoding` combinations and unsupported transfer coding (`AsyncSocket_HttpRequest.cpp:451-481`).
- Fixed-length parsing waits for exactly the declared bytes, enforces the body limit, and, for a positive length, produces exactly one body chunk containing those bytes (`AsyncSocket_HttpRequest.cpp:702-738`).
- Serialization privately validates the method, request target, and request-line size (`AsyncSocket_HttpRequest.cpp:902-918`).

Consequently, a layer-4 consumer receives a structurally parsed message. It may still enforce an application rule such as “exactly one decimal `Content-Length` and no transfer coding,” but it should not need to reconcile the declared length with the already-parsed body. Once that fixed-length rule succeeds, the server adapter's multi-chunk flatten/reconciliation loop is unnecessary because the lower parser has produced the single positive-length chunk.

### What layer 3 already does

The server API already:

- validates and decodes the routed path;
- provides a decoded relative path and a query separately;
- validates routing/Host/CORS conditions;
- normalizes response fields, flattens response chunks, and rebuilds `Content-Length`.

The client API already:

- constructs Host and identity-coding fields;
- projects the flat request value into a raw HTTP request;
- receives a parsed HTTP response;
- interprets selected response fields and flattens the body into `windows_http::HttpResponse`.

That client projection is intentionally lossy. `ConvertResponse` retains selected values such as the first content type/cookie, processes fields and trailers through the same projection, discards the remaining ordered raw fields, and erases the original framing. Layer 4 can compare the selected content type, but it cannot detect duplicate content-type fields, distinguish a value sourced from a trailer, or inspect the original framing. The compatibility contract explicitly requires the client to accept ordinary legal response framing and the legacy Windows client exposes the same flat shape, so this refactoring keeps that projection and does not add stricter raw-response validation.

These are the right layer-3 responsibilities. The problem is that their reusable pieces are private. Examples include `DecodePath`, `ToAscii`, `Field`, `Normalize`, and `SingleHeader` in `AsyncSocket_HttpServerApi.cpp`, and `CreateField`, `DecodeFieldValue`, `CreateQuery`, and `ConvertResponse` in `AsyncSocket_HttpClientApi.cpp`.

The public API exposes that asymmetry directly:

- `SocketHttpRequestContext` offers `GetRequest()`, `GetRelativePath()`, `GetQuery()`, and `Respond(Ptr<HttpResponse>)` (`AsyncSocket_HttpServerApi.h:37-44`).
- `SocketHttpClientApi::HttpQuery` accepts and returns the already-flattened `windows_http` value types (`AsyncSocket_HttpClientApi.h:39-45`).

The server-side layer-4 adapter is therefore forced to recreate field construction, body flattening, strict text decoding, and response construction. Similar helper copies in Mini HTTP tests and the sample server confirm that this is a public API gap rather than an isolated adapter preference.

### Why some layer-4 interpretation is still necessary

The Network Protocol over HTTP contract is stricter than a generic Mini HTTP exchange. Layer 4 has to understand that:

- `/Connect`, `/Request/{token}`, and `/Response/{token}` have different methods and empty/nonempty body rules;
- successful responses have status `200` and the exact media type `application/json; charset=utf8`;
- a logical message is a nonempty, NUL-free Unicode string with a byte-based size limit;
- an empty successful response means “no piggyback message” rather than an empty logical message; and
- the `/Connect` result contains exactly two endpoint paths separated by one semicolon.

Those are application-protocol facts. They should not be hidden in a generic HTTP parser. The review target is the generic machinery used to implement those facts, plus protocol facts duplicated independently by server and client.

## Duplicate and misplaced code

| Concern | Server layer 4 | Client layer 4 | Assessment |
| --- | --- | --- | --- |
| Unicode validity | `IsValidWString`, `DecodeStrictUtf8`, `EncodeMessage` (`AsyncSocket_HttpServer.cpp:56-220`) | `ValidateUtf8`, `ValidateWString`, `GetUtf8Size`, `DecodeUtf8` (`AsyncSocket_HttpClient.cpp:59-161`) | Near-duplicate generic validation. Layer 3 has a third strict decoder in its path parser. |
| Origin path validation | `ServerHexValue`, legal-character checks, `ValidateBasePath` (`AsyncSocket_HttpServer.cpp:30-188`) | `HexValue`, legal-character checks, `ValidateOriginPath` (`AsyncSocket_HttpClient.cpp:33-190`) | Overlapping syntax/security machinery, with a third implementation in `AsyncSocket_HttpServerApi.cpp:68-177`; their policy details are not identical. |
| Request-line budget | `ValidRequestTargetSize` (`AsyncSocket_HttpServer.cpp:190-193`) | `ValidateRequestTarget` (`AsyncSocket_HttpClient.cpp:192-198`) | Duplicates a private serializer formula and its unexplained constant. |
| Field/body mechanics | `CreateAsciiField`, `FieldValueEquals`, and the body loop in `DecodeSubmittedMessage` (`AsyncSocket_HttpServer.cpp:223-342`) | Mostly hidden by `SocketHttpClientApi`, but strict body decoding remains above it | Generic operations are absent from the public raw HTTP types. In this particular fixed-length route the body loop is redundant; similar general-purpose copies also exist in layer 3 and tests. |
| HTTP framing | `ParseContentLength`, `HasEmptyBody`, and part of `DecodeSubmittedMessage` (`AsyncSocket_HttpServer.cpp:271-342`) | Layer 3 has already flattened the response | The numeric parser repeats canonical layer-2 work but also enforces a narrower decimal-only wire form. That distinction needs an explicit home or compatibility decision, not a shared second parser. |
| Exact content type | `ServerJsonContentType` (`AsyncSocket_HttpServer.cpp:12`) | `JsonContentType` (`AsyncSocket_HttpClient.cpp:29`) | Genuine layer-4 information, duplicated across portable server/client and the legacy backend. |
| Connect result | Formats `requestPath + ";" + responsePath` (`AsyncSocket_HttpServer.cpp:1423-1425`) | Decodes and splits it (`AsyncSocket_HttpClient.cpp:759-796`) | The producer and consumer independently encode opposite halves of the same layer-4 wire format. |
| Request shapes | Route handling in `Impl::OnRequest` (`AsyncSocket_HttpServer.cpp:1394-1451`) | Connect, poll, and send requests are assembled separately (`AsyncSocket_HttpClient.cpp:744-757`, `832-841`, `1055-1060`) | Methods/routes are protocol facts. The client repetition is small, but a shared description could prevent drift. |

Two additional effects are worth noting:

- The client validates and measures a `WString` in `SendString`, stores only the `WString`, and encodes it again for each send attempt. The server likewise sometimes validates by encoding into a temporary array and later encodes again when constructing a response. This is not parsing correctness, but it is a symptom of having no reusable validated-message representation.
- Server/client helper names were made different to avoid amalgamated-build collisions. That resolves symbol collisions but leaves two implementations of the same information, so it does not resolve the duplication itself.
- The client poll request explicitly adds `Content-Length: 0` because the lower serializer deliberately does not generate that field for an empty request (`AsyncSocket_HttpRequest.cpp:962-967`). The legacy compatibility contract wants an unambiguous empty POST, so this is a real request-shape rule. It can be described with the other layer-4 request shapes; it is not evidence that the client is reparsing HTTP.

## Content-Length shows the boundary tradeoff

The server layer-4 `ParseContentLength` is not merely shared code waiting to be extracted. It repeats lower-layer parsing that has already succeeded.

For an incoming fixed-length request, layer 2 has already:

- parsed every `Content-Length` field;
- rejected invalid, conflicting, or ambiguous framing;
- enforced the body limit;
- waited for the declared number of bytes; and
- populated the body with those bytes.

Layer 4 may need to preserve a stricter legacy rule such as “one plain-decimal fixed-length field must be present” or “chunked input is not accepted.” Header count and framing kind alone do not preserve that rule: layer 2 deliberately accepts equivalent comma-separated values such as `Content-Length: 3, 3`, while the current layer-4 decimal-only parser rejects them. Preserving the exact legacy wire-shape rule therefore requires parsed value cardinality/form to be observable from layer 2. The alternative is an explicit decision that accepting layer 2's broader canonical form is compatible. Neither case justifies maintaining an independent numeric parser in layer 4.

The same reasoning applies to lowercased field names and trimmed field whitespace. Layer 4 can rely on the normalized representation produced by the parser instead of repeating case folding or OWS handling.

## Existing helpers and upstream audit

### Helpers that can already be reused

- The sibling `Vlpp` repository provides `wtou8` and `u8tow`. The portable client already uses them. They are suitable conversion primitives for this protocol only after strict validation and NUL rejection.
- `NetworkProtocolHttp.h` already owns `HttpServerUrl_Connect`, `HttpServerUrl_Request`, and `HttpServerUrl_Response` and is used by the portable implementation.
- `windows_http::HttpRequest::SetBodyUtf8` already constructs the client-side body.
- `HttpUrlEncodeQuery` / `HttpUrlDecodeQuery` already own query encoding. Query decoding is not a substitute for path decoding because its semantics are intentionally different, including `+` handling.
- The lower HTTP parser's normalization and framing guarantees are already available implicitly in every parsed request and response.

### Important gaps in those helpers

- No reusable upstream helper was found that reports strict RFC 3629-style UTF-8 failure while also supporting the required no-NUL policy. `wtou8` / `u8tow` convert, but are not a replacement for explicit strict validation.
- `windows_http::HttpResponse::GetBodyUtf8` does not report success or failure. It cannot distinguish malformed/truncated input or an embedded NUL from all valid outcomes required by this protocol.
- The serializer's request-line validation and limit calculation are private, so layer 4 duplicates the calculation to reject an invalid server-provided `/Connect` path before a later send/reconnect failure.
- The legacy Windows server has `GetUtf8Body`, `SendResponse`, and `SendResponseUtf8`, which are good evidence for the desired layer-3 convenience level. They depend on Windows HTTP Server API types and cannot be reused directly by the portable API.
- No suitable upstream portable UUID/token helper was found. Token generation is server-specific state rather than duplicated parsing, so it is not a priority for this boundary review.

## Verification coverage

Existing tests cover portable/Windows interoperability, canonical Connect/poll/send/piggyback exchanges, retry behavior, non-ASCII round trips, and the UTF-8 byte limit (`TestInterProcess.cpp:2141-2391`). They do not directly isolate malformed UTF-8 or NUL response bodies, malformed/multiple-semicolon Connect bodies, invalid base URLs, duplicate/wrong content types, or the strict decimal-only `Content-Length` form. The strict-parser observations in this document are therefore supported by the implementation and compatibility contract, but not every edge is independently regression-locked by a focused test.

## Placement analysis

### Generic raw HTTP helpers: `HttpRequest.h`

The following capabilities do not require knowledge of Network Protocol routes, tokens, long polling, or logical messages:

- encoding-explicit ASCII, UTF-8, and raw-byte `HttpField` construction/decoding;
- normalized field lookup, presence, and count;
- `HttpBody` byte count, append/set, and flatten operations;
- a failure-bearing strict UTF-8 encode/decode primitive over byte arrays or `HttpBody`;
- public validation of the serialized request-line/request-target budget; and
- access to parsed framing information if a caller needs to distinguish fixed-length, chunked, and absent-body forms.

These capabilities fit the raw HTTP model in `HttpRequest.h`. The generic UTF-8 primitive should be length-aware and report conversion failure; whether an empty string or NUL is legal should remain a caller policy rather than being silently hard-coded as a Network Protocol rule. In particular, an embedded-NUL-capable generic helper cannot be implemented by blindly relying on zero-terminated conversions. Field helpers should also name their encoding explicitly because HTTP field values are bytes, not inherently `WString` text.

The framing result should be exposed as the output of a public analyzer, not cached in `HttpRequest` or `HttpResponse`. Both classes expose mutable headers and bodies and are also used to construct outgoing messages, so cached `Content-Length` or framing members could become stale. A public analyzer over the current fields removes duplicated parsing code and preserves one canonical interpretation without introducing invalid derived state.

Origin-path handling is a boundary case. Percent-triplet parsing and strict byte-to-text decoding are generic, but rejecting encoded separators, accepting an empty base prefix, and rejecting a trailing slash are Mini HTTP routing/security policies. The existing implementations are not interchangeable: layer 4 restricts raw characters to an RFC-style `pchar` set and rejects a trailing base-prefix slash, whereas layer 3 accepts broader printable ASCII and normalizes trailing prefix slashes. A generic origin-form decoder could live with the raw HTTP helpers if its semantics are useful to other HTTP users. Any shared Mini HTTP policy above it requires an explicit compatibility choice rather than direct extraction of one current implementation.

Nontrivial implementations would belong in the corresponding `.cpp`; adding them to `HttpRequest.h` does not imply large inline implementations.

### User-oriented conveniences: layer 3

Candidate layer-3 capabilities that do not require knowledge of tokens or `/Connect` include:

- a strict, failure-bearing UTF-8 body accessor on `SocketHttpRequestContext` or an equivalent public helper;
- public header lookup/count helpers, unless those are exposed directly by the raw HTTP layer;
- an UTF-8 response convenience accepting status, content type, text/body, and completion, analogous to the legacy Windows `SendResponseUtf8`;
- a failure-bearing UTF-8 accessor for the flattened client response; and
- a policy-explicit Mini HTTP origin-path/prefix validator that makes the current character and trailing-slash differences visible rather than silently choosing one implementation.

Binary HTTP bodies must remain supported. These should be opt-in conveniences over the raw/flat representations, not an automatic assumption that every Mini HTTP request or response is text.

This is the main layer-3 opportunity: it supplies user-oriented parsed values and response construction while leaving the caller to decide whether the body is a logical message and which content type/status is required.

### Layer-4-specific shared information

After generic and layer-3 concerns are removed, the genuinely shared server/client residue is small:

- the exact compatibility media type;
- the success-status and empty/nonempty logical-message rules;
- the base-URL policy as applied to the three protocol endpoints;
- `/Connect` endpoint-pair formatting and parsing; and
- possibly a common description/factory for Connect, poll, and send HTTP request shapes.

This material is appropriate for a layer-4 shared component because it changes when the Network Protocol over HTTP contract changes, not when generic HTTP changes.

The chosen home is the existing `NetworkProtocolHttp.h/.cpp`. It already owns the route constants and value types and is shared by both the legacy Windows and portable backends. The exact media type, Connect-pair codec, base-path contract, and common request shapes extend that existing layer-4 ownership. Its `.cpp` implementation may consume public layer-2 helpers while keeping async-socket types out of the public header.

`AsyncSocket_HttpNetworkProtocolShared.h` will not be added in the initial refactoring. It should be reconsidered only if meaningful async-specific policy remains duplicated after the layer-2, layer-3, and common `NetworkProtocolHttp` work; creating it preemptively would fragment the protocol definition.

## What should remain in layer 4

The following complexity is not evidence of misplaced HTTP parsing:

- connection-token creation, lookup, ownership, and removal;
- route-to-operation semantics;
- at most one pending/in-flight server poll response, while allowing multiple queued outbound logical messages;
- one active operation in each client receive/send lane;
- long-poll reservation and cancellation;
- piggyback response selection;
- client receive-before-deliver ordering;
- two-lane retry/reconnect behavior and attempt limits;
- FIFO message delivery and callback draining; and
- shutdown/lifetime coordination.

These behaviors implement `INetworkProtocolServer` / `INetworkProtocolClient`, not HTTP. Extracting them into HTTP helpers would obscure the actual protocol state machine.

`ExtractToken` is similarly layer-4 route interpretation. It is not duplicated enough, and it contains protocol knowledge, so moving it merely to reduce the line count would not improve the layer boundary.

## Review judgment

The fourth layer contains too much parsing, but the excess is narrower than the overall file sizes suggest. The large state machines mostly belong there. The problematic area is the repeated strict Unicode/path/request-line block on both sides and the server's manual field/body/framing work.

The strongest ownership conclusions are:

- Layer 4's `ParseContentLength` is not a good shared helper: most of it repeats layer 2, while its narrower accepted syntax is a compatibility fact that needs either observable lower-layer metadata or an explicit decision to accept the canonical broader form.
- Generic field, body, strict-conversion, and request-line primitives align with the raw HTTP model.
- Opt-in, user-oriented request-body and response conveniences align with the Mini HTTP API.
- Exact media type, logical-message semantics, and Connect-pair encoding align with the Network Protocol over HTTP contract. The existing cross-backend `NetworkProtocolHttp.h/.cpp` is relevant before an async-only shared header is considered.
- Connection, retry, polling, delivery, and lifecycle state align with layer 4.

This boundary would make layer 4 read primarily as protocol decisions over parsed values, while retaining strict compatibility behavior and binary-capable Mini HTTP APIs.
