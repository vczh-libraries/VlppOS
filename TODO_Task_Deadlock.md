investigate repro

# Intermittent UnitTest deadlocks

Collect platform-specific reproduction evidence, identify every confirmed wait cycle, and fix only deadlocks supported by captured stacks and deterministic lifecycle analysis.

## macOS

### Reproduction method

Run the complete Debug arm64 UnitTest suite from `Test/Linux` as 30 independent sequential processes:

- execute `Bin/UnitTest /C` with no file filter;
- preserve each process's raw output and exit status;
- allow 60 seconds per process because `TestThread.cpp` contains intentional bounded waits and the normal redirected suite takes about 12 to 14 seconds;
- if a process exceeds 60 seconds, capture all threads with `/usr/bin/sample` before terminating it, then reproduce the last flushed test under `lldb`;
- distinguish a nonzero exit from a timeout, and distinguish a timeout before the pass summaries from a hang in `FinalizeGlobalStorage()` after the summaries.

The unit-test framework prints each file and case immediately before invoking it, and macOS console output is flushed by every write. The last printed case in an incomplete log therefore identifies the active case, while both final pass summaries prove that all registered cases returned.

### Result: not reproduced

All 30 runs exited successfully. Every run reported:

- `Passed test files: 13/13`
- `Passed test cases: 167/167`

There were zero timeouts, zero incomplete summaries, zero crashes, and therefore zero captured deadlocks. No test file or test case caused a macOS deadlock in this sample. In particular, all native async-socket, network-protocol, HTTP-request, MiniHttp, shutdown, timeout-race, and `TestThread.cpp` cases completed on every run.

This result does not prove that a lower-probability race is impossible, but it provides no evidence for assigning a macOS root cause or changing production synchronization.

### Analysis

Do not infer a deadlock merely from the name of the last case in a successful run or from the suite's intentional thread waits. A future macOS timeout must be classified from a stable blocked stack:

- `vl::inter_process::async_tcp_socket::macos_socket::ConnectionState::WaitForDrain` in `Source/InterProcess/AsyncSocket/AsyncSocket.macOS.cpp` waits for active callbacks, native contexts, retry timers, and locally queued tasks to reach zero.
- The macOS server stop path in the same file waits for listener cancellation and any other stop finalizer.
- `vl::inter_process::async_tcp_socket::HttpRequestConnection::StopConnection` in `Source/InterProcess/AsyncSocket/AsyncSocket_HttpRequest.cpp` coordinates the stop owner, active socket calls, socket callbacks, HTTP callbacks, and timeout cancellation.
- A timeout after both pass summaries belongs to global cleanup; inspect the `SocketHttpRegistry` finalizer in `Source/InterProcess/AsyncSocket/AsyncSocket_HttpServerApi.cpp` rather than attributing it to `TestThread.cpp`.

These are diagnostic boundaries, not confirmed defects. Without a sampled cycle, changing a wait, adding a timeout, or skipping drain work could hide a use-after-free instead of fixing a deadlock.

### Proposed action

No macOS production fix is proposed from this 30-run result. Keep the current hard-drain guarantees and do not add sleeps, reduce repetitions, weaken callbacks, or introduce a wall-clock assertion.

If a future run times out:

1. Preserve its raw log and identify whether the timeout occurred inside a case or after the pass summaries.
2. Capture all process threads before killing it, then reproduce the named file or case under `lldb` from `Test/Linux`.
3. Record the two sides of the wait cycle and the exact counter, callback, dispatch source, or stop-owner transition that never completes.
4. Fix the owner of that transition so every accepted, cancelled, failed, and reentrant path balances its drain state exactly once and wakes the corresponding condition variable.
5. Add a deterministic regression that forces the proven ownership order with events or barriers; do not rely on repeated timing alone.
6. Re-run the focused regression, the complete suite 30 times, and final global cleanup with the same bounded monitoring.

### Acceptance criteria for any future macOS fix

- A pre-fix stack sample demonstrates a specific wait cycle rather than ordinary bounded work.
- A deterministic regression fails on the broken ownership order and passes after the fix.
- Nested callback `Stop` remains non-self-waiting, while external `Stop` remains a hard drain boundary.
- No callback, Network.framework completion, retry timer, local dispatch task, HTTP timeout, or shared-server finalizer can touch released state after `Stop` returns.
- Thirty complete macOS runs finish with all 13 files and 167 cases passing and with no timeout during `FinalizeGlobalStorage()`.
