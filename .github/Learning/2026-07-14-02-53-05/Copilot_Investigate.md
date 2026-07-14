# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

TODO_Task_Linux.md , commit all local changes and push to master, rebase if conflict. If new compiling or linking options are needed, change ../Tools/Ubuntu/vl/makefile-cpp, and run `vgo uci VlppOS` to propogate the change to the current repo. Actual test cases in TestInterProcess_AsyncSocket.cdpp is not supposed to change unless you have run out of every possible ways, as these test cases are going to serve implementations in 3 different platforms to keep them consistency with each other.

# UPDATES

# TEST [CONFIRMED]

Use the five existing shared scenarios in `Test/Source/TestInterProcess_AsyncSocket.cpp` without changing their scenario bodies:

1. Repeat long full-duplex transfers in both directions and verify byte order/content, multiple positive read blocks, exact retained-buffer identity, and exactly one write completion.
2. Reject one accepted connection and verify terminal callback ordering and exactly one disconnection.
3. Call `Stop` from `OnRead` and verify nested stop returns only after the one terminal disconnection notification, with no later callbacks.
4. Start a client before its server, observe at least one nonfatal connection failure, then start the server and verify retry, connection, and data transfer.
5. Stop a client during its retry delay and verify `WaitForServer` unblocks, intentional cancellation is not fatal, disconnection occurs once, and no callback runs after stop returns.

The Linux binding must activate these unchanged cases with `linux_socket::AsyncSocketServer`, `linux_socket::AsyncSocketClient`, `EventObject::WaitForTime`, and a 65536-byte maximum read block. A full Linux rebuild must show that `AsyncSocket.Linux.cpp` is compiled and that the final link retains `liburing` after or independently of object ordering. The focused run from `Test/Linux` must pass all five cases with bounded waits and a zero exit status. Source review must additionally confirm that each runtime has one blocking CQ worker, each SQE owns a unique retained context, all target and cancellation CQEs drain before ring/FD release, and no user callback is invoked while an implementation lock is held.

## Confirmation

The clean baseline build from `Test/Linux` succeeds, but its compile commands contain no `AsyncSocket.Linux.cpp` and its final link command contains no `liburing`. Running `Bin/UnitTest /C /F:TestInterProcess_AsyncSocket.cpp` from that folder exits successfully while reporting `Passed test files: 1/1` and `Passed test cases: 0/0`. Source inspection explains the empty run: both the GCC/Linux include branch and the GCC/Linux `TEST_FILE` invocation branch are empty, and the requested Linux product files do not exist. This confirms that Linux currently provides no implementation or active shared-test coverage.

# PROPOSALS

- No.1 Implement a retained single-worker liburing runtime [CONFIRMED]

## No.1 Implement a retained single-worker liburing runtime

Add the GCC timed-event prerequisite with a normalized absolute deadline around `pthread_cond_timedwait`, using the condition variable's platform clock (`CLOCK_MONOTONIC` on Linux and the default `CLOCK_REALTIME` on macOS). Keep timed waiting off the GCC `WaitableObject` base, loop across spurious wakes, and preserve the current manual-reset, auto-reset, waiter-counter, and lock-reacquisition behavior in `EventObject::WaitForTime`.

Add public PIMPL-shaped Linux server/client declarations and a private implementation built around one `RingRuntime` for a client and one for a server. Each runtime owns one `io_uring`, probes accept/connect/receive/send/async-cancel/timeout support, serializes SQ preparation/submission, assigns a unique nonzero ID to every target, cancellation, empty-write, and stop-wake context, and runs one self-retained completion worker as the sole CQ consumer. The worker blocks in `io_uring_wait_cqe`, handles one context without runtime locks, calls `io_uring_cqe_seen`, and only then removes/releases the retained context. An explicit NOP context wakes an otherwise idle worker during runtime shutdown.

Keep native connection/server state in retained private objects rather than operation pointers to public PIMPL owners. Accept, connect, receive, send, timeout, cancellation, and wake contexts retain their owner and operation-specific sockaddr, timeout, 64 KiB receive storage, or exact write buffer. Use single-shot accept/receive, fresh IDs/contexts for rearming and short sends, `MSG_NOSIGNAL`, an asynchronous NOP for empty writes, and `io_uring_prep_cancel64` against unique target IDs. Cancellation CQEs count separately and never substitute for consuming target CQEs.

Implement two-phase idempotent shutdown. Every first phase marks stopping before cancellations, prevents rearming/callback claims, cancels each live target once, and uses `shutdown` without closing an FD still referenced by a request. External callers wait for all target and cancellation CQEs, other callbacks, terminal notification, worker exit, and final ring release. A call from the runtime's own callback worker never waits on itself: it waits only for any other callback, delivers the single terminal disconnection reentrantly before returning, and leaves contexts/self-retained runtime state alive. Repeated external `Stop` calls always participate in the drain phase, so later cleanup cannot be skipped merely because stopping began on the worker. Close descriptors only after every referencing target CQE has been seen.

For the server, synchronously bind/listen only on `127.0.0.1`, submit exactly one accept before `Start` returns, rearm before the user accept callback, retain every offered connection before the callback, and stop/drain rejected or shutdown-racing descriptors. For the client, submit asynchronous connect attempts on fresh sockets, report each failed attempt, use an `io_uring` timeout for the common retry delay, signal `WaitForServer` only after connection notification or terminal stop, and cancel both connect and retry operations during stop.

Integrate the files in the MSBuild metadata, select Linux/macOS sources conditionally in `Test/Linux/vmake`, and link `liburing`. Correct the shared linker template in `../Tools/Ubuntu/vl/makefile-cpp` so link-only options occur after objects, propagate it with `vgo uci VlppOS`, and use `-luring` only on non-Darwin Linux. Fill only the three existing Linux placeholders in `TestInterProcess_AsyncSocket.cpp`; leave every shared helper and scenario body unchanged.

### CODE CHANGE

Added `AsyncSocket.Linux.h` with the requested PIMPL-shaped, port-only `AsyncSocketServer` and `AsyncSocketClient` public classes. Added `AsyncSocket.Linux.cpp` with retained private server, connection, operation, and runtime state. A server shares one runtime with all accepted connections, while each client owns one runtime. Every runtime has one blocking CQ worker, a unique nonzero ID and retained context per SQE, serialized submission, and explicit operation-drain accounting.

Implemented asynchronous accept, connect, receive, short-send continuation, empty writes through NOP, retry through an `io_uring` timeout, target cancellation through `io_uring_prep_cancel64`, and a NOP wake for runtime shutdown. Target and cancellation CQEs are tracked independently. Descriptors remain owned until all referencing target CQEs have been seen, callbacks are claimed under state locks and invoked after releasing them, and external or callback-originated repeated `Stop` calls follow the same idempotent drain contract without making the CQ worker wait for itself.

The server synchronously creates a loopback-only listener and posts one accept before `Start` returns. The client creates a fresh loopback socket for each connect attempt and applies `SO_REUSEADDR` before the implicit connect bind. This matches the listener reuse setting and prevents a kernel-selected client source port left in `TIME_WAIT` from blocking a later listener on that port. Submission pressure uses a condition variable tied to CQ progress: external submitters wait without polling after `-EBUSY`, while the sole CQ worker returns to consume another completion; post-publication `-EAGAIN` fails fast because ownership cannot be rolled back safely.

Extended GCC `ConditionVariable` timed waiting and `EventObject::WaitForTime` with Linux monotonic deadlines, normalized timeout arithmetic, signal generations, and released-waiter accounting so spurious wakes do not consume or invent event signals. Updated the synchronization knowledge-base pages for the platform clock and timed-event semantics.

Integrated the Linux implementation in `Test/Linux/vmake`, the Visual Studio project, and its filters. Added `-luring` only to the non-Darwin generated build and corrected `../Tools/Ubuntu/vl/makefile-cpp` so objects precede link options and platform libraries for GCC, Clang, and coverage builds. Ran `vgo uci VlppOS`, which propagated the same template into `.github/Ubuntu/vl/makefile-cpp`.

Filled only the existing Linux include, timed-wait, and invocation placeholders in `TestInterProcess_AsyncSocket.cpp`, binding `linux_socket::AsyncSocketServer`, `linux_socket::AsyncSocketClient`, and the 65536-byte read maximum. The shared helpers and all five scenario bodies remain unchanged. Rebasing preserved the concurrently added macOS implementation and the Darwin/non-Darwin source and link selection.

### CONFIRMED

The final clean build from `Test/Linux` completed successfully and compiled both `AsyncSocket.Linux.cpp` and the existing shared async-socket test file. Its final Clang link command lists all object files before `-luring`, confirming the propagated linker-template correction.

The focused command `Bin/UnitTest /C /F:TestInterProcess_AsyncSocket.cpp` passes 1/1 files and 5/5 shared cases. Ten consecutive focused processes also passed. A traced stress failure initially proved that Linux had auto-bound an outgoing client to a future fixed listener port; the resulting non-reuse `TIME_WAIT` entry caused `bind` to return `EADDRINUSE`. After applying client-side `SO_REUSEADDR`, ten consecutive focused runs and an immediate full run passed while those entries were still accumulating. Independent reproduction confirmed that reuse on both the old client and new listener is the targeted Linux fix.

The threading-focused command passes 1/1 files and 11/11 cases. The final unfiltered command passes 11/11 files and 115/115 cases. Source reviews found no remaining lifecycle, ownership, callback-locking, cancellation-drain, submission-progress, platform-selection, or linker-order defect.
