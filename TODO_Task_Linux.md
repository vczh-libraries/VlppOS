investigate repro

# Linux `IAsyncSocket(Server|Client)`

Implement the Linux async TCP socket stage described by [TODO_SocketHttp_AsyncSocket.md](./TODO_SocketHttp_AsyncSocket.md).

## Scope

- Implement the Linux binding for the existing interfaces in `Source/InterProcess/AsyncSocket/AsyncSocket.h`.
- The authoritative parts of `TODO_SocketHttp_AsyncSocket.md` are the interface byte-stream/callback contract, the common rules under `Async Socket Implementations`, and the `Linux` section.
- Keep the existing common interface and cross-platform retry policy in `AsyncSocket.h`.
- Resolve the concrete namespace as `vl::inter_process::async_tcp_socket::linux_socket`.
- Add the small GCC timed-event prerequisite described below if it has not already been implemented by the macOS task.

The following work is explicitly out of scope:

- **`INetworkProtocol(Server|Client) on IAsyncSocket(Server|Client)`**. Do not create adapters, framing, UTF-8 serialization, or make any change to the existing `INetworkProtocol*` implementation.
- Windows or macOS async-socket implementation changes, except platform-selection bookkeeping needed to keep every target buildable.
- HTTP request/response or minimized HTTP layers from the other `TODO_SocketHttp_*.md` documents.
- Refactoring the existing NamedPipe or Windows HTTP transports.
- New test scenarios. The shared async-socket test cases already exist.
- Generated files under `Release`, dependencies under `Import`, and generated `Test/Linux/vmake.txt` or `Test/Linux/makefile` files.

## Files and public API

Create these product files in the existing `AsyncSocket` physical folder:

- `Source/InterProcess/AsyncSocket/AsyncSocket.Linux.h`
- `Source/InterProcess/AsyncSocket/AsyncSocket.Linux.cpp`

Expose these concrete types in `vl::inter_process::async_tcp_socket::linux_socket`:

- `AsyncSocketServer(vint port)`, implementing `IAsyncSocketServer`. Provide a default `OnClientConnected` that accepts the connection while allowing derived servers to override it.
- `AsyncSocketClient(vint port)`, implementing `IAsyncSocketClient` and owning the single connection returned by `GetConnection()`.

Keep the same port-only constructor shape as the Windows implementation. The shared test adapter intentionally requires the server and client to be constructible using only `vint port`, and requires the server to remain inheritable with an overridable `OnClientConnected`.

Keep native connection, ring, worker, and operation-context types private. Prefer the same PIMPL-shaped public header as the Windows implementation so `liburing` types do not leak into the public API. Validate the port in `1..65535` with `CHECK_ERROR`.

Use a fixed 64 KiB receive block and pass `65536` as the maximum read-block size when activating the shared tests. This is a deterministic upper bound, not a protocol boundary.

## Required contract

Preserve the existing common `IAsyncSocket*` contract:

- The transport is an ordered, full-duplex byte stream. It exposes neither messages nor TCP packet boundaries.
- `InstallCallback` supports one non-owning callback at a time, calls `OnInstalled` synchronously when installing, and accepts `nullptr` to uninstall. Callback owners may be stack allocated.
- `BeginReadingLoopUnsafe` keeps exactly one receive outstanding. Every `OnRead` receives a positive borrowed block valid only for that callback, and the next receive is submitted only after `OnRead` returns.
- Permit one read and one write concurrently, but only one user write at a time. Reject a null buffer, a write on a stopped/disconnected connection, or a second outstanding `WriteAsync` with `CHECK_ERROR`.
- Retain the exact `Ptr<AsyncSocketBuffer>` supplied to `WriteAsync`, handle every short send, and call `OnWriteCompleted` exactly once with that same object only after all bytes have been sent. A cancelled or failed write has no completion callback.
- Match the Windows empty-buffer behavior: reserve the one-write slot and schedule exactly one `OnWriteCompleted` with the same empty buffer without inventing a stream terminator or platform-only rejection.
- Callbacks may run on arbitrary threads. Never call user code while holding an implementation lock.
- EOF or peer loss calls `OnDisconnected` exactly once. A fatal `OnError` is delivered before that disconnection; intentional cancellation during `Stop` does not call `OnError`.
- `Start` is the boundary that enables server callbacks, so construction never starts callbacks against an incompletely constructed derived server.
- `Stop` is idempotent and is a hard drain boundary: prevent new work, cancel retry and native work, consume every final completion, and guarantee that no new or other callback can touch the connection, server, client, or callback owner after it returns.
- Preserve the special rule for `Stop` called from one of the connection's own callbacks. Do not wait for that current callback, do wait for all other callbacks, perform the one terminal disconnection notification before nested `Stop` returns, and retain detached native state until the current callback and cancellation completions unwind. A later `Stop` from another thread must still be able to finish that drain.
- Destructors of the concrete connection, client, and server call their idempotent draining `Stop` paths.

Use explicit ownership for accepted connections, file descriptors, the ring, operation contexts, receive storage, retry timeouts, and retained write buffers. Do not depend on a callback owner or connection being heap allocated.

## Linux implementation

Use `liburing` as the userspace interface to `io_uring`. The Linux target must include and link `liburing`.

### Ring and completion ownership

- Initialize one ring for a client runtime and one ring for a server runtime with `io_uring_queue_init_params`. A server runtime owns its listener and all accepted connections.
- Run one long-lived completion worker per runtime; never create one thread per connection. The completion worker is the sole CQ consumer and blocks with `io_uring_wait_cqe` rather than polling.
- Serialize SQ access and submission because public methods may be called from arbitrary threads. Wake a completion worker that has no ordinary request by submitting an explicit wake/stop operation; do not poll or sleep.
- Store a unique operation context in every SQE's `user_data`. Each context retains its owner, operation-specific native arguments, receive/write buffer, and any retry timeout storage until its CQE is consumed.
- Always call `io_uring_cqe_seen` and release an operation context only after its final completion handling is finished. Release a fully drained ring with `io_uring_queue_exit` last.
- No minimum Linux kernel is declared by this repository. Probe the initialized ring with `io_uring_get_probe_ring` / `io_uring_opcode_supported` and fail early when required accept, connect, receive, send, async-cancel, or timeout operations are unavailable.

### Server

- `Start` synchronously creates an IPv4 TCP listener, binds only to numeric `127.0.0.1:{port}`, calls `listen`, starts the completion runtime, and submits exactly one single-shot `io_uring_prep_accept` before returning.
- After a successful accept, rearm the next accept while the server is still running, wrap and retain the returned file descriptor, and then call `OnClientConnected`. Close and drain a rejected connection.
- Recoverable accept failures can rearm while running. Terminal failures make `IsStopped` reflect the failed server state.
- Never deliver `OnClientConnected` before `Start` or after `Stop`. Accepted server callbacks do not receive a second `OnConnected`.
- Server shutdown prevents rearming, cancels and drains the pending accept, then stops every accepted connection before joining the completion worker and releasing the ring.

### Client and retry

- `WaitForServer` changes `Ready` to `WaitingForServer`, creates a fresh IPv4 socket, and submits `io_uring_prep_connect` to numeric `127.0.0.1:{port}`. It may block only its caller while all connect, retry, and cancellation work remains asynchronous.
- A zero connect result changes the client to `Connected`, calls `OnConnected`, and then unblocks `WaitForServer`. A negative CQE result represents `-errno`.
- On a failed attempt, close the attempt socket after its operations drain and report `OnError(error, false)`. Schedule `AsyncSocketClientRetryDelay` with an `io_uring` timeout rather than `Thread::Sleep` or a blocking completion worker, then create a fresh socket for the next attempt.
- Use `AsyncSocketClientRetryCount` as the common bounded policy. On the terminal failure, report one fatal error, change to `Disconnected`, call `OnDisconnected` once, and unblock `WaitForServer`.
- Every connect and delayed-retry completion rechecks stopping state before creating or submitting new work. `Stop` cancels and drains the retry timeout as well as the current connect attempt, and unblocks `WaitForServer` without reporting intentional cancellation as an error.

### Reading and writing

- `BeginReadingLoopUnsafe` submits one single-shot `io_uring_prep_recv` into a retained 64 KiB block.
- A positive receive result becomes one `OnRead`; zero is EOF; a negative non-cancellation result is fatal. Rearm only after `OnRead` returns and only while the connection is still running.
- `WriteAsync` uses `io_uring_prep_send` with `MSG_NOSIGNAL`. Retain the exact input buffer, resubmit the remaining suffix after a short send, treat an impossible zero-progress send as terminal, and call `OnWriteCompleted` only after all bytes are sent.
- Expected `-ECANCELED` results during intentional shutdown are bookkeeping, not user errors.

### Cancellation and shutdown

- `Stop` first prevents all new submissions, then uses `io_uring_prep_cancel64` for every pending accept, connect, receive, send, retry-timeout, and wake request.
- Drain both the cancellation CQE and the target request's final CQE. Cancellation races such as `-ENOENT` or `-EALREADY` do not prove that the target has drained; track each target explicitly.
- If accept or connect succeeds concurrently with stopping, close the resulting file descriptor without dispatching an ordinary callback.
- Closing a file descriptor is not a substitute for cancelling an `io_uring` request. Close it only after no request can still reference it.
- A `Stop` invoked on the completion/callback worker must not join or synchronously wait on itself. Prevent further callbacks, detach and retain the remaining native state, and allow a later external idempotent `Stop` to complete the drain.

## Bounded event wait prerequisite

`EventObject::WaitForTime` is currently Windows-only, while the existing shared async-socket tests deliberately require a bounded platform wait. Do not bind the GCC test entry to unbounded `EventObject::Wait`, and do not implement the timeout with polling, sleeping tasks, or an orphaned waiter thread.

If this prerequisite is not already present:

- Expose `ConditionVariable::SleepWithForTime(CriticalSection&, vint)` and `EventObject::WaitForTime(vint)` for `VCZH_GCC` in `Source/Threading.h`. Do not broaden unrelated `WaitableObject` timed APIs.
- Implement both in `Source/Threading.Linux.cpp`, which is shared by Linux and macOS. Use `pthread_cond_timedwait` with a correctly normalized absolute deadline, return `false` on timeout/failure, and preserve the existing manual-reset/auto-reset event and waiter-counter behavior.
- Keep the event lock held according to the existing `ConditionVariable::SleepWith` contract and avoid any busy wait.

If the other platform task has already added these methods, reuse and verify them instead of creating another timed-wait implementation.

## Project and build integration

Update `Test/UnitTest/UnitTest/UnitTest.vcxproj` explicitly:

- Add `AsyncSocket.Linux.cpp` as `ClCompile`, excluded from all Debug/Release and Win32/x64 Windows builds.
- Add `AsyncSocket.Linux.h` as `ClInclude`.

Update `Test/UnitTest/UnitTest/UnitTest.vcxproj.filters` and put both files under `Common\InterProcess\AsyncSocket`. No solution change is needed.

Update `Test/Linux/vmake` without editing generated `vmake.txt` or `makefile`:

- Keep `AsyncSocket.Windows.cpp` excluded on every Unix-like target.
- On non-Darwin Linux, compile `AsyncSocket.Linux.cpp`, omit `AsyncSocket.macOS.cpp` if it exists, and link `liburing`.
- The shared make support expands `CPP_LINK_OPTIONS` before the object list. Do not place a plain as-needed `-luring` there because the linker can discard it before seeing references. Use `-Wl,--no-as-needed -luring` or an equivalent verified mechanism that retains/resolves `liburing`, and inspect the generated clean-build link command.
- On Darwin, omit `AsyncSocket.Linux.cpp` and do not link `liburing`. Preserve any macOS source, Blocks, and Network.framework configuration already present.
- Make platform selection conditional using `uname` and preserve/extend an existing platform block so the Linux and macOS tasks work in either order.

## Existing shared tests

Do not create, copy, or redesign test scenarios. Fill only the existing `VCZH_GCC && !VCZH_APPLE` placeholders in [TestInterProcess_AsyncSocket.cpp](./Test/Source/TestInterProcess_AsyncSocket.cpp):

- Include `AsyncSocket.Linux.h`.
- Bind `linux_socket::AsyncSocketServer` and `linux_socket::AsyncSocketClient` to the shared templated test entry.
- Keep both concrete objects port-only, bind bounded event waiting to the GCC `EventObject::WaitForTime` prerequisite above, and pass `65536` as the maximum receive block size.

Run the existing `TestInterProcess_AsyncSocket.cpp` cases after adding the Linux binding. From `Test/Linux`, use `.github/Ubuntu/build.sh -f` through the repository-prescribed absolute path, then run `Bin/UnitTest /C /F:TestInterProcess_AsyncSocket.cpp` asynchronously and check its output and exit status. Do not call CMake, make, Clang, GCC, or the unit-test executable from a different working directory.
