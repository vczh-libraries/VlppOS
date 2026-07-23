# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

Run `UnitTest` project and fix and test break.

# UPDATES

## UPDATE

The CI works perfectly on Windows so focus on Linux specific part if any issue is found

# TEST [CONFIRMED]

Build and run the existing complete Linux `UnitTest` project from `Test/Linux/UnitTest`, using the repository-provided build script and the unit-test runner's `/C` option. The failure is reproduced if the project fails to build or the test executable stops before reporting that every test file and test case passed. Success requires a clean Linux build and a complete passing test run without changing already-correct Windows behavior.

The clean Linux build succeeded. Running `./Bin/UnitTest /C` reproduced the problem in `TestInterProcess.cpp`, in the `SocketHttp portable pair (NetworkProtocol)` test case. The executable aborted at `TEST_ASSERT(!timeoutThread->timeout)`, before reaching the unit-test pass summary.

# PROPOSALS

- No.1 USE REENTRANT UNIX DATE-TIME CONVERSIONS [CONFIRMED]
- No.2 MAKE FAILED POLL DELIVERY TEST DETERMINISTIC [CONFIRMED]

## No.1 USE REENTRANT UNIX DATE-TIME CONVERSIONS

The Linux `DateTime` implementation keeps pointers returned by `localtime()` and `gmtime()` and then passes those shared objects to `mktime()`. On glibc, both functions use the same process-wide static `tm` storage and are documented as unsafe under `race:tmbuf`. The failing test spins on `DateTime::LocalTime()` in `TimeoutThread`, while each Socket HTTP response calls `DateTime::UtcTime()` to generate its `Date` header. A direct reproduction on this host showed that interleaving the two conversions can shift the calculated timestamp by 28,800,000 milliseconds. The unsigned watchdog subtraction can therefore exceed five seconds or underflow almost immediately, failing before the three protocol workers finish. Windows uses caller-owned `SYSTEMTIME` storage, which agrees with the reported passing Windows CI result.

Fix the source of truth in the upstream `Vlpp` repository by replacing all Unix `localtime()` and `gmtime()` calls with `localtime_r()` and `gmtime_r()` calls writing to caller-owned stack `tm` values. Use separate stack values for the two conversions in `UtcToLocalTime()`. Regenerate the `Vlpp` release, import the generated Linux release into `VlppOS`, and rerun the complete Linux `UnitTest` suite. This preserves the existing `DateTime` API and conversion semantics while removing the shared-buffer data race.

### CODE CHANGE

In `Vlpp/Source/Primitives/DateTime.Linux.cpp`, create caller-owned `tm` values for `FromOSInternal`, `LocalTime`, `UtcTime`, `LocalToUtcTime`, and both sides of `UtcToLocalTime`, fill them with `localtime_r()` or `gmtime_r()`, and pass their addresses to the existing conversion helpers. Regenerate `Vlpp/Release/Vlpp.Linux.cpp`, copy that generated file to `VlppOS/Import/Vlpp.Linux.cpp`, and keep all other platform implementations unchanged.

### CONFIRMED

The upstream Linux implementation now uses caller-owned `tm` values for every conversion named above, with separate local and UTC values in `UtcToLocalTime()`. CodePack regenerated `Vlpp/Release/Vlpp.Linux.cpp`, and that generated file is byte-for-byte identical to `VlppOS/Import/Vlpp.Linux.cpp`. The Windows implementation was not changed. A clean upstream Linux build succeeded and its complete unit-test run passed 31/31 files and 462/462 cases. In `VlppOS`, the focused inter-process file passed the two portable SocketHttp pairs that originally tripped the five-second watchdog, and the final clean full run passed 13/13 files and 202/202 cases. This confirms that removing glibc's shared `tm` buffer race fixes the original Linux-only timeout without changing Windows behavior.

## No.2 MAKE FAILED POLL DELIVERY TEST DETERMINISTIC

After the date-time race is removed, the complete Linux run reaches `SocketHttp failed poll delivery is requeued before a replacement poll` and times out while waiting for the replacement poll. The test stops the first HTTP client after the server claims its long poll, then assumes the server response completion must fail. `SocketHttpClientApi::Stop()` completes that client's query locally with `Stopped`; it does not prove that the server observed a failed response. TCP write completion only reports that bytes were accepted locally, not acknowledged by the peer, so the server send may legally succeed after the peer has begun closing. On Linux, the claimed hook also blocks the single `RingRuntime` completion worker, preventing it from processing the peer disconnect until the hook releases. The server therefore reports the first response as successful, correctly consumes the queued message, and leaves the infinite replacement poll waiting because there is nothing to requeue. Windows passing this test reflects a different scheduling outcome, not a stronger transport guarantee.

Keep the production requeue rule unchanged: requeueing a locally successful send would duplicate messages without a protocol acknowledgement. Instead, extend the existing private poll test hooks with an optional callback that requests cancellation of a claimed HTTP request context immediately before response submission. Use it only for the first claimed poll in this test. Cancelling the server context makes `RespondBytes()` reject the submission and deterministically drives `FinishPollResponse(..., false)`, after which the existing assertions verify that the same retained UTF-8 bytes are delivered successfully by the replacement poll.

### CODE CHANGE

In `Source/InterProcess/AsyncSocket/AsyncSocket_HttpServer.cpp`, add a test-hook callback that returns whether the claimed poll context should be cancelled before `RespondBytes()`, invoke it after the existing claimed hook, and reset it with the other poll hooks. In `Test/Source/TestInterProcess.cpp`, expose the callback through `SocketHttpPollHookScope`, request cancellation only while handling the first claim, and retain the existing completion-token and response-body assertions. Regenerate `Release/VlppOS.cpp`. Do not change the cross-platform socket write-completion contract or the production success/requeue decision.

### CONFIRMED

The new hook defaults to an empty callback and therefore has no effect outside tests. In the failed-delivery test it cancels only the first claimed server request context. The subsequent `RespondBytes()` submission deterministically fails, the existing completion hook records `succeeded == false`, and the unchanged production logic restores the retained outbound message at the FIFO head. The replacement poll then completes successfully with the same token, Unicode text, and exact UTF-8 bytes. The focused `TestInterProcess.cpp` run passed 18/18 cases once and then passed five additional consecutive stress runs. The complete Linux suite passed 13/13 files and 202/202 cases both before and after a clean rebuild. CodePack regenerated `Release/VlppOS.cpp`, and review found no production contract or platform-specific behavior change. This confirms the replacement-poll timeout was caused by the test's TCP timing assumption and that deterministic context cancellation verifies the intended requeue behavior on Linux and Windows.
