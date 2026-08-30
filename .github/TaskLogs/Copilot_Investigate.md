# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

You are going to debug the UnitTest process and find why sometimes (about 10%) it hit memory errors. Please avoid anything like gflags that could trigger UAC, since my computer will automatically cancel all UAC prompts. And this is my own project since you already have the source, not trying to reverse engineering others. figure out the issue.

# UPDATES

## UPDATE

a potential issue could be a thread copying `IHttpRequestConnection*` while another is deleting it, so maybe a fix would be using Ptr to hold such object, but figure out the issue first would help you fixing it more correctly

## UPDATE

there could be an issue where OnClientConnected receives a "local client", but if changing the interface and all implementation to Ptr is necessary, I would suggest also change local client functions to accept Ptr as well

## UPDATE

not just patch http

## UPDATE

or INetworkProtocolServer, therefore IChannelServer layer as well?

## UPDATE

I would like you to run 25 times and make sure every time it just work

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

- No.1 [CONFIRMED] Preserve original ownership through every connection admission layer

## No.1 [CONFIRMED] Preserve original ownership through every connection admission layer

The use-after-free is not fixed safely by constructing a new `Ptr<IHttpRequestConnection>` from the raw callback argument. `HttpRequestConnection` is an `Object`, so a second raw-pointer-to-`Ptr` conversion would create an independent reference counter and eventually double-delete the same object.

Instead, make every connection admission callback carry the producer's existing owning pointer: `IAsyncSocketServerCallback::OnClientConnected(Ptr<IAsyncSocketConnection>)`, `HttpRequestServer::OnClientConnected(Ptr<IHttpRequestConnection>)`, `INetworkProtocolServer::OnClientConnected(Ptr<INetworkProtocolConnection>)`, and the local-client parameter of `IChannelServer::OnClientConnected(..., Ptr<IChannelClient<TPackage>>)`. Update every implementation and test override so ownership is preserved from each concrete producer rather than reconstructed from a raw pointer.

At the failing Mini HTTP layer, `SharedServer` passes the HTTP `Ptr` into `SocketHttpServerApiDispatcher`, whose `ConnectionState` stores it. Every operation copies the `Ptr` under `ConnectionState::lock` before releasing the lock and calling `SendResponse` or `Stop`; `OnDisconnected` clears the stored `Ptr`. Apply the same principle to other adapter state that currently keeps an admitted connection beyond the callback. This covers asynchronous handoffs without relying on independently changing server connection lists and without creating callback ownership cycles.

Update all affected server overrides, examples, and inter-process knowledge-base contracts to reflect the owning admission arguments. Verify the existing focused Release reproduction, the full Debug x64 suite and repeated focused runs.

### CODE CHANGE

- `IAsyncSocketServerCallback`, `HttpRequestServer`, `INetworkProtocolServer`, and `IChannelServer` admission callbacks now receive the producer's existing `Ptr`. Windows, Linux, macOS, stdio, named-pipe, Windows HTTP, portable Socket HTTP, channel, and test implementations pass that same owning reference instead of reconstructing ownership from a raw pointer.
- `SocketHttpServerApiDispatcher::ConnectionState` now stores `Ptr<IHttpRequestConnection>`. Response, automatic-write, completion, error, and cancellation paths copy that `Ptr` while holding the state lock and use it after unlocking, so disconnect cannot delete the adapter between lookup and use.
- `NetworkProtocolChannelServer::Connection` retains `Ptr<INetworkProtocolConnection>`, validates the raw `OnInstalled` identity without creating a second reference counter, and local admission forwards `Ptr<IChannelClient<TPackage>>` unchanged.
- The strict Socket HTTP wire-form test now waits for the server-side asynchronous poll-response completion before destroying its one-shot raw client. Repeated immediate process launches exposed that the helper could close a successfully received physical response before the server completion callback, causing the server's intentionally conservative failed-delivery requeue to duplicate the previous message. This was a test-lane teardown race, distinct from the confirmed production use-after-free.
- Public examples and knowledge-base contracts were updated for the owning callback arguments.

### CONFIRMED

- CDB reproduced the original Win32 Release access violation on the first focused attempt and proved that `SocketHttpRequestContext::Impl::Finish` called a destroyed `HttpRequestConnection` whose storage had already been reused. CDB was run directly; no GFlags, UAC, elevation, or machine-wide debugger setting was used.
- Debug x64 built with 0 warnings/errors and the leak-checking full suite passed 278/278 with no CRT leak markers.
- Win32 Release built with 0 warnings/errors and the full suite passed 279/279.
- The final uninstrumented Win32 Release executable ran the affected `TestInterProcess.cpp` suite 25 consecutive times. Every process exited with code 0 and passed 44/44 cases, for 1,100 focused case executions with no access violation, assertion failure, timeout, or memory error.
