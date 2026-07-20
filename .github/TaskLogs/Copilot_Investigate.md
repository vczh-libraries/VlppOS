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
