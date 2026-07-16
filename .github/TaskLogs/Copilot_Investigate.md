# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

looks like Windows and macOS are both fine but only Ubuntu has frequent deadlocks. I think you are almost able to limit the scope to linux specific files. try to fix the issue, and you have to also pass 30 consecutive good runs to say you finishing the work.

# UPDATES

# TEST [CONFIRMED]

Use the existing shared async-socket protocol and channel cases in `TestInterProcess.cpp` as the root-cause-sensitive reproduction. Perform a clean Ubuntu Debug x64 build from `Test/Linux`, then run the complete `Bin/UnitTest /C` process repeatedly with complete raw output and a 60-second process boundary. A process is successful only when it exits with code 0 and prints both `Passed test files: 13/13` and `Passed test cases: 167/167`; a timeout, watchdog assertion, abort, incomplete summary, or failure in any test case is a failure.

The existing 30-run Ubuntu campaign is the baseline: 15 processes passed, eight hard hangs were localized to `AsyncSocket (NetworkProtocol)`, one hard hang was localized to `AsyncSocket (Channel)`, and six additional `NetworkProtocol` processes aborted after the five-second watchdog fired. The captured hard hang showed the test main thread unwinding through `pthread_cond_destroy` while three ThreadPool workers still waited on stack-owned conversation events. Reproduce at least one protocol/channel failure against the current source before changing it, and use focused `TestInterProcess.cpp` runs for fast iteration.

The fix is accepted only when the focused inter-process tests pass, one complete unfiltered suite passes, and then 30 independent complete unfiltered suites pass consecutively from the final clean build. The consecutive count resets to zero after any nonzero exit, missing summary, assertion, abort, or timeout. Preserve the dedicated client-before-server retry scenarios; the fix must not remove Linux retry or shutdown coverage merely to make the shared protocol scenarios deterministic.

The clean Ubuntu build at source commit `aff9572` succeeded. The first focused `Bin/UnitTest /C /F:TestInterProcess.cpp` process then exceeded the 60-second boundary with its last flushed case at `AsyncSocket (NetworkProtocol)`. It produced neither an assertion diagnostic nor pass summaries before exit code 124. This reproduces the hard failure on the unchanged current source with a boundary more than four times the normal complete-suite duration.

# PROPOSALS

- No.1 Synchronize Linux shared protocol startup [DENIED]
- No.2 Restore SpinLock exclusion and order the channel handshake

## No.1 Synchronize Linux shared protocol startup

The protocol and channel scenarios test framing, routing, sender ids, repeated exchanges, and hard shutdown. They are not the owner of client-before-server retry coverage. Both shared runners already accept `synchronizeServerStartup`; when enabled, the two client tasks wait on a manual-reset event that the server task signals only after `server->Start()` returns. Windows enables this barrier, while the Linux-only `VCZH_GCC && !VCZH_APPLE` binding currently disables it.

On Linux, `ServerState::Start` does not return until `socket`, `bind`, `listen`, the listener `RingRuntime`, and the first submitted accept operation are ready. Releasing the clients at that boundary therefore removes the incidental listener race. This matters particularly on Linux because each server and client eagerly constructs and probes its own 1024-entry `io_uring` and starts a completion worker, so three runtime startups compete with the test's five-second scenario watchdog when the clients are allowed to run first.

The captured deadlock proves that failure unwinding destroys conversation events while worker tasks still wait on them; it does not prove a production `io_uring` ownership cycle. Do not change Linux connection retry, callback draining, or `Stop()` without such evidence. The shared native test file already contains `AsyncSocket retry then connect` and `AsyncSocket Stop during retry`, which intentionally start a Linux client without a ready server and remain unchanged.

### CODE CHANGE

Change only the argument in the `VCZH_GCC && !VCZH_APPLE` registration in `Test/Source/TestInterProcess.cpp` from `false` to `true`. This changes Ubuntu behavior only: the existing barrier becomes active for `AsyncSocket (NetworkProtocol)` and `AsyncSocket (Channel)`, while Windows, macOS, all scenario repetitions and assertions, and the dedicated Linux retry and cancellation tests remain unchanged.

### DENIED

The incremental build succeeded and the first focused `TestInterProcess.cpp` run passed 1/1 file and 3/3 cases. The second focused run exceeded the 60-second boundary with its last flushed case at `AsyncSocket (NetworkProtocol)`, exactly as the original hard failure did. The listener-start barrier removes a real incidental retry but does not remove the frequent Linux failure, so it is insufficient as the fix and the source experiment was reverted.

The captured Channel trace also proves an independent post-connect ordering defect: Tom can receive the client-id announcement and send `Hello` before the server has submitted Jerry's client-id announcement. Jerry then treats the early `Hello` as the id package, throws, loses the message, and leaves all three tasks waiting. This occurs after both clients connected, so listener startup synchronization cannot fix it.

## No.2 Restore SpinLock exclusion and order the channel handshake

A parent-launched `strace -ff -k` run with Proposal No.1 active disproved the remaining listener-start hypothesis. All `io_uring_setup(1024)` and probe calls completed, but in protocol iteration five two different ThreadPool workers simultaneously executed the server lambda and constructed listeners for port 38504. Both reached `bind`; one reached `listen` first and the other failed with `EADDRINUSE`. The failing worker threw before incrementing the scenario completion counter, the ThreadPool suppressed the exception, and the remaining tasks waited until the watchdog failure began the previously captured teardown deadlock.

The custom GCC ThreadPool does not intentionally duplicate a task. Its queue is protected by the common `SpinLock`, whose `Enter` loop has violated mutual exclusion since commit `df2a7da`. `compare_exchange_strong` overwrites its `expected` argument when it fails, but `SpinLock::Enter` initializes `expected = 0` only once outside the retry loop. The following three-thread schedule is therefore legal in the current code:

1. A owns the token at 1.
2. B's compare-exchange from 0 to 1 fails, changing B's `expected` to 1.
3. A releases, then C acquires the token from 0 to 1.
4. B retries with `expected == 1`; its compare-exchange from 1 to 1 succeeds and B enters beside C.

Two Linux ThreadPool workers can consequently remove or mutate the same linked queue node at once. This exactly accounts for the two independently captured `RunTextNetworkProtocol::$_0` stacks and the missing client task. `QueueLambda`, `Func` copying, and the linked queue are otherwise ownership-safe. Windows uses its native thread pool and did not exercise this custom queue path; macOS did not reproduce the narrow schedule, but the primitive itself is common code and must be corrected at its owner rather than hidden in a Linux caller.

The channel package ordering observed in the earlier trace is a second defect that a correct ThreadPool does not remove. The server channel client batches the id announcement before its hello announcements, but Tom is allowed to process its id package and send `Hello` through another connection before Jerry processes the corresponding id package. A client must not begin the peer exchange until both clients have acknowledged their ids.

The first implementation passed the pre-fix-failing SpinLock regression, the focused thread and inter-process files, one complete suite, and the first 19 processes of the clean-build acceptance campaign. Process 20 then aborted at six seconds on the `AsyncSocket (NetworkProtocol)` watchdog, resetting the consecutive count. With queue exclusion repaired, this remaining failure is the independent listener scheduling trigger identified by Proposal No.1: the Linux shared scenario still releases both eager `io_uring` clients before the server task has committed its listener. Proposal No.1 was insufficient by itself because the broken queue could still duplicate the server task, but its startup boundary remains necessary in combination with the two root fixes. The implementation is therefore refined before resuming the acceptance campaign.

### CODE CHANGE

Reset the compare-exchange expected value to zero on every `SpinLock::Enter` attempt in `Source/Threading.cpp`. Add a high-contention `TestThread.cpp` regression that records the number of threads inside one SpinLock-protected section and fails if it ever exceeds one; the existing SpinLock tests increment an atomic counter and cannot detect overlapping owners.

In `TestInterProcess.cpp`, add an ids-received event and two guarded readiness flags to `ChannelChatData`. Have `RememberClientIds` publish each client's readiness after storing its ids, signal when both are ready, and move Tom's initial `Hello` send to an explicit method invoked by Tom's scenario task only after that event. This preserves all payload, sender-id, routing, repeat, and shutdown assertions while removing the invalid cross-connection ordering assumption.

Enable the existing `synchronizeServerStartup` barrier only in the `VCZH_GCC && !VCZH_APPLE` registration. Both shared Linux clients then wait until `server->Start()` has committed the listener. Do not change Linux `io_uring`, retry, callback-drain, or `Stop` behavior; the dedicated `retry then connect` and `Stop during retry` cases continue to cover client-before-server retry and cancellation.
