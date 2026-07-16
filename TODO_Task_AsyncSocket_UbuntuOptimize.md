investigate repro

# Optimize Ubuntu async-socket performance

Measure, explain, and fix the Ubuntu-observed Linux-backend cost of the socket- and HTTP-backed unit tests without reducing the work or coverage performed by those tests.

## Perspective

The reported fast macOS result is not evidence that the socket and HTTP tests are being skipped. The portable cases instantiate the macOS or Linux native socket types from the same test bodies, and the native scenarios still perform loopback connects, full-duplex transfers, protocol parsing, callback delivery, and hard-drain shutdown.

The important architectural difference is the fixed cost paid by each native object. The macOS implementation creates serial dispatch queues and delegates work to Network.framework and the system's shared GCD workers. The current Linux implementation gives every `linux_socket::AsyncSocketServer` and every `linux_socket::AsyncSocketClient` its own `RingRuntime`. Constructing and destroying one runtime includes:

- creating a 1024-entry ring with `io_uring_queue_init_params`;
- probing seven required operations with `io_uring_get_probe_ring`;
- creating a dedicated completion thread when the runtime starts;
- submitting and draining cancellation and wake operations during shutdown;
- waiting for detached completion-worker exit and calling `io_uring_queue_exit`.

Accepted connections already share their server's runtime, but separate servers and outgoing clients do not. This is a reasonable isolation-oriented design for long-lived objects. It can be expensive for unit tests and applications that repeatedly create short-lived clients and listeners, where ring setup and thread lifecycle may cost more than the loopback exchange itself. For example, the two cases in `TestInterProcess.cpp` each repeat 20 times and create one server plus two clients per repetition. Together they can pay for 120 complete Linux runtime lifecycles while testing only two protocol scenarios.

Treat this as the leading explanation, not as a predetermined conclusion. A slower Ubuntu total can also contain 100-millisecond connection retries, test watchdog contention, or actual data-path/shutdown latency. Establish their individual costs on one Ubuntu machine before selecting a fix. Do not use a cross-machine Ubuntu-versus-macOS wall-clock comparison as the performance baseline.

## Problem

The reported Ubuntu symptom is concentrated in the native portions of these portable files, which pass but take noticeably longer than their macOS bindings:

- `Test/Source/TestInterProcess.cpp`
- `Test/Source/TestInterProcess_AsyncSocket.cpp`
- `Test/Source/TestInterProcess_AsyncSocket_MiniHttpApi.cpp`
- `Test/Source/TestInterProcess_HttpRequest.cpp`

The first two contain repeated native connection scenarios. MiniHttp also constructs native listeners and clients in validation and occupied-port cases that may fail before doing useful I/O. A Linux client currently creates and starts its ring runtime in its constructor even when `WaitForServer` is never called. A Linux server creates its ring runtime in its constructor before `Start` has successfully bound the listener. These eager costs are especially suspicious in short or intentionally failing cases.

The task is to identify which phases dominate, correct the production ownership/lifecycle issue when one exists, and leave the tests deterministic and fully meaningful.

## Leading hypothesis: eager per-object `io_uring` lifecycle

Relevant code is in `Source/InterProcess/AsyncSocket/AsyncSocket.Linux.cpp`:

- `RingRuntime::RingRuntime` initializes the 1024-entry ring and probes required opcodes.
- `RingRuntime::Start` creates the completion worker.
- `RingRuntime::Stop` releases an unstarted ring directly; for a started runtime it requests drain and, when called off-worker, wakes the detached worker and waits for its exit signal. The worker releases that ring after the final completion.
- `AsyncSocketServer::Impl` constructs one runtime before the listener is started.
- `AsyncSocketClient::Impl` constructs and starts one runtime before the client begins connecting.
- `ServerState::Stop` and `AsyncSocketClient::Impl::Stop` stop the owning runtime after owner-specific operations and callbacks drain.

If construction and shutdown dominate, the preferred correction is to stop treating ring and worker setup as disposable object scaffolding while preserving the existing socket-owner contract. Start with lazy acquisition and capability caching. If that is insufficient, reuse warm runtimes through an explicitly finalized, bounded process-level facility.

Do not immediately put every server and client on one global ring. The Linux completion worker currently runs `RingOperation::Handle` and user callbacks inline. A single shared worker would newly serialize unrelated servers and clients, and a callback that synchronously waits for work owned by the same worker could starve or deadlock. Any concurrently shared or sharded-ring design therefore needs stronger evidence and an explicit callback-dispatch and owner-drain analysis.

## Competing explanations that must be ruled out

### Listener startup and retry delay

The protocol scenarios queue the server and two clients on separate thread-pool tasks. Unlike the Windows binding, the GCC bindings currently call `RunAsyncSocketNetworkProtocolTestCases` without the optional listener-start barrier. A client that reaches `connect` before `listen` succeeds reports a nonfatal failure and waits `AsyncSocketClientRetryDelay == 100` milliseconds before trying again.

Count `clientAttempts` and timestamp listener readiness, the first connect submission, every connect completion, and the retry timeout. Determine whether the Ubuntu duration arrives in 100-millisecond steps. The dedicated `AsyncSocket retry then connect` and `AsyncSocket Stop during retry` cases already cover retry behavior, so an unrelated protocol scenario does not need an accidental startup race to preserve that coverage.

### Completion and payload processing

Separate SQ submission, CQ wait/dispatch, receive rearming, short sends, callback execution, and protocol parsing from setup and shutdown. The long full-duplex case transfers and validates substantial byte arrays; it must remain capable of exposing a real Ubuntu data-path regression.

Count maximum simultaneous SQ entries and in-flight operations before changing the ring depth. A client usually needs far fewer than 1024 entries, but a server ring serves its listener and every accepted connection. Reducing the depth without a capacity/backpressure design can turn a performance experiment into a correctness failure.

### Cancellation and hard-drain shutdown

Measure connection cancellation, `OperationDrain::Wait`, callback drain, runtime wake submission, worker exit wait, and `io_uring_queue_exit` independently. A slow but necessary owner drain must not be hidden by returning a runtime to a cache early. Count cancellation races and final CQEs rather than assuming that closing a file descriptor completed native work.

### Test watchdog contention

`TimeoutThread` in `TestInterProcess.cpp` busy-spins until three tasks finish or five seconds elapse. It is common test code, but scheduler and Debug-build differences can make its CPU cost more visible on Ubuntu. Measure it separately. Replace it with bounded event coordination only if it is proven material, while preserving the five-second watchdog and failure behavior.

### Environment and kernel behavior

Record the Ubuntu version, kernel, CPU, virtualization/container environment, liburing version, Debug build, and whether the machine is otherwise loaded. Record wall, user, and system time. Use syscall and profiler evidence where available to distinguish `io_uring_setup`/registration/enter cost, thread scheduling, allocator cost, and actual socket I/O. Do not attribute a cross-host difference to the operating system alone.

## Required investigation

1. From `Test/Linux`, perform a clean Debug x64 build through `.github/Ubuntu/build.sh -f`.
2. Run each of the four focused files separately with `/C`, asynchronously, and record its exit code and complete summary. Then run the complete unfiltered suite.
3. Repeat the focused measurements enough times to report cold and warm median/range values on the same Ubuntu host. Keep the exact executable, build, test filters, repeat counts, payloads, and machine configuration for before/after comparison.
4. Add temporary counters and monotonic timestamps around:
   - server/client construction;
   - `io_uring_queue_init_params` and opcode probing;
   - completion-worker creation and exit;
   - listener bind/listen and first connect;
   - failed attempts and retry timeouts;
   - payload exchange and callback execution;
   - owner cancellation/callback drain;
   - runtime wake, worker-exit wait, and `io_uring_queue_exit`.
5. Record the number of servers, clients, rings, probes, worker starts/stops, submitted operations, CQEs, retries, and maximum in-flight/SQ work for each focused file. Reconcile these counts with the test source.
6. Use temporary diagnostic variants to isolate causes without treating them as the fix. In particular, compare the protocol cases with listener-start synchronization enabled, and compare cold first-use cost with later repetitions. Remove these diagnostics before the final commit unless a deterministic test change has lasting value.
7. During an unexpectedly long interval, use `lldb` or available Ubuntu profiling/syscall tools to identify the actual blocked or CPU-consuming path. Do not infer the root cause only from total elapsed time.
8. Select and implement only the stages below justified by the measurements, then repeat the identical baseline and stress runs.

## Suggested optimization stages

### Stage 1: avoid runtimes that cannot do work

- Defer a server's ring acquisition and worker start until `Start` has successfully created, bound, and listened on the socket. Preserve the rule that callbacks cannot begin before `Start`.
- Defer a client's ring acquisition and worker start until `WaitForServer` actually begins the first connection attempt. A constructed-and-stopped or wrapper-validation-only client should not create an `io_uring` instance.
- Define and test the intentional failure-boundary change caused by lazy acquisition. A server ring/probe/worker failure from `Start` must close its newly created listener, leave the server stopped with no retained callback, and throw `AsyncSocketServerStartException(AsyncSocketServerStartFailure::Other, ...)`. A client acquisition failure from `WaitForServer` must release partial native state, change the client to `Disconnected`, unblock its wait state, and synchronously rethrow the original actionable setup error. Do not also report the same setup failure through `OnError`; deliver at most the one terminal `OnDisconnected` notification to an installed callback, and keep every later `Stop` idempotent and nonblocking.
- Cache the immutable required-opcode result once per process only after proving that this is valid for every ring created with the same parameters. Make concurrent first use race-free, distinguish stable supported/unsupported capability from transient ring/probe allocation failure, preserve early actionable failure when the kernel lacks an operation, and do not cache a transient failure as kernel capability.
- Keep lazy runtime ownership behind the existing construction surfaces. Attach it through non-interface `Impl`, `ServerState`, or `ConnectionState` internals and the existing `Start` or `WaitForServer` calls; do not add a constructor argument, constructor overload, or replacement factory to expose a runtime, cache, or lease.
- Retain the 1024-entry depth initially so the effects of laziness and probe caching are measurable independently.

This stage should remove obvious wasted work from invalid-authority, failed-bind, and never-started-object paths without changing concurrency.

### Stage 2: reuse warm runtimes if lifecycle churn remains dominant

Prefer a bounded cache of started runtimes leased exclusively to one active server or client at a time as the first reuse design. Exclusive leases preserve the present completion-thread isolation: accepted connections share only their server runtime, while unrelated clients do not suddenly share callback execution.

- Acquire a lease lazily and release it only after all owner operations, cancellation completions, active callbacks, and file descriptors have drained.
- An individual socket's `Stop` must stop that socket owner, not the reusable runtime. Returning a lease early is a use-after-stop defect.
- A reentrant `Stop` running on the completion worker cannot return its lease from inside the current callback. Mark that lease release-pending and publish it to the idle cache only from a post-callback/post-CQE cleanup boundary where the current operation has been removed and its owner drain has ended. A later external `Stop` must still be able to complete the hard drain.
- A reusable runtime must be demonstrably idle: its operation dictionary is empty, no SQ/CQ work belongs to the old owner, and no callback can reference that owner. Continue using collision-free operation IDs across leases.
- Bound the retained idle count using measured concurrency. Create overflow runtimes rather than making the lazy acquisition in `Start` or `WaitForServer` block for a lease, and stop excess runtimes after their owners drain.
- Initialize the cache lazily and finalize it explicitly through the repository's global-storage lifecycle. Require every active owner and lease to drain before `FinalizeGlobalStorage()`; the finalizer then stops all cached idle runtimes. Using the facility after that cleanup boundary is outside the application lifecycle, because a global-storage accessor can initialize storage again. Do not rely on a later C++ static destructor or leak workers intentionally.
- Keep failures during acquisition, worker startup, or finalization exception-safe. A partially created runtime must not enter the cache.

If exclusive reuse cannot meet the measured target, evaluate a small role-aware or sharded shared-runtime pool. Do not implement it merely because sharing sounds cheaper. Prove that callback execution cannot deadlock and that unrelated owners are not unacceptably serialized. Before concurrent sharing, replace the `currentRingRuntime` shortcut with owner-aware reentrant drain/deferred-release logic: that pointer identifies only the worker, so today a callback that stops a different owner on the same runtime can skip the other owner's `OperationDrain::Wait` and return before its work drains. Add callback-dispatch isolation as required by the measured design.

### Stage 3: tune the hot path only when profiling requires it

After lifecycle cost has been removed from the comparison, consider ring depth, submission batching, operation lookup, or receive-buffer reuse only when profiles show a material remaining cost. Keep server capacity, arbitrary-thread submission, one-read/one-write rules, and borrowed-read-buffer lifetime correct. Do not introduce `SQPOLL`, multishot operations, registered buffers, lock-free queues, or a custom scheduler without evidence that the simpler design is insufficient.

## Test determinism

Enable the existing listener-start barrier for the portable protocol and channel scenarios so framing, callback ordering, FIFO sends, routing, and shutdown never depend on an incidental connect race. This is a required test determinism correction, not the production performance result. Keep the explicit retry-then-connect and stop-during-retry cases unchanged so retry scheduling and cancellation still execute on every platform. Verify every affected GCC binding that is available when changing the common/platform test registration.

Do not remove payload transfers, lower `InterProcessTestRepeatCount` or `AsyncSocketTestRepeatCount`, shorten production retry/drain policies, add sleeps, enlarge watchdogs, skip native scenarios, weaken byte assertions, or replace real loopback scenarios with fakes. Do not add a permanent wall-clock pass/fail assertion; timing is diagnostic evidence, while permanent tests should assert deterministic lifecycle state and behavior.

## Fix requirements

- Do not add, remove, or change any declaration in the existing `IAsyncSocket*` interfaces: `IAsyncSocketCallback`, `IAsyncSocketConnection`, `IAsyncSocketClient`, `IAsyncSocketServerCallback`, and `IAsyncSocketServer`.
- Do not add, remove, or change the signature, overload set, default arguments, explicitness, or accessibility of constructors on any existing production or test class implementing any of those interfaces. In particular, preserve the public Windows, Linux, and macOS `AsyncSocketServer(vint)` and `AsyncSocketClient(vint)` constructors and each platform-private `AsyncSocketConnection(Ptr<ConnectionState>)` constructor. Stage 1 may change Linux constructor implementations, including initializer lists, and move native setup failure timing to the existing `Start` or `WaitForServer` boundary, but runtime, cache, lease, and factory dependencies must remain internal and must not change existing construction call syntax.
- Preserve ordered full-duplex bytes, one outstanding read, one user write, short-send handling, exact retained write buffers, and callback ordering.
- Preserve synchronous `OnInstalled`, one terminal disconnect notification, fatal error-before-disconnect ordering, and suppression of intentional-cancellation errors.
- Preserve idempotent `Stop` as a hard drain boundary, including nested stop from a connection or server callback and the guarantee that no callback touches an owner after external `Stop` returns.
- Never execute user code while holding runtime, cache, connection, or server locks.
- Preserve prompt failure for unavailable required `io_uring` operations and native setup failures.
- Keep the implementation Linux-local unless evidence requires a common change. Explain and verify every affected platform if common code or portable tests change.
- Update `TODO_SocketHttp_AsyncSocket.md` if the final active-runtime ownership model changes its authoritative Linux design, and record the measured choice in `.github/TaskLogs/Copilot_Investigate.md`.
- Do not hand-edit generated `Test/Linux/vmake.txt` or `Test/Linux/makefile`; update their source metadata and regenerate them through the repository build script when source lists require it.

## Explicitly out of scope

- Replacing `io_uring` with another Linux socket backend.
- Optimizing or redesigning Windows IOCP, macOS Network.framework, NamedPipe, or Windows HTTP transports.
- Changing HTTP parsing, framing, timeout policy, MiniHttp routing, or public application APIs.
- Making Ubuntu match an absolute macOS time measured on different hardware.
- Broad thread-pool, allocator, lock-free, or global-storage refactoring unrelated to the measured socket cost.

## Acceptance criteria

- The original Ubuntu cost is attributed to measured phases. The report states how much time and how many retries, rings, probes, worker lifecycles, operations, and CQEs each relevant focused run used.
- Before/after results use the same Ubuntu host, clean Debug x64 build, command, filters, payloads, and repeat counts, and show a clear repeatable improvement in the measured dominant phase and focused total time.
- A declaration-level comparison with the baseline confirms that all five existing `IAsyncSocket*` interfaces are unchanged and that no constructor signature, overload, default argument, explicitness, accessibility, or existing call syntax changed for any pre-existing class implementing one. Linux constructor implementation changes, including initializer-list changes, are acceptable only for the lazy internal work described in Stage 1.
- If warm runtime reuse is implemented, ring/probe/worker creation no longer scales with every sequential server/client object up to the cache's documented bound. Cold first use, warm reuse, overflow, failed acquisition, failed bind, never-started client, reentrant deferred release, and explicit finalization with no active lease are covered.
- The optimization does not regress steady-state full-duplex throughput or concurrent progress for multiple servers and clients. Any reported variance is measured over repeated runs rather than hidden by a wall-clock assertion.
- The five shared native async-socket cases pass, including long full-duplex byte validation, rejection, callback stop, retry-then-connect, and stop-during-retry.
- Deterministic Linux coverage proves external server stop drains a blocked accept callback, reentrant server stop is safe, and no callback occurs after stop. If Stage 2 reuse is implemented, it also proves that no old-owner completion reaches a new owner.
- The native protocol, channel, HTTP request/response, and MiniHttp scenarios pass without lower repeat counts, reduced payloads, skipped assertions, production-duration sleeps, hangs, or unexpected retries.
- Stress at least 100 server/client lifecycle iterations, including concurrent objects, without deadlock, use-after-free, lost completion, file-descriptor leak, or surviving worker. If Stage 2 reuse is implemented, include repeated acquire/start/stop/deferred-release/reacquire and cache-overflow cycles.
- The complete Ubuntu Debug x64 UnitTest suite passes from a clean build.
- All temporary diagnostics are removed unless intentionally retained as bounded test instrumentation, and generated build artifacts contain only expected changes.

## Ubuntu verification

Follow `.github/copilot-instructions.md`, `Project.md`, and the referenced build, execution, debugging, and testing guidance.

- Work from `Test/Linux`.
- Build only through the repository's `.github/Ubuntu/build.sh`; use `-f` for the final clean Debug x64 rebuild.
- Run `Bin/UnitTest` asynchronously with `/C` and the appropriate case-sensitive `/F:<filename>` filters for the four focused files.
- Run repeated focused stress invocations with bounded monitoring, then run `Bin/UnitTest /C` without filters for the complete suite.
- Read the raw terminal output and process exit code. A `/C` run that stops before the final passed-file and passed-case summaries has failed or crashed.
- Before committing, inventory every production and test class that directly or indirectly implements an `IAsyncSocket*` interface across headers and implementation files, then compare it with the baseline. The expected declaration-level diff is zero for all five interfaces and for every existing implementing-class constructor; constructor implementation changes, including initializer-list changes, remain permitted for the stated Linux lazy acquisition.
- Record the baseline, phase attribution, design decision, before/after measurements, stress results, and final summaries in `.github/TaskLogs/Copilot_Investigate.md`.

Commit only intentional implementation, deterministic regression tests, required project/build metadata, design clarification, and the investigation record, then push the current branch.

## REVIEW COMMENTS

### Freeze all `IAsyncSocket*` interfaces and implementing-class constructors

**review comment**: The original fix requirement named only `IAsyncSocketServer`, `IAsyncSocketClient`, and `IAsyncSocketConnection`, omitting `IAsyncSocketCallback` and `IAsyncSocketServerCallback`. Its reference to port-only native constructors also did not protect platform-private connection constructors or constructors of other production and test implementers. The stage guidance, fix requirements, acceptance criteria, and verification now prohibit declaration-level changes to all five interfaces and API-shape changes to every existing implementing-class constructor. The documented Linux lazy-acquisition work may change constructor implementations and setup-failure timing only; it must remain behind the existing construction call syntax.
