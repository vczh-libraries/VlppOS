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

- No.1 USE REENTRANT UNIX DATE-TIME CONVERSIONS

## No.1 USE REENTRANT UNIX DATE-TIME CONVERSIONS

The Linux `DateTime` implementation keeps pointers returned by `localtime()` and `gmtime()` and then passes those shared objects to `mktime()`. On glibc, both functions use the same process-wide static `tm` storage and are documented as unsafe under `race:tmbuf`. The failing test spins on `DateTime::LocalTime()` in `TimeoutThread`, while each Socket HTTP response calls `DateTime::UtcTime()` to generate its `Date` header. A direct reproduction on this host showed that interleaving the two conversions can shift the calculated timestamp by 28,800,000 milliseconds. The unsigned watchdog subtraction can therefore exceed five seconds or underflow almost immediately, failing before the three protocol workers finish. Windows uses caller-owned `SYSTEMTIME` storage, which agrees with the reported passing Windows CI result.

Fix the source of truth in the upstream `Vlpp` repository by replacing all Unix `localtime()` and `gmtime()` calls with `localtime_r()` and `gmtime_r()` calls writing to caller-owned stack `tm` values. Use separate stack values for the two conversions in `UtcToLocalTime()`. Regenerate the `Vlpp` release, import the generated Linux release into `VlppOS`, and rerun the complete Linux `UnitTest` suite. This preserves the existing `DateTime` API and conversion semantics while removing the shared-buffer data race.

### CODE CHANGE
