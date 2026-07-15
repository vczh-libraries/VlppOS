# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

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

# UPDATES

# TEST [CONFIRMED]

Confirm the requested API and ownership boundary with a structural audit, migrate the existing shared behavioral suite to the callback supplied to `Start`, and add deterministic Windows coverage for callback draining and reentrant server stop.

The structural reproduction is:

- `IAsyncSocketServer` currently owns the virtual `OnClientConnected(IAsyncSocketConnection*)` hook and exposes parameterless `Start()`.
- All three native `AsyncSocketServer` classes still provide a default accepting override, and their native accept paths dispatch through their owning concrete server.
- `NetworkProtocolServer<TAsyncSocketServer>` and the shared native async-socket tests depend on subclassing a concrete server to override the removed hook.

The problem is confirmed. The public header and every guarded native header/definition expose the old subclass-only acceptance shape, and repository-wide call-site searches find no callback supplied to native `Start`. The clean pre-change Debug x64 build succeeded with zero warnings and zero errors. The complete baseline run selected `TestInterProcess_AsyncSocket.cpp` and `TestInterProcess.cpp`, passed 13/13 test files and 125/125 test cases, and appended no CRT memory-leak report. This establishes that the existing behavioral suite passes before the API migration; the new tests below are required to define and verify the new non-owning callback boundary.

The migrated shared scenarios must continue to cover full-duplex long transfers, rejected connections, connection stop from `IAsyncSocketCallback::OnRead`, retry followed by late server startup, and stop during retry on the one platform-neutral registration path. Each scenario will own a small `IAsyncSocketServerCallback` adapter for its existing `AcceptHandler`, pass it to every server `Start`, and retain it until `Stop` returns.

Add the following Windows-only server-callback cases with `EventObject` coordination and the existing bounded-wait helper:

1. Block inside `IAsyncSocketServerCallback::OnClientConnected`, start external `IAsyncSocketServer::Stop` on another task, and establish that shutdown has progressed while the stop-return event remains unsignaled and the callback remains entered. Release the callback, require both callback exit and stop return, destroy the callback owner immediately, and assert one callback, zero active callbacks, and no callbacks after stop or destruction.
2. Call `IAsyncSocketServer::Stop` from `IAsyncSocketServerCallback::OnClientConnected`, require the nested call to return, keep that callback entered while a later client fails to connect, release the callback, then call external `Stop` idempotently to complete deferred finalization before destroying the callback owner. Require exactly one accept callback and no callback after nested stop.
3. Keep the existing rejected-connection scenario so `WaitForClientResult::Accept` and `WaitForClientResult::Reject` both flow through the new callback interface. Also require `Start(nullptr)` to fail fast without starting the listener.

Acceptance requires all four Windows builds of `Test/UnitTest/UnitTest.sln` (Debug/Release and Win32/x64) to report zero errors. The complete Debug x64 run must select and pass `TestInterProcess_AsyncSocket.cpp` and `TestInterProcess.cpp`, including every repeated async-socket-backed NetworkProtocol and Channel case, and must pass the complete suite. The final `Build.log` and `Execute.log` tails must contain zero errors, complete passing summaries, and no Debug CRT memory-leak report. Linux and macOS declarations, guarded definitions, call sites, and synchronization paths will be reviewed only; they will not be built or executed in this task.

The implemented tests satisfy these criteria. The Windows-only callback cases prove that `Start(nullptr)` fails before listener startup, external `Stop` remains blocked while an accept callback is entered and permits immediate callback destruction after draining, and callback-reentrant `Stop` returns without allowing the already-connected second client to enter another accept callback. The migrated shared rejection case continues to exercise `Reject`, while the full-duplex and other shared cases exercise `Accept` through the new callback interface.

The repository build wrapper completed Debug and Release for both Win32 and x64 with zero warnings and zero errors. The final Debug x64 execution selected the complete `TestInterProcess_AsyncSocket.cpp` and `TestInterProcess.cpp` files and passed the complete suite: 13/13 test files and 128/128 test cases. The retained `Build.log` ends with zero warnings and zero errors, `Execute.log` ends with the complete passing summary, and `Execute.log.memoryleaks` is empty. Linux and macOS were not built or executed; their guarded declarations, definitions, dispatch paths, startup failures, and native stop/drain paths were reviewed textually.

# PROPOSALS

- No.1 Attach the accept callback at the one-shot server start boundary [CONFIRMED]

## No.1 Attach the accept callback at the one-shot server start boundary

Add `IAsyncSocketServerCallback` beside the shared async-socket interfaces and replace `IAsyncSocketServer::OnClientConnected` plus parameterless `Start` with `Start(IAsyncSocketServerCallback*)`. The argument is a required, non-owning pointer. Each native server stores it under the same state lock and in the same successful one-shot transition that makes the listener runnable, before posting or enabling the first accept. Native accept delivery copies the installed pointer while establishing that the server is still running, releases the native lock, invokes the callback behind the existing catch-all exception boundary, and rejects/stops the connection when the callback rejects or throws.

Keep each backend's existing synchronization model:

- Windows replaces the concrete-server owner pointer with the callback pointer in `AsyncSocketServer::Impl`. A normal external stop closes and drains the pending accept, stops retained connections, drains both IOCP workers through `IocpRuntime::Stop`, and then clears the callback. A stop on the server's own callback/completion worker requests runtime exit without joining itself and retains the callback; the existing later external idempotent `Stop` completes the deferred runtime join and clears it. Failures before the start-state commit never store the callback, while failure to post the first `AcceptEx` uses the normal stop/drain path.
- Linux replaces `ServerState`'s concrete owner with the callback pointer. `Start` installs it with `startCalled`/`starting`; synchronous setup and first-submission failures clear it, while stop racing startup clears it after the existing start-finished boundary. Accept completion increments `activeCallbacks` while copying the pointer under `lockState`, then invokes outside the lock. `Stop` prevents later entries, waits down to the current thread's callback depth, and clears the pointer only when called outside that callback. A nested stop therefore returns without waiting for itself, and a later external stop observes zero callback depth, drains, and clears.
- macOS replaces `ServerState`'s concrete owner with the callback pointer and installs it with `startCalled` before configuring `nw_listener_t`. Creation failure clears it immediately. New-connection delivery copies it under `lockState` and invokes it on the existing serial listener queue without the lock. A normal external stop waits for the listener cancellation handler and its queued finish boundary, which necessarily follows every already-entered listener callback, then clears the callback. A stop on that queue retains it and defers finalization; a later external stop or destructor finishes cancellation, clears it, and releases native state.

Retain `NetworkProtocolServer<TAsyncSocketServer>::SocketServerBridge`, additionally implement `IAsyncSocketServerCallback` on the bridge, and pass the bridge itself to native `Start`. Its self-reference and existing callback-domain/deferred-stop logic continue to keep the adapter, translated connections, and native server alive through nested stop and hard-drain destruction.

Migrate the shared native tests from a concrete-server subclass to a small callback adapter around `AcceptHandler`, construct native servers directly, and pass a retained adapter to every `Start`, including late retry startup. Add the two Windows event-driven callback-lifetime scenarios and the null-callback fail-fast check defined in `# TEST`. Update the design proposal and lifecycle explanation in `TODO_SocketHttp_AsyncSocket.md`, and audit all three guarded headers, definitions, native dispatch sites, and call sites without changing client/read/write/runtime behavior.

### CODE CHANGE

The shared API now defines `IAsyncSocketServerCallback` and exposes only `IAsyncSocketServer::Start(IAsyncSocketServerCallback*)`, `Stop`, and `IsStopped`. `NetworkProtocolServer<TAsyncSocketServer>::SocketServerBridge` implements the callback interface and passes its retained bridge object to native `Start`, preserving its existing lifecycle and translated-connection behavior.

The Windows, Linux, and macOS native server headers and implementations now store the required callback as synchronized, non-owning server state instead of dispatching through the concrete server object. Each backend installs it before enabling the first accept, captures it while accepting is still permitted, invokes it outside the server lock behind the existing exception-to-rejection boundary, and removes/stops rejected or stop-raced connections. Startup failures clear the pointer. Normal external stop uses that backend's existing completion/callback drain before clearing it, while callback-reentrant stop retains it for a later external idempotent stop or destructor to finalize.

The shared async-socket tests now create native servers directly and retain a small `TestServerCallback` adapter through `Stop`, including late retry startup. Windows adds deterministic event-driven checks for null-callback fail-fast behavior, external stop while an accept callback is blocked, immediate callback destruction after the drain, and reentrant stop with no later accept callback. `TODO_SocketHttp_AsyncSocket.md` now documents the implemented API and lifetime contract. No source file, project metadata, generated output, client API, connection callback contract, framing behavior, or unrelated transport path was added or changed.

### CONFIRMED

The proposal solves the subclass-only API gap: repository-wide source and test audits find the dedicated callback interface on the common API and matching `Start(IAsyncSocketServerCallback*)` declarations and definitions for all three native servers, with no remaining native `IAsyncSocketServer::OnClientConnected` override or parameterless native async-socket server start call. The existing public `INetworkProtocolServer::Start()` remains unchanged and its async-socket NetworkProtocol and Channel regressions pass through the retained bridge adapter.

The Windows callback lifetime behavior is directly exercised. In the blocked-callback case, an event-gated external stop task cannot signal completion while the callback remains entered; after release, both the callback and stop drain, the callback object is destroyed immediately, and an idempotent stop produces no later callback. In the reentrant case, two clients complete native connection before the callback invokes server stop, but only the first callback is delivered; nested stop returns without deadlock, the callback unwinds, and a later external stop completes deferred finalization before callback destruction. The null-start and shared rejection cases confirm fail-fast and rejection behavior, and the existing full-duplex, stop-from-read, retry, and cancellation cases remain green.

All requested Windows builds passed through `copilotBuild.ps1`: Debug Win32, Release Win32, Debug x64, and Release x64 each reported zero warnings and zero errors. The final complete Debug x64 run through `copilotExecute.ps1` passed 13/13 test files and 128/128 test cases, including all eight cases in `TestInterProcess_AsyncSocket.cpp` and the async-socket-backed NetworkProtocol and Channel cases in `TestInterProcess.cpp`. The final logs contain the successful summaries and no Debug CRT memory-leak report. Linux and macOS changes were reviewed for API parity, guarded declaration/definition consistency, callback capture, exception rejection, startup cleanup, cancellation, and deferred drain behavior, but were intentionally not built or executed on Windows.
