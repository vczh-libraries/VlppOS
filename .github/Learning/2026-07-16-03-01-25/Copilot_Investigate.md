# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

The last two commits are a new task executed and verified on Windows. You need to run the unit test and make sure it works on Ubuntu. commit and push all local changes.

# UPDATES

# TEST [CONFIRMED]

Use the repository's Ubuntu workflow from `Test/Linux`: perform a clean Debug x64 build with `.github/Ubuntu/build.sh -f`, then run the generated `Bin/UnitTest` asynchronously with `/C`.

The last two commits add the cross-platform `SocketHttpServerApi` / `SocketHttpClientApi` layer and deterministic timeout/stop-race coverage. First run the focused `TestInterProcess_AsyncSocket_MiniHttpApi.cpp` and `TestInterProcess_HttpRequest.cpp` files so compile failures, native Linux socket failures, timeout coordination failures, or hangs are isolated. Then run the complete unfiltered suite to detect regressions in other VlppOS behavior.

Success requires the clean build to compile and link every common and Linux-specific source, both focused files to finish with all cases passing (including the malformed-wire 408 path and external-stop/firing-timeout race), and the complete suite to finish with every test file and case passing. No test may hang or require a production-duration sleep.

The clean build succeeds. The focused MiniHttp file passes 1/1 file and 13/13 cases, and the focused HTTP-request file passes 1/1 file and 36/36 cases. The first complete-suite run instead exits with code 1 during `AsyncSocket (Channel)` in `TestInterProcess.cpp`, before printing a failure or final summary. This reproduces an Ubuntu-only integration failure outside the two directly changed files and confirms that focused success alone is insufficient.

The failure is timing-dependent rather than order-dependent. A later complete run reached MiniHttp but received an unexpected first result in the persistent-routing case. Stressing MiniHttp alone with `/C` reproduced by iteration 3, this time with the raw CORS sequence completing with fewer than nine responses. Stressing the three inter-process files reproduced by iteration 2 with a wrong/error result in the application-500 sequence. In contrast, 20 MiniHttp runs with `/R` and a debugger-delayed run passed. The same process can therefore lose or terminalize different asynchronous exchanges depending on timing; passing one focused invocation is not sufficient evidence.

# PROPOSALS

- No.1 Use a monotonic clock for asynchronous duration deadlines [CONFIRMED]

## No.1 Use a monotonic clock for asynchronous duration deadlines

The raw diagnostic exposes the hidden terminal error: `The HTTP peer timed out before sending a response header.` A nominal 30-second deadline fires during a sub-second MiniHttp run. On GCC platforms, `DateTime::LocalTime()` reaches `localtime`'s shared static `tm` and then passes that buffer to the mutating `mktime`. Multiple HTTP timeout workers call this concurrently, so the wall-clock values have a data race and can jump beyond a newly stored deadline. Debugger serialization and `/R` timing suppress the race.

Replace elapsed-duration bookkeeping in `HttpRequestTimeoutController` with `std::chrono::steady_clock` time points. Use the same monotonic clock for `NetworkProtocolConnection`'s bounded queued-write drain, which is the only other product duration loop using `DateTime::LocalTime()` and can explain the earlier channel failure. This fixes the owning deadline state without modifying imported date-time code or making a platform-specific exception.

Move the existing raw CORS `AssertState` check ahead of derivative response-count assertions so a future transport error remains visible instead of being masked, while retaining one check rather than duplicating it. Verify the fix with the focused files, repeated `/C` MiniHttp and inter-process runs, a clean build, and repeated complete suites.

### CODE CHANGE

- Replaced `DateTime::LocalTime().osMilliseconds` deadline arithmetic in both `HttpRequestTimeoutController` and `NetworkProtocolConnection`'s write drain with `std::chrono::steady_clock` time points and rounded monotonic waits.
- Moved the raw CORS sequence's existing `AssertState` check before response-count assertions so the actual asynchronous transport error is reported first.
- Regenerated `Test/Linux/vmake.txt` and `Test/Linux/makefile` through `.github/Ubuntu/build.sh`; they now include the common HTTP values, MiniHttp client/server implementations, and MiniHttp test file added by the preceding task.

### CONFIRMED

The monotonic clock removes both unsafe `DateTime::LocalTime().osMilliseconds` duration loops from `Source`. The HTTP controller still resets its deadline on `Refresh`, cancels and drains its worker through the existing condition-variable protocol, and rounds positive sub-millisecond remainders up before waiting. The network-protocol stop path retains its original bounded write-drain behavior without depending on a mutable wall clock.

After the incremental build, the focused HTTP-request file passed 1/1 file and 36/36 cases, the three inter-process files passed 3/3 files and 21/21 cases, and MiniHttp passed 50 consecutive `/C` runs. This directly exercises the path that had failed by iteration 3 before the change.

The final clean rebuild compiled and linked all common and Linux-specific sources, including every newly generated MiniHttp build entry. Two consecutive complete runs from those clean artifacts each passed 13/13 files and 167/167 cases. No premature response timeout, channel failure, test hang, or assertion occurred.
