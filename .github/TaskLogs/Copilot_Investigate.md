# !!!INVESTIGATE!!!
# PROBLEM DESCRIPTION

## Task 3

This task happens in `VlppOS` repo.

- Windows only simple HTTP server APIs, and refactor `HttpServer` `INetworkProtocolServer` implementation.
  - One `HttpServerApi` struct works for one `http://host:port/prefix/.../` prefix.
  - The construction does the initialization and the destructor does the finalization.
  - Delete copy/move ctor, delete copy/move operator=.
  - There will be one callback (abstract function) exposing http server API raw objects to handle the request. One helper function to send status code, optional utf8 reason and utf8 body back, `WString` instead of `U8String`.
  - Optional `HttpVerbOPTIONS` switch. It works for all url under this prefix, it will not passed to the callback.
  - `HttpServerApi.Windows.(h|cpp)`.
Everything should be in `vl::inter_process` namespace.

# UPDATES

# TEST [CONFIRMED]

Use the existing inter-process HTTP unit tests as coverage:

- `HttpServer (NetworkProtocol)` proves the refactored server still accepts HTTP clients, receives `/Response` payloads, and dispatches `/Request` data.
- `HttpServer (Channel)` proves the HTTP server still works through the channel abstraction.
- The full `UnitTest` executable must pass, and `Execute.log` must not contain memory leak reports after the test summary.

# PROPOSALS

- No.1 Extract reusable `HttpServerApi` [CONFIRMED]

## No.1 Extract reusable `HttpServerApi`

Move the Windows HTTP Server API setup, teardown, async request receive loop, OPTIONS handling, and generic response helper into `HttpServerApi.Windows.(h|cpp)`. The existing `HttpServer` will own an instance of a derived API callback object for the `http://localhost:port/baseUrl/` prefix and keep only the inter-process protocol routing and connection state.

### CODE CHANGE

- Added `Source/InterProcess/Windows/HttpServerApi.Windows.h` and `.cpp` to own the Windows HTTP Server API initialization, URL-prefix registration, async receive loop, OPTIONS responses, shutdown, and UTF-8 response helper.
- Refactored `HttpServer` to derive from `HttpServerApi`, register one `http://localhost:port/baseUrl/` prefix, and keep only inter-process routing for `/Connect`, `/Request/{guid}`, and `/Response/{guid}`.
- Updated the unit-test project, filters, Linux source exclusion list, and include-only release aggregators for the new Windows-only source file.

### CONFIRMED

Built `Test/UnitTest/UnitTest.sln` with `copilotBuild.ps1`: build succeeded with 0 warnings and 0 errors. Ran `UnitTest` with `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest`: all 12/12 test files and 115/115 test cases passed. The tail of `Execute.log` ended at the passing summary and did not contain a memory leak report.
