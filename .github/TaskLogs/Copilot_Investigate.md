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
