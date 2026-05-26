# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

## Task 2

This task happens in `VlppOS` repo.

- Windows only simple HTTP client APIs, and refactor `HttpClient` `INetworkProtocolClient` implementation.
  - Delete `HttpUtility.h` and `HttpUtility.Windows.cpp`.
  - The API design should be similar to above files but everything would be async.
  - Keep `UrlEncodeQuery`.
  - `HttpClientApi.Windows.(h|cpp)`.

Details:

I would like you to follow the design of `HttpUtility.h` but completely discard the implementation to complete the new API.
By saying `HttpQuery` should be async, I mean the last argumne would be a callback in `Func<void(Variant<HttpResponse, HttpError>)>`, returning `void`.
You are going to decide what could be in `HttpError`. Remember that getting a 404 doesn't count as an error, it counts as an success. Error means anything that is going wrong causing the underlying http client API not working.
If anything is needed from `HttpClient.Windows.cpp` but it does not appear in the original `HttpUtility.h` design, just add anything that is missing.
The underlying http client API needs initialization and finalization, you are just going to add a `HttpClientApi` struct with:
  - All necessary Windows API objects in it.
  - The construction does the initialization and the destructor does the finalization.
  - Delete copy/move ctor, delete copy/move operator=.
  - The class will be created for the same host:port only, sending http requests to different targets requires different instances of `HttpClientApi`.
  - `HttpQuery` becomes its method. Since `HttpClient` only uses utf8, it is OK to make this API only working with `WString`.
  - `HttpEncodeQuery` and `HttpDecodeQuery` becomes its static methods.
Everything should be in `vl::inter_process` namespace.

# UPDATES

- Added `Source\InterProcess\Windows\HttpClientApi.Windows.(h|cpp)` with async WinHTTP request handling, `HttpRequest`, `HttpResponse`, `HttpError`, static query encode/decode helpers, and `UrlEncodeQuery` compatibility in `vl::inter_process`.
- Deleted the old synchronous `Source\HttpUtility.h` and `Source\HttpUtility.Windows.cpp` and moved project/include-only references to the new API files.
- Refactored `HttpClient` to use `HttpClientApi` for `/Connect`, `/Request`, and `/Response`, while preserving the protocol behavior that non-200 statuses are handled as client connection/read failures.
- Added a per-request `keepAliveOnStop` option so `HttpClient` can let final `/Response` writes complete during `Stop`, matching the previous implementation's shutdown behavior.
- Hardened `HttpServerConnection::SubmitResponse` to accept request bodies that arrive either in initial HTTP request chunks or through `HttpReceiveRequestEntityBody`, which the new async client can expose.

# TEST

[CONFIRMED]

- `Source\HttpUtility.h` and `Source\HttpUtility.Windows.cpp` are deleted, and the unit-test project references the replacement API files.
- The new `HttpClientApi` initializes/finalizes WinHTTP resources, owns per-request async state, returns non-transport HTTP statuses (including 404) as `HttpResponse`, and reports only WinHTTP/underlying failures as `HttpError`.
- `HttpClient` uses `HttpClientApi` instead of directly owning WinHTTP request state.
- `..\..\.github\Scripts\copilotBuild.ps1` passes from `Test\UnitTest` with 0 warnings and 0 errors.
- `..\..\.github\Scripts\copilotExecute.ps1 -Mode UnitTest -Executable UnitTest` passes from `Test\UnitTest`: 12/12 test files and 115/115 test cases.
- The tail of `.github\Scripts\Execute.log` has no memory leak dump after the final test run.

# PROPOSALS

[PROPOSED]

- Add `InterProcess\Windows\HttpClientApi.Windows.(h|cpp)` with `HttpRequest`, `HttpResponse`, `HttpError`, and non-copyable/non-movable `HttpClientApi` in `vl::inter_process`.
- Keep the old request/response body conveniences (`SetBodyUtf8`, `GetBodyUtf8`) and expose `UrlEncodeQuery` through static `HttpClientApi::HttpEncodeQuery` plus a compatibility free function in the new namespace.
- Move the async WinHTTP callback/request lifetime management out of `HttpClient` and make each request complete exactly once through `Func<void(Variant<HttpResponse, HttpError>)>`.
- Refactor `HttpClient` to issue `/Connect`, `/Request`, and `/Response` through `HttpClientApi`, preserving the inter-process protocol’s expectation that non-200 protocol statuses are connection/read errors even though the lower API returns them as successful HTTP responses.
