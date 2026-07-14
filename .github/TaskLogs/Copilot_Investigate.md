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
