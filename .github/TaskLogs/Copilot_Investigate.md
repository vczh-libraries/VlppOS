# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

Refer to TODO_Task.md for the feature added to the project which passes on Windows, but here it "looks like" a deadlock. Figure out why and fix it, commit and push.

# UPDATES

# TEST [CONFIRMED]

Run the existing async-socket-backed cases in `TestInterProcess.cpp` on Linux through the repository's UnitTest build. The focused test already exercises both the raw network-protocol adapter and the channel adapter over the concrete Linux async socket implementation, including connection, consecutive FIFO sends, and shutdown.

The problem is confirmed if the focused executable stops making progress rather than completing. Capture all thread backtraces with LLDB while it is stuck and identify the exact wait and lock ownership cycle. After the fix, the focused `TestInterProcess.cpp` cases must complete repeatedly, and the complete Linux UnitTest suite must pass.

The full Linux UnitTest project builds successfully through `.github/Ubuntu/build.sh -f`. Running `Bin/UnitTest /C /F:TestInterProcess.cpp` prints the `AsyncSocket (NetworkProtocol)` case and then makes no further progress; it was interrupted after five seconds. This reproduces the reported Linux-only stall in the existing shared coverage.

LLDB shows that the transport is not deadlocked. Both clients and the server have completed, all thread-pool workers are idle, no io_uring worker remains, `threadCounter` is 3, and `TimeoutThread::Run` has returned. The main thread alone is blocked in `Thread::Wait`, which waits for a Linux-only completion event that a custom `Thread` subclass never signals. Add a direct custom-derived-thread case to `TestThread.cpp` so the owning contract is covered independently of the inter-process integration test.

# PROPOSALS

- No.1 Centralize GCC thread completion for every derived thread

## No.1 Centralize GCC thread completion for every derived thread

Move GCC thread startup cleanup, the `Stopped` transition, completion-event signaling, and optional self-deletion into the common native thread entry path in `Threading.Linux.cpp`. Store the factory-created thread's auto-delete policy in the private platform `ThreadData`, defaulting to false for arbitrary subclasses, and reduce `ProceduredThread::Run` and `LambdaThread::Run` to invoking their supplied work. Capture the auto-delete policy before calling the virtual `Run` so the entry path never reads the object after signaling waiters, and delete only after completion has been published.

Set `threadState` to `Running` before `pthread_create` and roll it back to `NotStarted` on failure. Otherwise a fast `Run` can publish `Stopped` before `Thread::Start` overwrites it with `Running`. This change fixes the public inherited-`Thread` contract at its owner instead of adding a private completion event to `TimeoutThread`, and it applies to both Linux and macOS builds that share this source file. Keep Windows unchanged because `Thread::Wait` already waits on the native thread handle.

Add a focused `TestThread.cpp` case whose custom `Thread` subclass returns from `Run`; require `Wait` to return, its work to be visible, and its state to be `Stopped`. Retain the existing repeated async-socket network-protocol and channel cases as integration coverage.

### CODE CHANGE
