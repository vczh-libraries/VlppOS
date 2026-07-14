# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

investigate repro

# `INetworkProtocol(Server|Client)` on `IAsyncSocket(Server|Client)`

Implement the async-socket-backed text transport described by [TODO_SocketHttp_AsyncSocket.md](./TODO_SocketHttp_AsyncSocket.md).

## Scope

- Implement the `INetworkProtocol*` adapters in `Source/InterProcess/AsyncSocket/AsyncSocket.h`.
- The authoritative section is `INetworkProtocol(Server|Client) on IAsyncSocket(Server|Client)`, together with the existing `IAsyncSocket*` byte-stream/callback contract and the `INetworkProtocol*` contract in `Source/InterProcess/NetworkProtocol.h`.
- All paths in `TODO_SocketHttp_AsyncSocket.md` are relative to `Source/InterProcess`, as stated by [TODO_SocketHttp.md](./TODO_SocketHttp.md).
- Keep the adapters in `vl::inter_process::async_tcp_socket`.
- Provide reusable server and client templates parameterized by an actual concrete `IAsyncSocketServer` or `IAsyncSocketClient` implementation.
- Instantiate the templates in `Test/Source/TestInterProcess.cpp` with all three existing platform implementations under their correct compile-time guards.

The following work is explicitly out of scope:

- Changing or duplicating the Windows, Linux, or macOS async-socket implementations.
- Changing `Test/Source/TestInterProcess_AsyncSocket.cpp`; it already verifies the native byte-stream implementations.
- Creating new test scenarios instead of reusing the shared text-protocol and channel scenarios in `TestInterProcess.cpp`.
- HTTP request/response or minimized HTTP layers from the other `TODO_SocketHttp_*.md` documents.
- Refactoring the existing NamedPipe or Windows HTTP transports.
- Adding source files or changing project, solution, filter, or `vmake` files.
- Generated files under `Release`, dependencies under `Import`, and generated `Test/Linux/vmake.txt` or `Test/Linux/makefile` files.

## Public adapter shape

Expose the common connection adapter and public templates from `AsyncSocket.h`, using names consistent with:

- `NetworkProtocolConnection`
- `NetworkProtocolServer<TAsyncSocketServer>`
- `NetworkProtocolClient<TAsyncSocketClient>`

Include `NetworkProtocol.h` and any required common synchronization header directly so `AsyncSocket.h` remains independently usable. Do not include any platform implementation header from this common header.

The templates construct and own the supplied concrete implementation, forwarding its constructor arguments. The existing platform bindings must therefore remain constructible with their current `vint port` argument.

`NetworkProtocolServer<TAsyncSocketServer>`:

- Implements `INetworkProtocolServer`.
- Bridges the concrete server's `OnClientConnected(IAsyncSocketConnection*)` to the protocol-level `OnClientConnected(INetworkProtocolConnection*)`.
- Translates each accepted socket connection into one protocol connection and returns the protocol hook's accept/reject result to the socket server.
- Provides the normal default acceptance behavior while remaining inheritable for test and application overrides.
- Delegates `Start`, `Stop`, and `IsStopped` to the concrete socket server.
- Retains translated connections for the complete lifetime required by the socket's non-owning callback, including rejection shutdown and server draining.
- Remains suitable as `TServerBase` for `NetworkProtocolChannelServer<TPackage, TSerialization, TServerBase>`.

Use composition or an internal concrete-server bridge where needed. Do not rely on unrelated `IAsyncSocketServer` and `INetworkProtocolServer` base interfaces to satisfy each other's virtual functions automatically.

`NetworkProtocolClient<TAsyncSocketClient>`:

- Implements `INetworkProtocolClient`.
- Owns the concrete socket client and exactly one translated connection.
- Always returns that translated connection from `GetConnection`.
- Delegates `WaitForServer` and `GetStatus` to the concrete socket client.
- Stops and drains the underlying connection before releasing the translated connection or its callback adapter.

The client adapter cannot directly inherit both interfaces and expose both `GetConnection` functions because they differ only by unrelated return types. Use composition for the concrete client.

## Text framing

Encode every `WString` as one stream frame:

1. One native `vint32_t` containing `WString::Length()`.
2. Exactly `length * sizeof(wchar_t)` bytes containing the characters.

This matches the string representation used by `NamedPipeConnection` without copying its redundant outer byte count. Do not add UTF-8 conversion, a BOM, a NUL terminator, an outer frame size, or any assumption about TCP/read callback boundaries. An empty string is a valid zero-length frame containing only the length field.

The read side must be a buffered state machine over arbitrary positive `IAsyncSocketCallback::OnRead` blocks:

- Preserve partial length fields and partial character data across callbacks.
- Handle a split inside a `wchar_t`.
- Deliver a frame only after all declared characters arrive.
- Parse and deliver every complete frame when one read block contains multiple frames.
- Preserve any incomplete suffix for the next callback.
- Validate negative lengths and arithmetic or allocation overflow before allocating.
- Read the length without assuming the borrowed buffer is suitably aligned.
- Never retain or expose the borrowed socket read buffer after `OnRead` returns.

## Connection behavior

`NetworkProtocolConnection` implements both `INetworkProtocolConnection` and `IAsyncSocketCallback`. It installs itself on the underlying `IAsyncSocketConnection` while exposing a separate non-owning `INetworkProtocolCallback` to users.

- `InstallCallback` accepts one callback at a time, calls `OnInstalled(this)` synchronously, and uses `nullptr` to uninstall.
- Callback replacement, uninstallation, and shutdown must not leave the underlying non-owning callback pointing at a destroyed adapter.
- `BeginReadingLoopUnsafe` delegates to the underlying socket connection.
- `OnRead` parses frames and calls `OnReadString` once for every complete string, without holding an adapter lock while calling user code.
- `OnError(error, fatal)` maps to `OnLocalError(error, fatal)`.
- `OnConnected` and `OnDisconnected` preserve the corresponding protocol callback semantics and ordering.
- The framing has no remote-error opcode, so do not invent one or reinterpret ordinary strings as `OnReadError`.
- `Stop` delegates to the underlying hard-drain boundary and prevents later protocol callbacks from accessing the callback owner, subject only to the socket contract's existing nested-callback allowance.

`SendString` adapts the synchronous protocol API to the socket's one-outstanding-write rule:

- Build one retained `AsyncSocketBuffer` containing the complete frame.
- Queue calls in FIFO order and submit only one `WriteAsync` at a time.
- Support concurrent and reentrant `SendString` calls.
- In `OnWriteCompleted`, consume the completed front buffer and submit the next queued frame.
- Preserve FIFO order by the order in which concurrent or reentrant calls enter the adapter's serialized queue.
- Clear undelivered queued frames during fatal disconnection or shutdown.
- Never call user code or `WriteAsync` while holding an adapter lock.

All callback, parser, write-queue, and lifecycle state must be thread-safe because socket callbacks may run on arbitrary threads. Keep every adapter alive until the underlying socket has drained callbacks that can reference it. Destructors must follow the same idempotent stop, drain, callback-uninstall, and release ordering.

## Shared `TestInterProcess.cpp` coverage

Reuse both existing shared runners; do not copy or redesign their scenarios:

- `RunTextNetworkProtocol` exercises the raw `INetworkProtocol*` text transport through the framing adapter.
- `RunNetworkProtocolChannel` verifies the same adapter beneath `NetworkProtocolChannel*`.

Bind all three concrete implementations explicitly in the existing platform-specific include, namespace, server-glue, and `TEST_CASE` branches:

| Guard | Header | Concrete server/client |
| --- | --- | --- |
| `VCZH_MSVC` | `AsyncSocket/AsyncSocket.Windows.h` | `windows_socket::AsyncSocketServer` / `windows_socket::AsyncSocketClient` |
| `VCZH_GCC && VCZH_APPLE` | `AsyncSocket/AsyncSocket.macOS.h` | `macos_socket::AsyncSocketServer` / `macos_socket::AsyncSocketClient` |
| `VCZH_GCC && !VCZH_APPLE` | `AsyncSocket/AsyncSocket.Linux.h` | `linux_socket::AsyncSocketServer` / `linux_socket::AsyncSocketClient` |

- Keep platform-neutral templated text-server and channel-server glue outside the guards, and instantiate it with the matching server/client pair inside each branch.
- Add one async-socket-backed NetworkProtocol case through `RunTextNetworkProtocol` and one async-socket-backed Channel case through `RunNetworkProtocolChannel` on every platform.
- Keep the existing NamedPipe and HTTP cases in the Windows branch.
- Fill both currently empty GCC branches; do not select one platform implementation through a Windows-only alias or leave another implementation unreferenced.
- Preserve `InterProcessTestRepeatCount` and the existing consecutive sends. Those sends verify FIFO queuing over the one-outstanding-write socket interface.
- Use dedicated, non-overlapping loopback port ranges, distinct from the `38000..38400` ranges in `TestInterProcess_AsyncSocket.cpp`, and use a different port for every repetition so a recently closed TCP connection cannot make repeated server binding ambiguous.

No project-file change is expected: `AsyncSocket.h`, all three platform implementations, and `TestInterProcess.cpp` are already registered in the Windows and Unix-like builds.

## Verification

Verification for this task is Windows-only:

- Build `Test/UnitTest/UnitTest.sln` through `.github/Scripts/copilotBuild.ps1` for Debug/Release and Win32/x64.
- Run the focused `TestInterProcess.cpp` cases through `.github/Scripts/copilotExecute.ps1` in Debug x64, ensuring the file is not skipped by the `.vcxproj.user` filter.
- Run the complete Debug x64 `UnitTest` suite.
- Read the ends of `Build.log` and `Execute.log`; all tests must pass and the Debug log must contain no CRT memory-leak report.

# UPDATES

# TEST [CONFIRMED]

Extend the existing shared `TestInterProcess.cpp` coverage with the async-socket-backed protocol adapter on every compile-time platform branch. Reuse `RunTextNetworkProtocol` to exercise raw text messages, including the existing consecutive sends that require a FIFO write queue, and reuse `RunNetworkProtocolChannel` to exercise the adapter as the raw transport under the channel layer. Use a distinct loopback port for every repetition and separate port ranges from `TestInterProcess_AsyncSocket.cpp`.

Before the adapter exists, the new bindings must fail to compile because `NetworkProtocolConnection`, `NetworkProtocolServer<TAsyncSocketServer>`, and `NetworkProtocolClient<TAsyncSocketClient>` are missing. After implementation, the Windows Debug x64 focused run must pass all `TestInterProcess.cpp` cases without timeout or CRT leak output. The complete Debug x64 suite and all four Windows build configurations must also pass. The macOS and Linux bindings must remain explicitly present under their platform guards, but their execution is outside this task's verification scope.

The shared platform bindings reproduce the missing feature in the default Debug x64 build. MSVC reports `C2039: 'NetworkProtocolServer': is not a member of 'vl::inter_process::async_tcp_socket'` at both the raw text-server and channel-server bindings, followed by the expected dependent template errors. The client binding also cannot be instantiated until the matching `NetworkProtocolClient<TAsyncSocketClient>` adapter is added. This confirms that the platform async sockets cannot currently be used through either existing network-protocol runner.

# PROPOSALS

- No.1 [CONFIRMED] Add reusable framed protocol adapters over concrete async sockets

## No.1 Add reusable framed protocol adapters over concrete async sockets

Add the header-only `NetworkProtocolConnection`, `NetworkProtocolServer<TAsyncSocketServer>`, and `NetworkProtocolClient<TAsyncSocketClient>` adapters to `AsyncSocket.h`. Keep the native client and server implementations behind composition: the client retains a heap-owned concrete client and one translated connection, while the server owns an internal concrete-server bridge and every translated accepted connection. This avoids the incompatible socket/protocol `GetConnection` return types and ensures translated callbacks are detached and drained while their native transports are still alive. If destruction begins in a native callback, retain the concrete transport and finish its native stop on an external worker so the callback runtime cannot be released beneath its current stack.

Implement the connection as both an `INetworkProtocolConnection` and the single installed `IAsyncSocketCallback`. Frame each string with a copied native `vint32_t` character count followed by the exact non-terminated `wchar_t` bytes. Use a parser lock with separate header and aligned payload storage so arbitrary byte fragments, splits inside a character, empty messages, and multiple frames per read are handled without retaining the borrowed read buffer. Validate negative lengths and multiplication/allocation overflow; report malformed input as a fatal local error and stop the connection.

Protect lifecycle, callback, parser, and write state with common cross-platform synchronization. Track active protocol and socket callbacks plus per-thread nested depth with callback-frame chains, allowing nested stop/uninstall without deadlock while making external stop/uninstall wait for every callback that could still reference the non-owning callback owner. Elect one nested uninstaller when concurrent callbacks uninstall themselves, and elect one terminal notifier while allowing a nested `OnInstalled` callback to take delivery from an external notifier that is waiting for installation to finish. Drain peer protocol callbacks before the terminal callback and never invoke user code while holding a state lock. Stop must serialize concurrent callers, prevent new work, wait for out-of-lock socket calls, hard-stop and detach the native callback, clear the protocol callback, and preserve the underlying nested-callback allowance. Terminal notification must self-uninstall and null the borrowed native connection while that object is still guaranteed alive, which is required for rejected macOS connections that are destroyed immediately after the server hook returns.

Adapt synchronous `SendString` to the one-outstanding-write socket contract with a retained FIFO of complete `AsyncSocketBuffer` frames. Select the front and account for every out-of-lock `WriteAsync` submission under the lifecycle lock, submit outside the lock, consume the exact completed front in `OnWriteCompleted`, and submit the next item. An ordinary external `Stop` first closes admission to new sends and offers the already accepted FIFO a bounded drain opportunity of up to one second before clearing any remainder and entering native shutdown; fatal disconnection and nested callback shutdown clear unsent frames immediately. Use exception-safe submission accounting so shutdown cannot race an untracked `WriteAsync` call.

Keep the shared test helpers platform-neutral and instantiate them explicitly with the Windows, macOS, and Linux concrete server/client pairs under their existing guards. Exercise both `RunTextNetworkProtocol` and `RunNetworkProtocolChannel` with dedicated per-repetition ports, preserving the existing consecutive sends as FIFO coverage. Verify only on Windows as required, using the focused Debug x64 inter-process run, the complete Debug x64 suite, and all four Debug/Release Win32/x64 builds.

### CODE CHANGE

- `Source/InterProcess/AsyncSocket/AsyncSocket.h` now includes the protocol and synchronization contracts and defines the common framed connection plus concrete-server and concrete-client templates. The connection implements incremental native `vint32_t`/`wchar_t` framing, a retained FIFO write pump, fatal-error translation, callback installation and terminal-delivery elections, callback draining, idempotent stop, and retained/deferred teardown for nested callbacks. The server bridge retains accepted translated connections and defers nested native finalization; the client heap-owns its concrete transport and repeats native stop on an external worker when destruction begins in a callback.
- `Test/Source/TestInterProcess.cpp` now has shared async-socket text and channel server helpers. Its Windows, macOS, and Linux branches explicitly bind `NetworkProtocolServer` and `NetworkProtocolClient` to their matching concrete async-socket implementations, using raw-protocol ports `38500 + repetition` and channel ports `38600 + repetition`.
- The existing NamedPipe and HTTP cases and all three native async-socket implementations remain unchanged.

### CONFIRMED

The test-first bindings reproduced the missing adapter as compile errors for the absent `NetworkProtocolServer` and `NetworkProtocolClient` types. With the proposal implemented, the final Debug x64 focused `TestInterProcess.cpp` run passed all 7 cases three consecutive times, including both new async-socket cases, and the complete Debug x64 suite passed 124/124 cases. The Debug log contains no CRT memory-leak report.

Final builds of Debug x64, Debug Win32, Release x64, and Release Win32 all succeeded with zero warnings and zero errors. The shared test source explicitly instantiates the Windows, macOS, and Linux implementations under their correct platform guards; as requested, only the Windows tests were executed.
