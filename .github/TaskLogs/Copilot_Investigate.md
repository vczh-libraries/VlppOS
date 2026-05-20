# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

- you have to follow `REPO-ROOT/.github/Guidelines/Coding.md` when coding.
- you have to run unit test to make sure your change works.
- you have to commit and push all local changes after finishing any task, before doing the next task.
  - It is important to do task one by one strictly, by me designing tasks in this way, we can achieve:
  - Easy-to-understand commits for file changing that is easy to review.
  - Limit side effects so that you don't have to deal with massive of issues at the same time.
- each task will be treated as a new `# Repro`, that is, to wipe the document before execution.

All tasks below are for completing `vl::inter_process`.
`UnitTest` test project has been configured to only run `TestInterProcess.cpp` under debug x64.
I think this is the only test file you need.

## Task 1

Refactor `HttpClient`.
- In `HttpClient::BeginReadingLoopUnsafe` and `HttpClient::SendString` there is two similar `WinHttpSetStatusCallback` calls.
  - In `HttpClient::WaitForServer` there is one more `WinHttpSetStatusCallback` calls with a much simpler handling.
  - I think there is no reason to have 3 different `WinHttpSetStatusCallback` calls as their handling should be almost the same.
  - I would like you to make a protected `HttpClient::SendHttpRequest` function to send all different requests to the server. In this function you can pass an enum to distinguish Connect/Request/Response so that when a full body is received different actions could be taken. And `/Request` doesn't need to call `callback->OnReadString` because `HttpServer` always send messages through `/Response`.
  - Currenty calling to `WinHttpSetStatusCallback` is incorrect. It uses `self->QueueCallback` to block `Stop` but it is not correct. The correct way to handle it is that:
    - In `HttpClient::SendHttpRequest` checks state and ignore if already stopping.
    - `BeginPendingCallback`.
    - If anything error happens before `WinHttpSendRequest` in that function, `EndPendingCallback`.
    - If no error happens before `WinHttpSendRequest`, `EndPendingCallback` will be called when `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` happens, as in the document this status will always be invoked no matter what happens.
    - No `self->QueueCallback` is needed as the piece of code should just be executed in the `WinHttpSetStatusCallback` directly. Calling `ThreadPoolLite::Queue` for any callback is just a waste of resource.
- `hEventWaitForServer` should use `EventObject` instead of `HANDLE`.

Refactor `HttpServer`.
- All interlocked operations should be replaced by `atomic<T>`.

Refactor `NamedPipe.Windows.cpp`.
- All interlocked operations should be replaced by `atomic<T>`.

Remove all 6 `Thread::Sleep(1000);` in TestInterProcess.cpp.

The goal is to make sure all `Stop` actually waits until no more callback could happen.

# UPDATES

# TEST [CONFIRMED]

Use the existing `Test\Source\TestInterProcess.cpp` unit tests because the `UnitTest` project is configured to run this file under debug x64 for the inter-process work.

The criteria for success are:
- `NamedPipe (NetworkProtocol)` and `NamedPipe (Channel)` still pass after replacing registered wait-handle interlocked swaps with atomics.
- `HttpServer (NetworkProtocol)` and `HttpServer (Channel)` still pass after consolidating `HttpClient` WinHTTP request handling and replacing server registered wait-handle interlocked swaps with atomics.
- `Stop()` waits until registered callbacks cannot execute again, including WinHTTP request callbacks that are pending after `WinHttpSendRequest`.
- `TestInterProcess.cpp` no longer contains the six `Thread::Sleep(1000);` delays.
- The unit test process exits without timeouts, crashes, or memory leak reports.

# PROPOSALS

- No.1 CONSOLIDATE CALLBACK LIFETIME AND ATOMIC WAIT HANDLES [CONFIRMED]

## No.1 CONSOLIDATE CALLBACK LIFETIME AND ATOMIC WAIT HANDLES

Refactor `HttpClient` to send `/Connect`, `/Request`, and `/Response` through one protected `SendHttpRequest` helper. The helper will install one WinHTTP status callback, begin pending callback tracking before sending, and release it only from `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` for successfully submitted requests. Error paths before `WinHttpSendRequest` will release the pending callback immediately.

Replace `hEventWaitForServer` with `EventObject` so synchronous `/Connect` waiting uses the library synchronization abstraction.

Replace registered wait-handle interlocked pointer exchanges in `HttpServer.Windows.cpp` and `NamedPipe.Windows.cpp` with standard atomic exchanges. Registered callbacks and `Stop()` will exchange these handles atomically before unregistering them.

Remove the six one-second sleeps from `TestInterProcess.cpp` so tests check that `Stop()` itself provides the required callback drain behavior.

### CODE CHANGE

Refactored `HttpClient` so `BeginReadingLoopUnsafe`, `WaitForServer`, and `SendString` all call `SendHttpRequest`. The helper creates one heap-allocated `HttpRequestContext` per WinHTTP request, installs one shared status callback, tracks callback lifetime with `BeginPendingCallback` before sending, and releases it from `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING`. It uses `WINHTTP_OPTION_CONTEXT_VALUE` so handle-closing callbacks can identify the context even around failed sends. Completed request handles are closed before invoking user read callbacks, allowing reentrant `SendString` calls from callbacks without keeping the completed request active.

Changed `HttpClient::Stop()` to cancel long-poll `/Request` handles while allowing already-started `/Response` sends to finish and drain their handle-closing callbacks. This keeps final client messages from being aborted when callers stop immediately after `SendString`.

Replaced `HttpClient::hEventWaitForServer` with `EventObject`.

Replaced registered wait-handle `InterlockedExchangePointer` usage in `HttpServer.Windows.cpp` and `NamedPipe.Windows.cpp` with `std::atomic_ref<HANDLE>::exchange`, preserving the `HANDLE*` storage required by `RegisterWaitForSingleObject`.

Changed `HttpServer` `/Response` reply handling to accept `ERROR_CONNECTION_INVALID` and `ERROR_OPERATION_ABORTED` after the request body has already been processed, because clients may now stop immediately after sending when the test no longer waits one second.

Removed all six `Thread::Sleep(1000);` calls from `TestInterProcess.cpp`.

### CONFIRMED

This proposal is confirmed. The consolidated `HttpClient::SendHttpRequest` removes the duplicated WinHTTP callback implementations and makes request lifetime tracking depend on `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING`. `Stop()` now cancels long-poll read requests while allowing already-started outgoing `/Response` sends to drain, so final messages are not lost when tests stop clients immediately after sending. `EventObject` replaces the raw wait event for `/Connect`, and registered wait callbacks in HTTP server and named pipe code now use standard atomic exchanges instead of interlocked pointer exchanges.

Verification succeeded:
- `copilotBuild.ps1` completed with `Build succeeded.`, `0 Warning(s)`, and `0 Error(s)`.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest` completed with `Passed test files: 1/1` and `Passed test cases: 4/4`.
