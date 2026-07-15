investigate repro

# Supply `IAsyncSocketServerCallback` to `IAsyncSocketServer::Start`

Refactor the async-socket server acceptance API so an already-created `Ptr<IAsyncSocketServer>` can receive an external accept callback without subclassing a concrete Windows, Linux, or macOS server.

This is a focused prerequisite for the pointer-based `HttpRequestServer` in [TODO_Task.md](./TODO_Task.md). Implement the callback refactor across all three native async-socket servers and migrate every affected caller and unit test, but execute verification only on Windows. The user will run the Linux and macOS tests separately.

## Public API change

In `Source/InterProcess/AsyncSocket/AsyncSocket.h`, replace the acceptance virtual function on `IAsyncSocketServer` with a dedicated callback supplied to `Start`:

```C++
class IAsyncSocketServerCallback : public virtual Interface
{
public:
	virtual WaitForClientResult                 OnClientConnected(IAsyncSocketConnection* connection) = 0;
};

class IAsyncSocketServer : public virtual Interface
{
public:
	virtual void                                Start(IAsyncSocketServerCallback* callback) = 0;
	virtual void                                Stop() = 0;
	virtual bool                                IsStopped() = 0;
};
```

- Remove `IAsyncSocketServer::OnClientConnected` and the corresponding default overrides from `windows_socket::AsyncSocketServer`, `linux_socket::AsyncSocketServer`, and `macos_socket::AsyncSocketServer`.
- `Start` requires a valid callback. Follow the repository's fail-fast style for `nullptr`; do not silently accept every connection or retain the old virtual function as a fallback.
- The callback pointer is non-owning. The caller keeps it alive from the call to `Start` until `Stop` has reached the callback-drain boundary described below.
- Keep native server constructors port-only and leave `Stop` and `IsStopped` on `IAsyncSocketServer`.
- Update the interface proposal and related explanation in [TODO_SocketHttp_AsyncSocket.md](./TODO_SocketHttp_AsyncSocket.md) so the design document matches the implemented public API.

Do not add a separate installation method. Supplying the callback to `Start` makes callback attachment and listener startup one operation and guarantees that no accept can be delivered before the callback is available.

## Callback and lifetime contract

Preserve the existing server behavior while moving ownership of the accept hook out of the concrete server object.

- Store the non-owning callback before the first native accept is posted or listener delivery is enabled.
- Invoke `IAsyncSocketServerCallback::OnClientConnected` once for each offered connection, without holding native server locks.
- Preserve `WaitForClientResult::Accept` and `WaitForClientResult::Reject` behavior exactly. A rejected connection is stopped and never retained as an accepted server connection.
- Preserve the current exception boundary: an exception escaping the callback rejects the connection and must not terminate a native completion worker.
- Prevent new accept callbacks as soon as stop begins. A normal external `Stop` waits for every already-entered accept callback before returning, clears the stored callback, and permits the caller to destroy the callback immediately afterward.
- `Stop` called from inside that server's own accept callback must not deadlock by waiting for itself. It prevents later callbacks, retains all detached native/callback state until the current callback unwinds, and remains compatible with a later idempotent external `Stop` or destructor that completes deferred cleanup before the callback owner is released.
- `Start` remains one-shot. Attach the callback as part of the same state transition already used to enforce one-time startup, and leave no reachable callback pointer if startup fails before the server can run.

Do not weaken the existing hard-drain promise, add sleeps, or make callback lifetime depend on a guessed delay.

## Native implementations

Apply the same public and lifecycle behavior to:

- `Source/InterProcess/AsyncSocket/AsyncSocket.Windows.h/.cpp`;
- `Source/InterProcess/AsyncSocket/AsyncSocket.Linux.h/.cpp`;
- `Source/InterProcess/AsyncSocket/AsyncSocket.macOS.h/.cpp`.

Replace each native accept path's call through its owning `AsyncSocketServer` with the callback captured by `Start`. Keep callback state synchronized with that implementation's existing start/stop/accept state and use its existing callback-drain mechanism. Do not copy one platform's synchronization primitives into another platform or refactor unrelated connect/read/write/runtime code.

The Linux and macOS source changes are required even though this task does not run those platforms. Review every guarded declaration/definition and every call to ensure all three implementations expose the same interface and preserve their current native cancellation semantics. Do not claim that unavailable platforms were built or tested.

Respect the current source organization if non-template implementations have already moved from `AsyncSocket.h` into `AsyncSocket.cpp`; update the canonical location and do not undo that separation.

## Migrate `NetworkProtocolServer`

The existing `NetworkProtocolServer<TAsyncSocketServer>` in `AsyncSocket.h` derives an internal `SocketServerBridge` from the native concrete server only to override `OnClientConnected`. Migrate that bridge to the new callback contract with the smallest safe change: it may continue deriving from the concrete server while also implementing `IAsyncSocketServerCallback`, or it may use composition if all existing lifecycle behavior is demonstrably preserved. Pass a retained callback adapter to the native server's `Start`.

- Preserve the public `INetworkProtocolServer::Start()` signature and all existing `NetworkProtocolServer` constructor forwarding.
- If the existing bridge is retained, have its socket-side `OnClientConnected(IAsyncSocketConnection*)` override the callback interface and call `asyncSocketServer->Start(asyncSocketServer.Obj())`; do not depend on a removed native-server virtual function.
- Preserve translated connection retention, accept/reject behavior, callback-depth accounting, reentrant stop handling, deferred native cleanup, and hard-drain destruction.
- Keep the callback adapter alive until the native server can no longer invoke it. Do not pass a temporary or an unretained raw owner pointer whose lifetime ends during nested stop.
- Do not alter string framing, `NetworkProtocolConnection`, client behavior, channel behavior, port ranges, or the one-outstanding-write contract.

## Update shared async-socket tests

Migrate `Test/Source/TestInterProcess_AsyncSocket.cpp` from a subclass override to the new callback API while keeping its scenarios registered once for Windows, Linux, and macOS.

- Replace `TestServer<TServerBase>::OnClientConnected` with a small `IAsyncSocketServerCallback` implementation around the existing `AcceptHandler`.
- Construct native servers directly and pass the scenario callback to `Start` in every server-start path, including the intentionally late server startup in the retry-then-connect scenario.
- Retain each test callback until `Stop` returns and all callback-thread assertions have been transferred back to the test thread.
- Update diagnostic text that still names `IAsyncSocketServer::OnClientConnected`.
- Preserve all existing full-duplex, rejection, stop-from-read, retry, cancellation, repeat-count, bounded-wait, and no-sleep coverage.

Add focused deterministic coverage for the new non-owning callback boundary on Windows:

1. Block inside `IAsyncSocketServerCallback::OnClientConnected`, call `server->Stop()` from another task, and prove that external stop does not return until the callback is released. Destroy the callback immediately after `Stop` and assert that no callback begins or continues afterward.
2. Call `server->Stop()` from an accept callback and require no deadlock and no later accept callback. Complete any deferred finalization with an idempotent external stop before releasing the callback owner.
3. Preserve the existing rejected-connection case so both `Accept` and `Reject` results are covered through the new interface.

Use events and existing test wait helpers, not timing sleeps or brittle elapsed-time assertions.

## Affected regression coverage

`Test/Source/TestInterProcess.cpp` calls `INetworkProtocolServer::Start`, so it should not need a direct API rewrite. Its async-socket-backed cases are nevertheless mandatory regression coverage for the refactored internal `NetworkProtocolServer` callback adapter:

- `AsyncSocket (NetworkProtocol)`;
- `AsyncSocket (Channel)`.

Keep `InterProcessTestRepeatCount` and the shared Windows/macOS/Linux bindings unchanged. Do not rewrite NamedPipe, Windows HTTP, channel, or protocol scenarios merely to accommodate the new native callback API.

No new production or test source file is expected, so no solution, MSBuild project/filter, or `Test/Linux/vmake` edit should be necessary. Never hand-edit generated `Release`, `Test/Linux/vmake.txt`, or `Test/Linux/makefile` files.

## Explicitly out of scope

- HTTP request/response parsing, `HttpRequestServer`, `HttpRequestClient`, or any MiniHttp behavior.
- Changes to `IAsyncSocketClient`, `IAsyncSocketConnection`, or their callback contracts.
- Native connect, read, write, retry, socket-option, and runtime-performance refactoring.
- Running or claiming Linux or macOS builds/tests in this task.

## Windows-only verification

Follow `.github/copilot-instructions.md`, `Project.md`, and the referenced coding, source-file, build, execution, and native-dialog guidance. Use only the repository-provided Windows scripts.

- Build `Test/UnitTest/UnitTest.sln` through `.github/Scripts/copilotBuild.ps1` for Debug/Release and Win32/x64.
- Run the complete `TestInterProcess_AsyncSocket.cpp` file in Debug x64, including full-duplex, rejection, stop-from-callback, retry-then-connect, stop-during-retry, and the new callback lifetime cases.
- Run the complete `TestInterProcess.cpp` file in Debug x64 so both async-socket NetworkProtocol and Channel cases execute with all repetitions.
- Run the complete Debug x64 UnitTest suite.
- Inspect the ends of `.github/Scripts/Build.log` and `.github/Scripts/Execute.log`; require zero build errors, all selected tests passing, and no Debug CRT memory-leak report after the test summary.
- Record the API migration, per-platform implementation review, callback lifetime evidence, regression results, and the explicit limitation to Windows execution in `.github/TaskLogs/Copilot_Investigate.md`.

Acceptance requires the common interface and all three native implementations to use `Start(IAsyncSocketServerCallback*)`, with no remaining native acceptance override on `IAsyncSocketServer`. All temporary diagnostics must be removed. Commit only the intentional API, three-platform implementation, documentation, tests, adapter migration, and investigation record, then push the current branch.
