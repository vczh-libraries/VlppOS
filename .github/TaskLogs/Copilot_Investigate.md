# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

investigate repro

# Windows `IAsyncSocket(Server|Client)`

Implement the first async TCP socket stage described by [TODO_SocketHttp_AsyncSocket.md](./TODO_SocketHttp_AsyncSocket.md).

## Scope

- Implement the common binary async-socket interfaces and the Windows implementation only.
- The authoritative parts of `TODO_SocketHttp_AsyncSocket.md` are the interface proposal, its byte-stream/callback contract, the common implementation rules under `Async Socket Implementations`, and the `Windows` section.
- All paths in that document are relative to `Source/InterProcess`, as stated by [TODO_SocketHttp.md](./TODO_SocketHttp.md).
- Resolve its namespace placeholder as:
  - `vl::inter_process::async_tcp_socket` for the common interfaces.
  - `vl::inter_process::async_tcp_socket::windows_socket` for the concrete Windows implementation.

The following work is explicitly out of scope:

- `INetworkProtocol(Server|Client) on IAsyncSocket(Server|Client)`. Do not create adapters, templates, framing, UTF-8 serialization, or make any change to the existing `INetworkProtocol*` implementation.
- Linux and macOS implementations, platform bindings, and active test cases.
- HTTP request/response or minimized HTTP layers from the other `TODO_SocketHttp_*.md` documents.
- Refactoring the existing NamedPipe or Windows HTTP transports.
- Generated files under `Release` and dependencies under `Import`.

## Files and public API

Create all product files in the `AsyncSocket` physical folder:

- `Source/InterProcess/AsyncSocket/AsyncSocket.h`
- `Source/InterProcess/AsyncSocket/AsyncSocket.Windows.h`
- `Source/InterProcess/AsyncSocket/AsyncSocket.Windows.cpp`

Use the repository's case-sensitive `.Windows.h` / `.Windows.cpp` convention. Do not put these files in the existing `Source/InterProcess/Windows` folder.

`AsyncSocket.h` should declare the proposal from `TODO_SocketHttp_AsyncSocket.md` without adding message or packet concepts:

- `AsyncSocketBuffer`
- `IAsyncSocketCallback`
- `IAsyncSocketConnection`
- `IAsyncSocketClient`
- `IAsyncSocketServer`

Reuse `ClientStatus` and `WaitForClientResult` from `Source/InterProcess/Channel.h`; do not duplicate or move these enums. This dependency does not put the `INetworkProtocol*` adapter in scope.

Expose these concrete Windows types in `vl::inter_process::async_tcp_socket::windows_socket`:

- `AsyncSocketServer(vint port)`, implementing `IAsyncSocketServer`. As with the existing transports, provide a default `OnClientConnected` that accepts the connection while allowing tests/applications to override it.
- `AsyncSocketClient(vint port)`, implementing `IAsyncSocketClient` and owning the single client connection returned by `GetConnection()`.

The concrete connection and IOCP operation-context types may remain implementation details. Validate that the port is in `1..65535` using the repository's crash-early conventions.

## Required contract

Follow the common contract in `TODO_SocketHttp_AsyncSocket.md`, including all of these details:

- The transport is an ordered, full-duplex byte stream. It exposes neither messages nor TCP packet boundaries.
- `InstallCallback` supports one non-owning callback at a time, calls `OnInstalled` when installing, and accepts `nullptr` to uninstall. Callback owners may be stack allocated.
- `BeginReadingLoopUnsafe` keeps exactly one read outstanding. Every `OnRead` gets a positive, borrowed block valid only for that callback, and the next read is posted only after `OnRead` returns.
- Use a fixed 64 KiB receive block in the Windows implementation. A completion may still deliver fewer bytes; this is only a deterministic upper bound for testing and not a protocol boundary.
- Permit one read and one write concurrently, but only one user write at a time. Reject a null buffer, a write on a stopped/disconnected connection, or a second outstanding `WriteAsync` with `CHECK_ERROR` instead of silently accepting invalid use. Do not add an empty-buffer requirement without first defining its completion semantics.
- Retain the exact `Ptr<AsyncSocketBuffer>` supplied to `WriteAsync`, handle every short OS write, and call `OnWriteCompleted` exactly once with that same object only after all bytes have been sent. A cancelled/incomplete write has no completion callback.
- Callbacks may run on arbitrary threads. Never call user code while holding an implementation lock.
- EOF or peer loss calls `OnDisconnected` exactly once. A fatal `OnError` is delivered before that disconnection; intentional cancellation during `Stop` does not call `OnError`.
- `Start` is the boundary that enables server callbacks, so construction never starts callbacks against a not-yet-constructed derived server.
- `Stop` is idempotent and is a hard drain boundary: prevent new work, cancel native work, consume every final completion, and guarantee that no new or other callback can touch the connection, server, client, or callback owner after it returns.
- Preserve the special rule for calling `Stop` from one of the connection's own callbacks: the invoking callback is the sole exception to the previous guarantee. Do not wait for that current callback, do wait for all other callbacks, perform the one terminal disconnection notification before returning, and keep detached native state alive until the current callback and cancellation completions unwind. A later `Stop` from another thread must still be able to wait for that detached callback to finish.
- Destructors of the concrete connection, client, and server must call their idempotent draining `Stop` path rather than relying on callers to do so.

Use explicit ownership for accepted connections, native handles, operation contexts, receive storage, and retained write buffers. Do not depend on a callback owner or connection being heap allocated.

## Windows implementation

Implement IPv4 TCP on numeric `127.0.0.1` only, using the user-specified port. Do not resolve `localhost`, bind to all interfaces, or add IPv6/TLS behavior.

Follow the Windows mapping in `TODO_SocketHttp_AsyncSocket.md`:

- `AsyncSocket.Windows.h` must include Winsock 2 headers before any header that can include `windows.h`. Link `Ws2_32.lib` from `AsyncSocket.Windows.cpp` with `#pragma comment(lib, ...)`.
- Balance `WSAStartup` / `WSACleanup` only after all sockets, operations, and workers using the runtime have stopped. Avoid global objects with constructors/destructors.
- Use `WSASocketW` with `WSA_FLAG_OVERLAPPED`, an IOCP, and a small bounded set of completion workers. Do not create one thread per connection and never block an IOCP worker waiting for a connect retry or user action.
- Server setup is synchronous in `Start`: apply `SO_EXCLUSIVEADDRUSE`, bind to `INADDR_LOOPBACK`, listen, associate the listener with the IOCP, load `AcceptEx` with `WSAIoctl`, and keep one zero-initial-data accept pending. On completion, apply `SO_UPDATE_ACCEPT_CONTEXT`, associate the accepted socket with the IOCP, repost the next accept while still running, then invoke `OnClientConnected`; close and drain a rejected connection.
- `WaitForServer` changes `Ready` to `WaitingForServer`, loads `ConnectEx` with `WSAIoctl`, associates a fresh socket with the IOCP, binds it to an ephemeral local endpoint, posts `ConnectEx`, and blocks only its caller. On success, apply `SO_UPDATE_CONNECT_CONTEXT`, change to `Connected`, call `OnConnected`, and then unblock the caller.
- Put retry count/delay in named common policy constants so future platform bindings share the policy; do not bury them in Windows completion code. Schedule retries asynchronously and create a fresh socket for each failed attempt. Report intermediate failed attempts through `OnError(error, false)`. On bounded terminal failure, report one `OnError(error, true)`, change to `Disconnected`, call `OnDisconnected` once, and unblock `WaitForServer`.
- `Stop` must cancel and drain a pending retry timer/work item as well as native socket operations. Every retry callback must recheck stopping state before creating or posting a new socket.
- Implement reads with overlapped `WSARecv`; positive completions become `OnRead` and a zero-byte completion is EOF.
- Implement writes with overlapped `WSASend`, resubmitting the unsent suffix after a short completion.
- Closing/cancelling a socket does not finish cleanup by itself. Drain the resulting IOCP packets, treat expected `WSA_OPERATION_ABORTED` results as shutdown bookkeeping, and release each `OVERLAPPED` context and buffer only after its final completion has been consumed.
- Accepted server connections are already connected before their callbacks are installed in `OnClientConnected`, so those callbacks do not receive a second `OnConnected`. The client callback receives `OnConnected` before `WaitForServer` unblocks.
- Server shutdown drains the pending accept and every accepted connection before it stops the IOCP workers and releases the Winsock runtime.

## Project and Solution Explorer integration

Update `Test/UnitTest/UnitTest/UnitTest.vcxproj` explicitly (no wildcards):

- Add `AsyncSocket.Windows.cpp` and `Test/Source/TestInterProcess_AsyncSocket.cpp` as `ClCompile` entries.
- Add `AsyncSocket.h` and `AsyncSocket.Windows.h` as `ClInclude` entries.

Update `Test/UnitTest/UnitTest/UnitTest.vcxproj.filters` so the files are organized in Solution Explorer as well as on disk:

- Create `Common\InterProcess\AsyncSocket` and put all three product files there.
- Create `Source Files\AsyncSocket` and put `TestInterProcess_AsyncSocket.cpp` there.

No change to `Test/UnitTest/UnitTest.sln` is needed. Add `../../Source/InterProcess/AsyncSocket/AsyncSocket.Windows.cpp` to `CPP_REMOVES` in `Test/Linux/vmake` so the existing Linux project generator does not attempt to compile this Windows-only file. This is exclusion bookkeeping only: do not add Linux code or a Linux test case, and do not edit generated `vmake.txt` or `makefile` files.

## Shared test design

Create `Test/Source/TestInterProcess_AsyncSocket.cpp`. Follow the structure of `Test/Source/TestInterProcess.cpp`, but design new socket-specific scenarios rather than reusing its chat scenario.

- Include `AsyncSocket.h` and the common threading primitives needed by the harness unconditionally.
- Keep deterministic payload builders, shared state, callback implementations, bounded wait/error handling, and factory-driven runners such as `RunAsyncSocket(...)` outside platform guards.
- Make runners accept factories returning `Ptr<IAsyncSocketServer>` and `Ptr<IAsyncSocketClient>` rather than constructing Windows types directly. Pass the platform implementation's maximum read-block size into the shared long-data runner instead of hardcoding the Windows value in platform-neutral code.
- Put only the Windows implementation include, thin concrete server subclasses/factories, timed-wait binding, and active `TEST_CASE` invocations under `#ifdef VCZH_MSVC`.
- A future Linux or macOS implementation must be able to add its include/factories and invoke the same runners without copying the scenarios.
- Use `EventObject` milestones and bounded waits. Record the first asynchronous failure in lock-protected shared state and assert it on the test thread; do not use `TEST_ASSERT` on an IOCP callback thread.
- Every timeout path must stop the client and server, wait for runner tasks to exit, and detach callbacks before the shared stack state is destroyed. Do not add sleeps to make shutdown appear safe.
- Repeat the primary transfer and shutdown-drain scenarios (use 20 iterations, consistent with existing inter-process tests) and use a different fixed loopback port for each iteration to avoid immediate port-reuse/TIME_WAIT interference. The focused retry/reject cases do not need the same expensive repetition.

### Full-duplex long-data case

This is the primary case and must deterministically produce multiple `OnRead` callbacks.

- Build two different binary payloads, one for each direction. Each must be larger than eight 64 KiB receive blocks and have an odd-sized tail (for example `8 * 65536 + 257` and `11 * 65536 + 113`). Fill them with deterministic patterns that cover `0x00`, high-bit bytes, and every other byte value.
- Start the server first. In `OnClientConnected`, install the server callback, start its read loop, retain the accepted interface pointer only while the server owns the connection, and signal an explicit accepted/callback-installed/read-started milestone.
- Install the client callback before `WaitForServer`; after it returns, verify `ClientStatus::Connected`, start the client read loop, and wait for the server milestone before submitting one long write from each side so read and write activity overlaps. `WaitForServer` alone does not prove that `OnClientConnected` has finished.
- Drop the caller's local `Ptr<AsyncSocketBuffer>` immediately after each submission. The test should keep only independent expected bytes and the raw identity needed to prove that the implementation retains the buffer until `OnWriteCompleted`.
- Treat every read boundary as arbitrary. For every callback, require `size > 0`, prevent overflow, compare the bytes at the current expected offset, accumulate the total, and count callbacks without assuming an exact count.
- Assert exact byte order/content and total size in both directions, `OnRead` count greater than one in both directions, exactly one write completion per direction, and the identity/content of the returned write buffer.
- Also assert one callback installation per connection, one accepted connection, one client `OnConnected`, no `OnError`, `Ready` before connecting, `Connected` after `WaitForServer`, and `Disconnected` after stopping. Assert `!IAsyncSocketServer::IsStopped()` after `Start` and `IAsyncSocketServer::IsStopped()` after `Stop`; do not invent pre-`Start` semantics.
- After both transfers and write completions finish, stop while the continuous reads are pending. Require one disconnection per side, no cancellation error, and immediate safe callback detachment/destruction after `Stop` returns.

### Reject case

- Override `OnClientConnected` to return `WaitForClientResult::Reject` for one connection.
- Start the client read loop when connection establishment returns so it can observe the peer close.
- Assert one accept attempt, no delivered data or write completion, one eventual disconnection, and `!IAsyncSocketServer::IsStopped()` until the server is explicitly stopped. A client-side rejection may appear as EOF or as a fatal connection-reset error; if `OnError` occurs, assert that it is fatal and precedes the single disconnection instead of requiring an error-free close.
- Do not assert a transient client status or the relative ordering between `WaitForServer` returning and the rejected socket reaching EOF; only assert the final state.

### Stop-from-callback case

- Have the server send data after accepting a client.
- On the first client `OnRead`, without holding the test callback's lock, call that same connection's `Stop`, record that nested `Stop` returned, and signal an event. `OnDisconnected` is intentionally allowed to be reentrant during this call.
- The bounded event must prove there is no deadlock. After it signals, call the same idempotent `Stop` again from the test thread so this external call drains the still-unwinding `OnRead` before callback detachment.
- Assert exactly one read callback, no cancellation error, exactly one `OnDisconnected` delivered before the nested `Stop` returns, and no new ordinary callback after it. Do not forbid the server write completion: it may legitimately finish before or concurrently with the client read-side stop.
- Keep callback counters and the post-stop flag alive through the external drain and the subsequent server stop, then detach/destroy callback state after the runner joins.

### Connect-retry and retry-stop cases

- Start `WaitForServer` on a runner task without a server listening. The first nonfatal `OnError` is the event-driven milestone proving that a failed `ConnectEx` completed without blocking the IOCP worker.
- In the retry-success variant, start the server after that milestone and require a later retry to connect, deliver `OnConnected`, unblock `WaitForServer`, and support a small byte transfer.
- In the retry-stop variant, call `Stop` after the first nonfatal failure. Require `WaitForServer` to unblock, one terminal disconnection, no fatal error caused by intentional cancellation, no later retry/callback, and safe destruction immediately after the external drain.

Do not add tests for an exact TCP/read chunk count or ambiguous/invalid call sequences beyond the explicit preconditions above.

## Verification

- Build `Test/UnitTest/UnitTest.sln` on Windows through `.github/Scripts/copilotBuild.ps1` for Debug/Release and Win32/x64.
- Run the focused `TestInterProcess_AsyncSocket.cpp` cases first through `.github/Scripts/copilotExecute.ps1` in Debug x64, making sure the new test is not skipped by any `.vcxproj.user` filter.
- Then run the complete Debug x64 `UnitTest` suite.
- Read the ends of `Build.log` and `Execute.log`; all tests must pass and the Debug log must contain no CRT memory-leak report.
- Linux and macOS builds/tests are not part of this task.

# TEST [CONFIRMED]

Create `Test/Source/TestInterProcess_AsyncSocket.cpp` as a platform-neutral, factory-driven harness. Until the requested public header exists, the file uses a compile-safe fallback test that fails explicitly and proves that the AsyncSocket feature is absent. Once the header is supplied by a proposal, the same test source compiles the following real scenarios and the fallback disappears from the active build:

1. **Full-duplex long transfer:** exchange two deterministic binary buffers concurrently, sized `8 * maximumReadBlock + 257` and `11 * maximumReadBlock + 113`. Verify every byte in order, more than one `OnRead` in each direction, exactly one completion with the original retained buffer in each direction, callback/connection counts and final shutdown state. Repeat 20 times on distinct loopback ports.
2. **Rejected connection:** reject one accepted socket and verify one accept, no delivered data/write completion, one final client disconnection, and a still-running server before explicit shutdown. Permit either EOF or one fatal reset error, but require any fatal error to precede disconnection.
3. **Stop inside `OnRead`:** call the receiving connection's `Stop` from its first read callback, prove the nested call returns without deadlock and delivers the sole terminal notification before returning, then call `Stop` again externally to drain the callback before detaching its stack-owned callback. Repeat 20 times on distinct ports.
4. **Retry then connect:** begin `WaitForServer` with no listener, use the first nonfatal connect error as the event milestone, start a server, and verify a later attempt connects, unblocks the waiter and transfers bytes.
5. **Stop during retry:** after the first nonfatal connect error, stop the connection and verify the waiter unblocks, there is no cancellation-induced fatal error, one disconnection occurs, and no callback runs after the external stop boundary.

All asynchronous paths record the first error in lock-protected shared state; assertions run only on the unit-test thread. `EventObject` milestones have bounded Windows waits, waiter tasks always expose an exited event, cleanup is idempotent on timeout, and no sleeps or polling are used.

The problem is confirmed when the initial Debug x64 build succeeds and the focused fallback test fails because `Source/InterProcess/AsyncSocket/AsyncSocket.h` is absent. The proposal succeeds only when all five real scenarios pass, the complete Debug x64 suite passes with no CRT leak report, and Debug/Release Win32/x64 builds all succeed.

## Confirmation

The Debug x64 unit-test project built successfully with zero warnings and zero errors. Running only `TestInterProcess_AsyncSocket.cpp` through `copilotExecute.ps1` selected the fallback test and failed at `IAsyncSocket is not implemented / Assertion failure: false`; all other test files were reported as skipped. This confirms that the requested public AsyncSocket feature and implementation are absent while the complete future-facing test source is present and buildable.

# PROPOSALS

- No.1 Implement a retained IOCP state machine with two-phase shutdown [CONFIRMED]

## No.1 Implement a retained IOCP state machine with two-phase shutdown

Add the common byte-stream interfaces in `AsyncSocket.h` and expose Windows server/client facades backed by private implementation objects. Each server and client owns one Winsock/IOCP runtime with one long-lived `Thread`-derived completion worker; accepted server connections share the server runtime, so there is no thread per connection. Runtime startup is explicit and runtime shutdown posts a cooperative IOCP sentinel, waits for the worker to exit, then closes the completion port and balances `WSACleanup`.

Represent every `AcceptEx`, `ConnectEx`, `WSARecv`, and `WSASend` request with a heap operation context containing its own `OVERLAPPED`, retained owner/state and native buffers. Pending-operation counters and manual-reset events keep contexts and socket state alive until the final IOCP packet is consumed. Reads reuse one fixed 64 KiB context and repost only after `OnRead` returns. Writes retain the caller's exact `Ptr<AsyncSocketBuffer>`, track an offset across short completions, clear the one-write gate before invoking the sole completion callback, and never complete a cancelled write.

Protect connection lifecycle, callback installation, socket ownership, active-callback counts and pending-operation counts with short lock sections. User callbacks execute outside locks. Ordinary callback entry rechecks the stopped state under the same lock, while terminal error/disconnection has a separate once-only path that preserves fatal-error-before-disconnection ordering. Accepted connections remain strongly owned by the server until server shutdown; operation contexts temporarily retain connection/client/server implementation state without creating permanent ownership cycles.

Implement `Stop` as a repeatable two-phase boundary. The first caller marks stopping and detaches/closes the socket so no new work can start. An external caller drains native completions and active callbacks before issuing the once-only terminal notification and returning. When called from the runtime worker during one of that connection's own callbacks, it skips only the wait that would depend on the current IOCP packet, performs `OnDisconnected` reentrantly before returning, and leaves state retained by the operation context. A later external `Stop` still executes the drain waits even though the stopped flag is already set. Server shutdown similarly closes the listener, drains the pending accept/user accept callback, stops every accepted connection, then stops its runtime.

Use `ConnectEx` with a fresh bound/associated socket per attempt. Put the retry count and delay in common named constants. A Windows thread-pool timer only schedules attempts and invokes no user code; cancellation disables and drains timer callbacks. Failed intermediate attempts report nonfatal `OnError`, terminal exhaustion reports one fatal error then disconnects, and connection stop always changes client status to `Disconnected` and releases `WaitForServer`.

Complete explicit Visual Studio project/filter entries for all product files, add Windows source exclusion bookkeeping to `Test/Linux/vmake`, and keep generated Linux files unchanged. Remove the temporary missing-header fallback from the active test path by making the new public header available, then use the already committed shared runners to validate the implementation.

### CODE CHANGE

Created `Source/InterProcess/AsyncSocket/AsyncSocket.h` with the platform-neutral binary stream interfaces, the retained `AsyncSocketBuffer`, and shared retry policy constants. Created `AsyncSocket.Windows.h` with Winsock-first includes and the concrete Windows server/client facades, and created `AsyncSocket.Windows.cpp` with an IPv4 loopback IOCP implementation. The Windows runtime balances Winsock startup, owns one completion worker and one serialized callback worker, associates every socket with its completion port, and retains per-operation state for `AcceptEx`, `ConnectEx`, `WSARecv`, and `WSASend` until the final completion is consumed.

The server performs synchronous listener setup only from `Start`, keeps one accept pending, owns every delivered connection, rejects connections by draining their connection state, and elects one caller to perform ordered listener/connection/runtime shutdown. The client creates a freshly bound and associated socket for each connect attempt, uses the shared bounded retry policy through a drained thread-pool timer, reports intermediate failures as nonfatal, and wakes `WaitForServer` only after `OnConnected` or terminal shutdown. Runtime and server finalization use once-only election plus manual-reset completion events so concurrent repeated `Stop` calls cannot terminate or release the IOCP ahead of another caller's drain.

Connection state permits one receive and one retained write concurrently. Critical sections serialize lifecycle changes with native operation submission without holding a spin lock across Winsock calls. Reads use a 64 KiB borrowed block and repost only after `OnRead` returns; writes retain the exact submitted buffer and resubmit an unsent suffix after a short completion. Terminal state gates new work before reporting an error, cancellation completions are bookkeeping only, and fatal errors precede the once-only disconnection callback. `Stop` detaches the socket, requests an orderly shutdown before closing it, drains native completions and callbacks, supports reentrant stop from the connection's current callback, and lets a later external call drain that callback before its non-owning callback owner is detached.

Activated the committed platform-neutral test harness by removing its temporary missing-header fallback. Tightened timeout cleanup so queued `WaitForServer` work is always joined, and retained the five factory-driven scenarios for long full-duplex transfer, rejection, stop inside `OnRead`, retry-success, and stop-during-retry. Added explicit product/test entries and Solution Explorer filters to `UnitTest.vcxproj` and `UnitTest.vcxproj.filters`, and excluded only the Windows implementation source from Linux generation through `Test/Linux/vmake`. No network-protocol adapter, HTTP layer, Linux/macOS binding, generated make input, or solution file was changed.

### CONFIRMED

The first focused implementation run exposed Windows error 64 after a completed transfer when the peer socket was closed while a read remained pending. Updating the stop path to request an orderly `shutdown(SD_BOTH)` before `closesocket` made the peer observe normal stream termination while the local close still cancelled and drained overlapped work. The focused Debug x64 run then passed all five async-socket cases, including 20 distinct-port full-duplex iterations with payloads larger than eight 64 KiB blocks in both directions and 20 distinct-port reentrant callback-stop iterations.

The complete unfiltered Debug x64 run passed 13/13 test files and 122/122 test cases. The end of `Execute.log` records the successful totals, and no `Execute.log.memoryleaks` file or CRT leak report was produced. `copilotBuild.ps1` succeeded with zero warnings and zero errors for Debug x64, Debug Win32, Release x64, and Release Win32; the ends of the overwritten `Build.log` were checked after each remaining matrix build. Independent final review found the API/scope, retained IOCP ownership, retry/terminal ordering, concurrent shutdown election, platform-neutral test structure, project filters, and Linux exclusion bookkeeping consistent with the task.
