# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

You are going to debug the UnitTest process and find why sometimes (about 10%) it hit memory errors. Please avoid anything like gflags that could trigger UAC, since my computer will automatically cancel all UAC prompts. And this is my own project since you already have the source, not trying to reverse engineering others. figure out the issue.

# UPDATES

## UPDATE

a potential issue could be a thread copying `IHttpRequestConnection*` while another is deleting it, so maybe a fix would be using Ptr to hold such object, but figure out the issue first would help you fixing it more correctly

# TEST [CONFIRMED]

Run the existing `UnitTest` process repeatedly from the prescribed repository scripts until the intermittent memory error is reproduced. Capture the exact failing test/output and, without using any elevation-dependent system setting such as GFlags, use CDB or source-level instrumentation to identify the first invalid memory operation and its ownership/lifetime path.

Success requires a deterministic or high-confidence reproduction, an evidence-backed root cause, a focused regression test when practical, and repeated clean `UnitTest` runs after the selected correction.

The existing `SocketHttp portable pair (NetworkProtocol)` case in `TestInterProcess.cpp` is sufficient to reproduce the problem. A Debug x64 full-suite run passed 278/278 cases, but a focused CDB run of the existing Win32 Release executable failed on the first attempt at that case with access violation `0xC0000005` in `ntdll32!RtlpWaitOnCriticalSection`.

The captured stack was:

- `SocketHttpRequestContext::Impl::Finish` at `AsyncSocket_HttpServerApi.cpp:608`
- `HttpRequestConnection::Stop`
- `HttpRequestConnection::StopConnection` while entering `Lifecycle::lockState`

`Finish` had copied `connection == 0x0085e950` from `ConnectionState::connection` under the lock and called it after releasing the lock. The object still had the `HttpRequestConnection` vtable, but its `lifecycle` `Ptr` storage referred to memory whose destructor was `ReferenceCounterOperator<HttpRequestTimeoutController::State>::DeleteReference`. This proves the raw connection pointer was used after the `HttpRequestConnection` adapter had been destroyed and its storage reused. No GFlags, elevation, UAC prompt, or machine-wide debugger setting was used.

# PROPOSALS

- No.1 Preserve the original HTTP adapter ownership through connection admission

## No.1 Preserve the original HTTP adapter ownership through connection admission

The use-after-free is not fixed safely by constructing a new `Ptr<IHttpRequestConnection>` from the raw callback argument. `HttpRequestConnection` is an `Object`, so a second raw-pointer-to-`Ptr` conversion would create an independent reference counter and eventually double-delete the same object.

Instead, change `HttpRequestServer::OnClientConnected` to receive `Ptr<IHttpRequestConnection>` converted from the already-existing `Ptr<HttpRequestConnection>`. This preserves the adapter's original reference counter through the admission callback. `SharedServer` then passes that `Ptr` into `SocketHttpServerApiDispatcher`, whose `ConnectionState` stores it. Every operation copies the `Ptr` under `ConnectionState::lock` before releasing the lock and calling `SendResponse` or `Stop`; `OnDisconnected` clears the stored `Ptr`. This covers the asynchronous handoff without relying on the server's independently changing connection list and without creating a callback ownership cycle.

Update the HTTP request server overrides and the HTTP/Mini HTTP knowledge-base contract to reflect the owning admission argument. Verify the existing focused Release reproduction, the full Debug x64 suite and repeated focused runs.

### CODE CHANGE
