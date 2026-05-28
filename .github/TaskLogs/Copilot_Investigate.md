# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

## Task 1

This task happens in `VlppOS` repo.

Add a helper function `Nullable<WString> HttpServerApi::GetUtf8Body(PHTTP_REQUEST pRequest)`. Make it static as possible.
- Use implementation from `HttpServer` for reading the complete utf8 body, raise exception as exactly what has been done in `HttpServer`.
Add a helper function `void HttpServerApi::SendResponseUtf8(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, WString body)`.
- Use `{ 200, WString::Unmanaged(L"OK"), body, L"application/json; charset=utf8" }` to call the original `SendResponse`.
In `HttpServer` and other places should use the above helper functions

## Task 2

This task happens in `GacUI` repo.
Release `VlppOS` and `VlppParser2 to `GacUI`.
`GacUI` already uses the above functions, ensure it compile.

# UPDATES

# TEST [CONFIRMED]

Use existing build coverage for the Windows HTTP server implementation and downstream compile coverage:

- Build `Test\UnitTest\UnitTest.sln` in `VlppOS` because `Source\InterProcess\Windows\HttpServerApi.Windows.*` and `HttpServer.Windows.cpp` changed.
- Run the `UnitTest` project in `VlppOS`. The existing inter-process tests cover HTTP server/client request and response paths.
- Regenerate `Release\VlppOS.*` so downstream repos can import the new API.
- Success criteria: the build succeeds, the `VlppOS` unit test passes without a memory leak report, and `Release\VlppOS.Windows.*` contains the new helpers.

# PROPOSALS

- No.1 Move UTF-8 body/JSON response helpers to `HttpServerApi` [CONFIRMED]

## No.1 Move UTF-8 body/JSON response helpers to `HttpServerApi`

Add `HttpServerApi::GetUtf8Body(PHTTP_REQUEST)` as a shared body-reading helper and keep it as close to static as the Windows API allows: it is an instance method because `HttpReceiveRequestEntityBody` requires the request queue handle owned by `HttpServerApi`. Add static `HttpServerApi::SendResponseUtf8` to wrap `SendResponse` with the common JSON UTF-8 response shape. Refactor `HttpServer` to reuse these helpers where the original code only needed a successful JSON response or the shared body reader.

### CODE CHANGE

- Added `HttpServerApi::GetUtf8Body(PHTTP_REQUEST)` and moved the existing `/Response` UTF-8 body reading and validation logic into it, preserving the original `CHECK_ERROR` messages.
- Added static `HttpServerApi::SendResponseUtf8(HANDLE, HTTP_REQUEST_ID, WString)` using `{ 200, WString::Unmanaged(L"OK"), body, L"application/json; charset=utf8" }`.
- Updated `HttpServerConnection::SubmitResponse` to call `GetUtf8Body(pRequest).Value()` before delivering the string to the callback or queue.
- Updated JSON response sends in `HttpServerConnection::OnNewHttpRequestForPendingRequest` and `/Connect` handling to call `SendResponseUtf8`.
- Regenerated `Release\VlppOS.Windows.h` and `Release\VlppOS.Windows.cpp` from `Release\CodegenConfig.xml`.

### CONFIRMED

Built `Test\UnitTest\UnitTest.sln` with `copilotBuild.ps1`: build succeeded with 0 warnings and 0 errors. Ran `UnitTest` with `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest`: the active `TestInterProcess.cpp` filter passed 1/1 test files and 4/4 test cases, and `Execute.log.memoryleaks` was empty. Ran `CodePack.backup.exe` on `Release\CodegenConfig.xml`; `Release\VlppOS.Windows.h` and `Release\VlppOS.Windows.cpp` now contain `HttpServerApi::GetUtf8Body` and `HttpServerApi::SendResponseUtf8`.
