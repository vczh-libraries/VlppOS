# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

Take a look at [TestInterProcess.cpp](Test/Source/TestInterProcess.cpp) , the test case is organized in cross-platform way while the entry create different set of classes per different platforms. This make multiple implementations share the same set of test cases, ensuring feature consistency.

Now take a look at [TestInterProcess_AsyncSocket.cpp](Test/Source/TestInterProcess_AsyncSocket.cpp) , it is organized in a similar way but not quite perfect. Think about when linux and macos implementations added in, there are still a little bit code need to duplicate e.g. 5 test cases. Twist it a little bit, make placeholder for linux and macos like [TestInterProcess.cpp](Test/Source/TestInterProcess.cpp) , minimized platform specific code. By the way, the `WindowsTestServer` class seems to be able to convert to a template class by making the base class into template argument, it just requires other platform's socket server are authored in the same constructor shape, which should be definitely doable. I expect only a few lines of code changes as the overall structure looks good.

And take a look at [TODO_Task.md](TODO_Task.md) , you are going to write TODO_Task_Linux.md and TODO_Task_macOS.md for linux/macos implementation, but now since the unit test has already been done, you can skip the testing part, just say add the platform specific implementation to [TestInterProcess_AsyncSocket.cpp](Test/Source/TestInterProcess_AsyncSocket.cpp) and run the test case. Note that some platform specific technical details already in [TODO_SocketHttp_AsyncSocket.md](TODO_SocketHttp_AsyncSocket.md) , and skip `**INetworkProtocol(Server|Client) on IAsyncSocket(Server|Client)`. By the way, the Windows version of socket client/server only take port as the only constructor argument, keep that if possible.**

commit and push to master, rebase if conflict.

# UPDATES

# TEST [CONFIRMED]

The existing `TestInterProcess_AsyncSocket.cpp` runners already cover the shared async-socket contract through five scenarios: long full-duplex binary transfer, rejected connection shutdown, `Stop` from `OnRead`, connection retry followed by success, and stopping during retry. Use the current Windows implementation as the baseline binding for these existing scenarios; no new behavioral test case is needed.

The organization problem is confirmed by source inspection when all five `TEST_CASE` registrations are inside the Windows-only preprocessor block. In that shape, adding Linux and macOS bindings would require repeating those registrations or expanding Windows-specific code. The refactored source succeeds structurally when:

1. The five registrations exist only once in platform-neutral code.
2. Windows, macOS, and Linux have explicit include/binding/invocation branches, with unimplemented platforms left as clear placeholders.
3. The shared server adapter is templated over a concrete `IAsyncSocketServer` base whose constructor takes only the port.
4. Each platform branch only supplies its concrete server/client types, maximum read block size, and timed-event binding before invoking the shared registrations.

Build and run the existing async-socket cases after the refactor. The Debug x64 unit-test result must pass all selected test files and cases, and `Execute.log` must contain no CRT memory-leak report. Review `TODO_Task_Linux.md` and `TODO_Task_macOS.md` to confirm that both preserve the common async byte-stream and shutdown contract, incorporate the platform-specific guidance from `TODO_SocketHttp_AsyncSocket.md`, exclude `INetworkProtocol(Server|Client) on IAsyncSocket(Server|Client)`, prefer port-only concrete server/client constructors, and direct the implementer to bind the platform classes in `TestInterProcess_AsyncSocket.cpp` and run the already-authored cases instead of creating duplicate tests.

## Confirmation

The baseline Debug x64 build succeeded with zero warnings and zero errors. The unfiltered unit-test run passed 13/13 files and 122/122 cases, including all five existing async-socket scenarios, and `Execute.log` contains no CRT memory-leak report. Source inspection confirms the organizational defect independently of behavior: the five scenario registrations are all inside `#ifdef VCZH_MSVC`, the Windows factory setup is embedded beside them, and there are no macOS or Linux binding placeholders. Therefore the behavior is already covered while future platform activation would currently require duplicating or restructuring the registration block.

# PROPOSALS

- No.1 Share async-socket test registration and prepare platform implementation tasks [CONFIRMED]

## No.1 Share async-socket test registration and prepare platform implementation tasks

Move the five existing `TEST_CASE` registrations into one platform-neutral templated function. The function takes the concrete server and client types as template arguments, constructs the existing factory delegates, accepts the platform's maximum read-block size and timed-event delegate, and registers all existing scenarios exactly once. Keep the implementation entry inside `TEST_FILE`, where each supported platform invokes that shared function once. Add explicit empty macOS and Linux branches beside the existing Windows include and test entry so future implementations only fill their own binding branch.

Replace the Windows-only server test adapter with `TestServer<TServerBase>`. Its constructor forwards only `vint port` to `TServerBase` and stores the shared accept handler; generic server/client factory templates construct `TServerBase(port)` and `TClient(port)`. This deliberately makes the port-only constructor shape and overridable server accept hook a compile-time requirement for every platform implementation while leaving the maximum receive block size platform-specific.

Create `TODO_Task_Linux.md` and `TODO_Task_macOS.md` as standalone future `investigate repro` tasks modeled after `TODO_Task.md`. Both documents preserve the existing common interface and callback/shutdown contract, scope work to one platform implementation and its build integration, explicitly exclude `INetworkProtocol(Server|Client) on IAsyncSocket(Server|Client)`, expose concrete server/client classes with port-only constructors where possible, and incorporate the corresponding `liburing`/`io_uring` or Network.framework/Grand Central Dispatch requirements from `TODO_SocketHttp_AsyncSocket.md`. They do not request new test scenarios; instead they direct the implementer to fill the prepared platform include/invocation branch in `TestInterProcess_AsyncSocket.cpp` and run the shared cases.

### CODE CHANGE

Updated `Test/Source/TestInterProcess_AsyncSocket.cpp` with explicit Windows, macOS, and Linux branches at the platform include, timed-wait binding, and `TEST_FILE` entry points. The unimplemented GCC branches are empty placeholders and compile without instantiating unavailable concrete types.

Replaced `WindowsTestServer`, `CreateWindowsServer`, and `CreateWindowsClient` with the platform-neutral `TestServer<TServerBase>`, `CreateTestServer<TServerBase>`, and `CreateTestClient<TClient>` templates. `TestServer<TServerBase>` forwards `vint port` to its concrete base and overrides only the shared accept callback. Added `RunAsyncSocketTestCases<TServerBase, TClient>`, which creates the factory delegates and contains the five existing test registrations once. The Windows entry now supplies `windows_socket::AsyncSocketServer`, `windows_socket::AsyncSocketClient`, its 64 KiB receive maximum, and its bounded event-wait function in one invocation.

Created `TODO_Task_Linux.md` and `TODO_Task_macOS.md`. Each standalone future task specifies the port-only public constructors, private native implementation shape, common byte-stream/callback/shutdown contract, platform-native completion and cancellation rules, project/vmake integration, and activation of the prepared shared test entry without authoring duplicate scenarios. Both explicitly exclude the `INetworkProtocol*` adapter section. Review identified that `EventObject::WaitForTime` is currently Windows-only, so both tasks now include the same order-independent GCC prerequisite: add timed `ConditionVariable`/`EventObject` waiting once in `Threading.Linux.cpp` using `pthread_cond_timedwait`, then bind that bounded wait through the existing test seam. The Linux task also accounts for `liburing` link order, and the macOS task preserves the authoritative distinction between retryable `waiting` and terminal `failed` Network.framework states.

### CONFIRMED

The refactored Debug x64 solution built with zero warnings and zero errors. The unfiltered post-change unit-test run registered all five async-socket cases exactly once and passed 13/13 files and 122/122 cases. `Execute.log` ends at the passing summary and contains no CRT memory-leak report.

Source review confirms that all five `TEST_CASE` bodies now occur only inside the platform-neutral `RunAsyncSocketTestCases` template, while each platform has a separate include/binding/invocation branch. The common test server and factories enforce the requested port-only constructor shape at template instantiation. Independent C++ review found no lifetime issue: the unit-test framework executes each registered case synchronously while the helper's factory and timed-wait delegates remain alive.

Independent Linux and macOS document reviews confirmed the final scopes, constructor contracts, native lifecycle guidance, build selection, adapter exclusion, and shared-test reuse. Their only initial findings—the missing GCC bounded event wait, an early as-needed `liburing` link, and ambiguous macOS `failed` handling—were corrected and re-reviewed with no remaining concrete issues.
