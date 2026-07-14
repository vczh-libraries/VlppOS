investigate repro

# macOS `IAsyncSocket(Server|Client)`

Implement the macOS async TCP socket stage described by [TODO_SocketHttp_AsyncSocket.md](./TODO_SocketHttp_AsyncSocket.md).

## Scope

- Implement the macOS binding for the existing interfaces in `Source/InterProcess/AsyncSocket/AsyncSocket.h`.
- The authoritative parts of `TODO_SocketHttp_AsyncSocket.md` are the interface byte-stream/callback contract, the common rules under `Async Socket Implementations`, and the `macOS` section.
- Keep the existing common interface and cross-platform retry policy in `AsyncSocket.h`.
- Resolve the concrete namespace as `vl::inter_process::async_tcp_socket::macos_socket`.
- Add the small GCC timed-event prerequisite described below if it has not already been implemented by the Linux task.

The following work is explicitly out of scope:

- **`INetworkProtocol(Server|Client) on IAsyncSocket(Server|Client)`**. Do not create adapters, framing, UTF-8 serialization, or make any change to the existing `INetworkProtocol*` implementation.
- Linux or Windows async-socket implementation changes, except platform-selection bookkeeping needed to keep every target buildable.
- HTTP request/response or minimized HTTP layers from the other `TODO_SocketHttp_*.md` documents.
- Refactoring the existing NamedPipe or Windows HTTP transports.
- New test scenarios. The shared async-socket test cases already exist.
- Generated files under `Release`, dependencies under `Import`, and generated `Test/Linux/vmake.txt` or `Test/Linux/makefile` files.

## Files and public API

Create these product files in the existing `AsyncSocket` physical folder:

- `Source/InterProcess/AsyncSocket/AsyncSocket.macOS.h`
- `Source/InterProcess/AsyncSocket/AsyncSocket.macOS.cpp`

Expose these concrete types in `vl::inter_process::async_tcp_socket::macos_socket`:

- `AsyncSocketServer(vint port)`, implementing `IAsyncSocketServer`. Provide a default `OnClientConnected` that accepts the connection while allowing derived servers to override it.
- `AsyncSocketClient(vint port)`, implementing `IAsyncSocketClient` and owning the single connection returned by `GetConnection()`.

Keep the same port-only constructor shape as the Windows implementation. The shared test adapter intentionally requires the server and client to be constructible using only `vint port`, and requires the server to remain inheritable with an overridable `OnClientConnected`.

Keep concrete connection, Network.framework objects, dispatch queues/timers, Blocks, and callback state private. Prefer the same PIMPL-shaped public header as the Windows implementation. Validate the port in `1..65535` with `CHECK_ERROR`.

Use the Network.framework C API from `<Network/Network.h>` and Grand Central Dispatch from `<dispatch/dispatch.h>`. Keep the implementation in C++ (`.cpp`); do not introduce Objective-C or Objective-C++.

Use a fixed 64 KiB maximum receive size and pass `65536` as the maximum read-block size when activating the shared tests. This is a deterministic upper bound, not a protocol boundary.

## Required contract

Preserve the existing common `IAsyncSocket*` contract:

- The transport is an ordered, full-duplex byte stream. It exposes neither messages nor TCP packet boundaries.
- `InstallCallback` supports one non-owning callback at a time, calls `OnInstalled` synchronously when installing, and accepts `nullptr` to uninstall. Callback owners may be stack allocated.
- `BeginReadingLoopUnsafe` keeps exactly one receive outstanding. Every `OnRead` receives a positive borrowed region valid only for that callback, and the next receive is scheduled only after all callbacks for the current result return.
- Permit one read and one write concurrently, but only one user write at a time. Reject a null buffer, a write on a stopped/disconnected connection, or a second outstanding `WriteAsync` with `CHECK_ERROR`.
- Retain the exact `Ptr<AsyncSocketBuffer>` supplied to `WriteAsync` and call `OnWriteCompleted` exactly once with that same object only after the entire write succeeds. A cancelled or failed write has no completion callback.
- Match the Windows empty-buffer behavior: reserve the one-write slot and schedule exactly one `OnWriteCompleted` with the same empty buffer without sending a stream terminator or adding a platform-only rejection.
- Callbacks may run on arbitrary threads. Never call user code while holding an implementation lock.
- EOF or peer loss calls `OnDisconnected` exactly once. A fatal `OnError` is delivered before that disconnection; intentional cancellation during `Stop` does not call `OnError`.
- `Start` is the boundary that enables server callbacks, so construction never starts callbacks against an incompletely constructed derived server.
- `Stop` is idempotent and is a hard drain boundary: prevent new work, cancel retry and native work, drain final handlers, and guarantee that no new or other callback can touch the connection, server, client, or callback owner after it returns.
- Preserve the special rule for `Stop` called from one of the connection's own callbacks. Do not synchronously wait on the serial queue that is executing the current callback, do wait for all other callbacks, perform the one terminal disconnection notification before nested `Stop` returns, and retain detached native state until the callback and cancellation handlers unwind. A later `Stop` from another thread must still be able to finish that drain.
- Destructors of the concrete connection, client, and server call their idempotent draining `Stop` paths.

Use explicit ownership for accepted connections, native objects, dispatch queues and timers, copied Blocks, receive content, and retained write buffers. Do not depend on a callback owner or connection being heap allocated. Break handler/native-object ownership cycles only after terminal cancellation has drained.

## Network.framework implementation

Implement plain IPv4 TCP on numeric `127.0.0.1` only. Create parameters with `nw_parameters_create_secure_tcp`, passing `NW_PARAMETERS_DISABLE_PROTOCOL` for TLS and the default TCP configuration. Enable Clang Blocks support and link Network.framework.

Give each listener and connection a serial dispatch queue. Public methods can still be called from arbitrary threads, so protect shared state and marshal Network.framework operations carefully. Never synchronously dispatch to or wait on the serial queue when already executing on that queue.

### Server

- In `Start`, create the specific local endpoint `127.0.0.1:{port}`, set it with `nw_parameters_set_local_endpoint`, create the listener, install its state and new-connection handlers, assign its serial queue, and call `nw_listener_start`.
- Do not configure the listener by port alone, because that may accept on non-loopback interfaces.
- Treat `ready` as the listener becoming operational. `Start` may return while readiness is pending, but after it starts the listener `IsStopped` must remain false unless explicit shutdown or a terminal setup failure occurs.
- Treat listener `failed` as terminal and make `IsStopped` reflect the underlying failure. Explicit `Stop` cancels the listener and drains its terminal `cancelled` state.
- For every delivered `nw_connection_t`, create and retain its wrapper, assign its serial queue and state handler, and finish configuring it before calling `OnClientConnected`.
- If accepted, retain the wrapper in the server and start the native connection. If rejected, cancel and drain it. Accepted server connections do not receive `OnConnected`.
- An accepted connection reaches `ready` asynchronously. Remember a `BeginReadingLoopUnsafe` request made by `OnClientConnected` and issue the first receive after `ready`.
- The shared test can also submit the server's one allowed write after acceptance but before `ready`. Retain that write and issue it when ready instead of rejecting a logically accepted connection.
- Server shutdown prevents new accepted callbacks, cancels and drains the listener, and separately cancels and drains every connection already delivered by it before returning.

### Client and retry

- Create the remote endpoint with `nw_endpoint_create_host` using numeric `127.0.0.1` and the decimal port, while keeping the public constructor port-only.
- `WaitForServer` changes `Ready` to `WaitingForServer`, creates and starts an asynchronous connection attempt, and blocks only its caller on repository synchronization primitives.
- On `ready`, change to `Connected`, call `OnConnected`, and then unblock `WaitForServer`.
- Give each attempt a generation so stale state handlers cannot mutate a replacement connection. Treat `waiting` as retryable: report `OnError(error, false)` once for that attempt, then restart it or cancel/drain it before creating a replacement.
- Schedule `AsyncSocketClientRetryDelay` with cancellable dispatch timer work; never sleep or block a Network.framework/dispatch callback queue. Recheck stopping state and the active generation before creating or restarting a connection.
- Use `AsyncSocketClientRetryCount` as the common bounded policy for retryable waiting attempts. Treat Network.framework `failed` as terminal, as required by `TODO_SocketHttp_AsyncSocket.md`; either a `failed` state or retry exhaustion reports one fatal error, changes to `Disconnected`, calls `OnDisconnected` once, and unblocks `WaitForServer`.
- `Stop` cancels and drains pending retry timer work and the active connection, changes status to `Disconnected`, and unblocks `WaitForServer` without reporting intentional cancellation as an error.

### Reading and writing

- Implement `BeginReadingLoopUnsafe` with `nw_connection_receive(connection, 1, 65536, ...)`.
- Traverse returned `dispatch_data_t` content with `dispatch_data_apply`. Call `OnRead` for each positive contiguous borrowed region while that region is valid.
- Content can arrive together with `is_complete` or an error. Deliver all content first, then enter the terminal path. This interface does not expose a read-half-closed state.
- Rearm the next receive only after every `OnRead` for the current receive returns and only while the connection is still running.
- Implement `WriteAsync` with `dispatch_data_create` and `nw_connection_send`, using an ordinary non-final stream context. Network.framework handles partial TCP writes internally.
- Keep both the native byte storage and the exact submitted `AsyncSocketBuffer` alive until the send completion. Do not assume `dispatch_data_create` copies the source bytes; use explicit retained storage/destructor ownership.
- A successful send completion clears the single-write state and calls `OnWriteCompleted` with the original buffer. A non-cancellation send error enters the single fatal-error/disconnection path.
- Receive/send errors caused by intentional `nw_connection_cancel` are shutdown bookkeeping and must not become user errors.

### Cancellation and shutdown

- `nw_connection_cancel` and `nw_listener_cancel` are asynchronous. Cancellation is not complete until the corresponding terminal state handler and all already-enqueued handlers have drained.
- Track callbacks executing on each serial queue. A nested self-`Stop` prevents further ordinary callbacks and issues `OnDisconnected` before returning, but leaves retained detached state to consume the later cancellation handlers without waiting on itself.
- An external `Stop` waits for that detached state, every cancellation handler, retry timer cancellation, and every other callback to drain before releasing native objects or returning.
- Do not use an empty serial queue as the only cancellation proof. The listener and connection terminal states remain the native lifetime boundary.

## Bounded event wait prerequisite

`EventObject::WaitForTime` is currently Windows-only, while the existing shared async-socket tests deliberately require a bounded platform wait. Do not bind the GCC test entry to unbounded `EventObject::Wait`, and do not implement the timeout with polling, sleeping tasks, or an orphaned waiter thread.

If this prerequisite is not already present:

- Expose `ConditionVariable::SleepWithForTime(CriticalSection&, vint)` and `EventObject::WaitForTime(vint)` for `VCZH_GCC` in `Source/Threading.h`. Do not broaden unrelated `WaitableObject` timed APIs.
- Implement both in `Source/Threading.Linux.cpp`, which is shared by Linux and macOS. Use `pthread_cond_timedwait` with a correctly normalized absolute deadline, return `false` on timeout/failure, and preserve the existing manual-reset/auto-reset event and waiter-counter behavior.
- Keep the event lock held according to the existing `ConditionVariable::SleepWith` contract and avoid any busy wait.

If the other platform task has already added these methods, reuse and verify them instead of creating another timed-wait implementation.

## Project and build integration

Update `Test/UnitTest/UnitTest/UnitTest.vcxproj` explicitly:

- Add `AsyncSocket.macOS.cpp` as `ClCompile`, excluded from all Debug/Release and Win32/x64 Windows builds.
- Add `AsyncSocket.macOS.h` as `ClInclude`.

Update `Test/UnitTest/UnitTest/UnitTest.vcxproj.filters` and put both files under `Common\InterProcess\AsyncSocket`. No solution change is needed.

Update `Test/Linux/vmake` without editing generated `vmake.txt` or `makefile`:

- Keep `AsyncSocket.Windows.cpp` excluded on every Unix-like target.
- On Darwin, compile `AsyncSocket.macOS.cpp`, omit `AsyncSocket.Linux.cpp` if it exists, append `-fblocks` to `CPP_COMPILE_OPTIONS`, and append `-framework Network` to `CPP_LINK_OPTIONS`.
- On non-Darwin Linux, omit `AsyncSocket.macOS.cpp` and preserve any Linux source and `liburing` configuration already present.
- Preserve the CoreFoundation framework supplied by the common Darwin makefile support.
- Make platform selection conditional using `uname` and preserve/extend an existing platform block so the Linux and macOS tasks work in either order.

## Existing shared tests

Do not create, copy, or redesign test scenarios. Fill only the existing `VCZH_GCC && VCZH_APPLE` placeholders in [TestInterProcess_AsyncSocket.cpp](./Test/Source/TestInterProcess_AsyncSocket.cpp):

- Include `AsyncSocket.macOS.h`.
- Bind `macos_socket::AsyncSocketServer` and `macos_socket::AsyncSocketClient` to the shared templated test entry.
- Keep both concrete objects port-only, bind bounded event waiting to the GCC `EventObject::WaitForTime` prerequisite above, and pass `65536` as the maximum receive block size.

Run the existing `TestInterProcess_AsyncSocket.cpp` cases after adding the macOS binding. From `Test/Linux` on macOS, use `.github/Ubuntu/build.sh -f` through the repository-prescribed absolute path, then run `Bin/UnitTest /C /F:TestInterProcess_AsyncSocket.cpp` asynchronously and check its output and exit status. Do not call CMake, make, or Clang directly, and do not run the unit-test executable from a different working directory.
