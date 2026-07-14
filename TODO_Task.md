investigate repro

# Windows async-socket protocol latency

Probe, verify, and fix the Windows-only performance problem in the async-socket-backed network-protocol tests.

## Problem

The GCC thread-completion fix in commit `6ac98df` makes the existing async-socket protocol coverage finish on Linux and macOS. Windows also passes the derived-thread regression, but the following cases in `Test/Source/TestInterProcess.cpp` have become observably slow, taking about ten seconds while the NamedPipe and HTTP cases remain fast:

- `AsyncSocket (NetworkProtocol)`
- `AsyncSocket (Channel)`

Establish a measured Windows baseline before changing code. Do not assume that commit `6ac98df` directly caused the regression: its production change is confined to `Source/Threading.Linux.cpp`, which is excluded from every Windows build. The only Windows-compiled addition is an isolated test in `TestThread.cpp`, and a focused `TestInterProcess.cpp` run does not execute it.

Determine why the Windows async-socket transport accumulates the latency, prove the cause in the debugger or with temporary instrumentation, implement the root fix, verify the improvement, then commit and push the result.

## Leading hypothesis: Nagle and delayed acknowledgments

Treat this as the leading hypothesis to verify, not as a predetermined fix.

`async_tcp_socket::NetworkProtocolConnection::SendString` encodes every small `WString` as a separate frame. It permits only one outstanding native write and submits the next frame from `OnWriteCompleted`. The shared scenarios deliberately contain consecutive sends on the same connection, including:

```C++
connection->SendString(L"Tom:Good");
connection->SendString(L"Stop");
```

The Windows implementation creates client and accepted TCP sockets without enabling `TCP_NODELAY`. On Windows, a small send-send-receive sequence can encounter the Nagle algorithm and delayed acknowledgments, adding roughly `RTT + 200 ms` at a serialized exchange. The tests repeat each scenario 20 times, and the channel scenario contains multiple small serialized exchanges, so this could plausibly accumulate to the observed duration. NamedPipe and HTTP do not use this raw TCP behavior.

Relevant code:

- `Source/InterProcess/AsyncSocket/AsyncSocket.h`
  - `NetworkProtocolConnection::SendString`
  - `NetworkProtocolConnection::OnWriteCompleted`
  - `NetworkProtocolConnection::StopConnection`
- `Source/InterProcess/AsyncSocket/AsyncSocket.Windows.cpp`
  - client socket creation and `ConnectEx` setup
  - accepted socket completion and setup
  - `ConnectionState::DeliverWrite`
- `Test/Source/TestInterProcess.cpp`
  - `RunTextNetworkProtocol`
  - `RunNetworkProtocolChannel`
  - `RunAsyncSocketNetworkProtocolTestCases`

Microsoft's [`TCP/IP-specific Issues`](https://learn.microsoft.com/en-us/windows/win32/winsock/tcp-ip-specific-issues-2) documentation describes the Nagle delay for the send-send-receive programming model. Use authoritative Windows/Winsock documentation when deciding where and when a socket option should be applied.

## Competing explanations that must be ruled out

### Bounded protocol write drain

`NetworkProtocolConnection::StopConnection` waits up to `WriteDrainTimeout == 1000` milliseconds when accepted frames remain queued. The scenarios signal their completion events immediately after final sends, so an external `Stop()` intentionally races write completion.

Count or break on the `now >= deadline` path. If the slow run repeatedly reaches that branch, determine why `OnWriteCompleted` does not drain the FIFO promptly. Do not merely reduce or remove the timeout.

### Client startup retries

The server and two clients run on separate thread-pool tasks. A client can attempt `ConnectEx` before the listener is ready. Windows retries after `AsyncSocketClientRetryDelay == 100` milliseconds.

Inspect `ConnectionState::clientAttempts`, `ScheduleRetry`, and connection timestamps. If the delay occurs in 100-millisecond increments before connection, fix the startup or retry cause rather than changing unrelated write behavior.

### Runtime construction and shutdown

Every repetition creates one server and two clients. Each Windows object owns an `IocpRuntime` with a completion worker and a callback worker, so one test case creates and joins 120 worker threads over 20 repetitions. Native shutdown also drains accepts, I/O completions, callbacks, timers, and the IOCP workers.

Measure construction, protocol exchange, connection stop, and runtime stop independently. Confirm whether runtime churn is material before refactoring it.

### Test watchdog contention

`TimeoutThread` busy-spins until all three scenario tasks complete or five seconds elapse. It can add CPU contention, but it predates the GCC fix and is shared by AsyncSocket, NamedPipe, and HTTP cases. Do not change it unless measurements prove it is part of the Windows-specific slowdown.

## Required investigation

1. Build Debug x64 with the repository-provided Windows scripts.
2. Run only `TestInterProcess.cpp` and record elapsed time for every transport case.
3. Record per-repetition timing for the two async-socket cases, separating:
   - server/client construction;
   - first connection attempt and any retries;
   - protocol message exchange;
   - protocol write draining;
   - native connection and IOCP runtime shutdown.
4. During a slow interval, use the repository-provided debugger scripts or temporary instrumentation to identify the actual wait or serialized network operation.
5. Specifically establish:
   - whether the write-drain deadline is reached;
   - whether `clientAttempts` exceeds one;
   - whether a small second send waits while earlier TCP data is unacknowledged;
   - whether IOCP worker startup or shutdown accounts for a significant fraction of the duration.
6. Remove all temporary diagnostics before the final commit unless they provide lasting, appropriately scoped test value.

## Fix requirements

- Fix the measured root cause rather than optimizing the test or hiding the latency.
- If Nagle/delayed ACK is confirmed, configure every connected Windows async TCP socket consistently, including both client-created and server-accepted sockets, at a lifecycle point where failures can be reported correctly.
- Preserve the one-outstanding-write contract, FIFO ordering, framing, callback ordering, and hard-drain shutdown guarantees.
- Preserve `InterProcessTestRepeatCount == 20` and the consecutive-send coverage.
- Do not add sleeps, weaken assertions, enlarge the watchdog, skip cases, or reduce retry/drain guarantees merely to make the timing look better.
- Do not change or revert the GCC thread-completion fix.
- Avoid unrelated NamedPipe, HTTP, Linux, or macOS refactoring. If a common change is genuinely required, explain why platform-local correction is insufficient and verify every affected platform that is available.
- Do not add a brittle wall-clock assertion. Use timing to verify the investigation; use deterministic state/behavior coverage for any permanent regression test.

## Acceptance criteria

- The slow path is identified with direct evidence, not inferred only from total case duration.
- The focused Windows Debug x64 run passes all `TestInterProcess.cpp` cases.
- The two async-socket protocol cases show a clear and repeatable improvement under the same machine, configuration, and execution method used for the baseline.
- The final run does not repeatedly hit the one-second write-drain deadline or unnecessary 100-millisecond connection retries unless either is proven unrelated and documented.
- NamedPipe, HTTP, and native async-socket tests remain passing.
- The complete Debug x64 UnitTest suite passes without a CRT memory-leak report.
- Debug/Release and Win32/x64 builds all succeed.

## Windows verification

Use only the repository-provided scripts and follow `.github/copilot-instructions.md`, `Project.md`, and the referenced build, execution, debugging, and native-dialog guidance.

- Build `Test/UnitTest/UnitTest.sln` through `.github/Scripts/copilotBuild.ps1` for Debug/Release and Win32/x64.
- Run the focused `TestInterProcess.cpp` cases through `.github/Scripts/copilotExecute.ps1` in Debug x64, ensuring the file is not skipped by the `.vcxproj.user` filter.
- Run `TestInterProcess_AsyncSocket.cpp` in Debug x64.
- Run the complete Debug x64 UnitTest suite.
- Inspect the ends of `.github/Scripts/Build.log` and `.github/Scripts/Execute.log`; require passing tests, zero build errors, and no Debug CRT memory-leak report.
- Record the before/after timings and the evidence that selected the fix in `.github/TaskLogs/Copilot_Investigate.md`.

Commit only the intentional fix, tests, and investigation record, then push the current branch.
