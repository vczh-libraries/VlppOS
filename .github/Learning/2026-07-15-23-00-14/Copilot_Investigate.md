# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

TODO_Task_SocketCallback.md and TODO_Task_Task.md are the two new implemented works, the code passed unit test on Windows and macOS but on Linux it has build breaks. So I believe the test code should be correct, the only thing needs to fix is in the Linux specific part. Fix it until unit test pass, commit and push.

# UPDATES

# TEST [CONFIRMED]

Use the repository's Linux workflow from `Test/Linux` to perform a full Debug x64 rebuild with `.github/Ubuntu/build.sh -f`. The build must compile all common and Linux-specific async-socket sources, produce `Bin/UnitTest`, and the focused async-socket and HTTP-request test files plus the complete unit-test suite must pass from that folder.

The clean full build reproduces the reported Linux-only break while compiling `Source/InterProcess/AsyncSocket/AsyncSocket.Linux.cpp`. Clang rejects line 1216 because `ConnectionState::InstallCallback` passes its `AsyncSocketConnection* owner` to `IAsyncSocketCallback::OnInstalled(IAsyncSocketConnection*)` while `AsyncSocketConnection` is only forward declared at that definition point, so the required derived-to-base conversion is not yet known. Compilation stops with one error before the unit-test executable is linked. The common HTTP implementation files compile before this failure.

Success requires fixing the Linux-specific declaration/definition ordering without changing the already-passing shared tests or other platform implementations, rebuilding successfully through `build.sh`, passing focused `TestInterProcess_AsyncSocket.cpp` and `TestInterProcess_HttpRequest.cpp` runs, and passing the complete Linux unit-test suite.

# PROPOSALS

- No.1 Store the Linux callback owner through its interface [CONFIRMED]

## No.1 Store the Linux callback owner through its interface [CONFIRMED]

`ConnectionState` needs the owner only to test whether it is attached and to supply the public `IAsyncSocketConnection*` argument to `IAsyncSocketCallback::OnInstalled`. Change the Linux-only `owner` field from `AsyncSocketConnection*` to `IAsyncSocketConnection*`, matching the macOS implementation. Assignment from `this` remains at the later concrete wrapper definition, where its inheritance is complete, while the earlier callback-installation definition no longer needs a derived-to-base conversion from an incomplete class.

This fixes the issue at the state that owns the callback contract, without a cast, declaration reordering, common-code change, test change, or modification to another platform.

### CODE CHANGE

Changed only `Source/InterProcess/AsyncSocket/AsyncSocket.Linux.cpp`: `ConnectionState::owner` now has type `IAsyncSocketConnection*` instead of `AsyncSocketConnection*`. No shared HTTP code, test code, public header, other platform implementation, or generated Linux build artifact changed.

### CONFIRMED

The change removes the incomplete-class conversion at `IAsyncSocketCallback::OnInstalled`, and both incremental and clean full Linux builds compile and link successfully through `.github/Ubuntu/build.sh`.

Runtime verification from `Test/Linux` passes:

- `TestInterProcess_AsyncSocket.cpp`: 1/1 file and 5/5 cases;
- `TestInterProcess_HttpRequest.cpp`: 1/1 file and 29/29 cases;
- `TestInterProcess.cpp`: 1/1 file and 3/3 cases, including the async-socket NetworkProtocol and Channel regressions;
- complete unfiltered suite after a clean full rebuild: 12/12 files and 147/147 cases.

Review confirms the interface pointer is sufficient for every use and matches the macOS ownership representation. The new common HTTP sources, Linux platform guards and binding, and generated source registration compile without further changes. The proposal fixes the only reproduced Linux break while preserving the already-passing cross-platform implementation and test code.
