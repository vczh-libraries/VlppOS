# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

The last three commits are a new task executed and verified on Windows and Ubuntu. You need to run the unit test and make sure it works on macOS. commit and push all local changes.

# UPDATES

# TEST [CONFIRMED]

Use the repository's macOS workflow from `Test/Linux`: perform a clean Debug x64 build with `.github/Ubuntu/build.sh -f`, then run the generated `Bin/UnitTest` asynchronously with `/C`.

The last three commits introduce the cross-platform Mini HTTP API, expand HTTP request/server timeout and shutdown behavior, and replace elapsed wall-clock deadlines with a monotonic clock after Ubuntu stress testing. First run the three focused async-socket and HTTP files (`TestInterProcess_AsyncSocket.cpp`, `TestInterProcess_AsyncSocket_MiniHttpApi.cpp`, and `TestInterProcess_HttpRequest.cpp`) so macOS compilation, native socket behavior, timeout coordination, and shutdown failures are isolated. Then run the complete unfiltered suite to detect regressions in other VlppOS behavior.

Success requires the clean build to compile and link every common and macOS source, the focused files to finish with all cases passing, and the complete suite to finish with every test file and case passing. No test may hang. Regenerating the tracked `Test/Linux/vmake.txt` and `Test/Linux/makefile` on macOS must not introduce host-specific differences.

The clean native arm64 build succeeds with Apple Clang, including the common HTTP sources and macOS Network.framework backend. Regeneration leaves `Test/Linux/vmake.txt` and `Test/Linux/makefile` unchanged. The focused run passes 3/3 files and 54/54 cases, and the complete run passes 13/13 files and 167/167 cases without hanging.

Because the preceding Ubuntu deadline defect was timing-dependent, the Mini HTTP file was also run 20 consecutive times with `/C`. Every run passes 1/1 file and 13/13 cases.

# PROPOSALS

- No.1 Retain the implementation after macOS validation [CONFIRMED]

## No.1 Retain the implementation after macOS validation

The last three commits already route Mini HTTP through `macos_socket::AsyncSocketServer`, register the shared cross-platform tests on macOS, use `std::chrono::steady_clock` for concurrent duration deadlines, and coordinate Network.framework listener startup and shutdown. Review the macOS factory binding, test guards, common source dependencies, generated build metadata, and framework link policy. Keep the implementation unchanged if the clean, focused, complete, and repeated runs all pass.

### CODE CHANGE

- No product, test, or generated-build change is required. Archived the preceding Ubuntu investigation and recorded the macOS validation in the current investigation document.

### CONFIRMED

The source review finds no unguarded Windows or Linux native API in the common Mini HTTP implementation. The product factory selects the macOS async-socket backend under `VCZH_GCC && VCZH_APPLE`, the same shared Mini HTTP suite is registered on macOS, and the generated build selects the required Blocks, CoreFoundation, and Network.framework options without host-specific tracked output.

The clean build, focused 54/54 run, complete 167/167 run, and 20 repeated Mini HTTP runs all pass. These checks exercise native listener startup, occupied-port retry, persistent longest-prefix routing, malformed-wire responses, deferred and callback-reentrant responses, cancellation, hard shutdown draining, and response deadlines. No macOS portability fix is necessary.
