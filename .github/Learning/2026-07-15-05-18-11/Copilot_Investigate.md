# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

[AsyncSocket.h](Source/InterProcess/AsyncSocket/AsyncSocket.h) has some classes that are not template. I would like you to create a AsyncSocket.cpp in the same folder, storing all method bodies of them. Leave template classes untouched. You need to check out some other cpp files to find out how I would like to use block comment to separate different classes in cpp files. By the way, AsyncSocket.(Windows|Linux|macOS).cpp do not have these block comment separators, you need to also add them. A block comment not only separate methods, but also separate a complete class definition if it is completely in a cpp files. Things belong to the same class should stay grouped together. If there are any forward declarations, variables, structs or anything that is not a class/method, move them to the top of the namespace. commit and push all local changes.

# UPDATES

# TEST [CONFIRMED]

Confirm the requested source-layout problem with a structural audit, then verify the refactor with the existing async-socket coverage and the complete unit-test project.

The problem is confirmed. `Source/InterProcess/AsyncSocket/AsyncSocket.cpp` does not exist. `AsyncSocket.h` contains method bodies for the non-template `IAsyncSocketCallback`, `NetworkProtocolCallbackDomain`, `NetworkProtocolConnectionLifecycle`, and `NetworkProtocolConnection` classes, while its two template adapter classes must remain header-defined. `AsyncSocket.Windows.cpp`, `AsyncSocket.Linux.cpp`, and `AsyncSocket.macOS.cpp` contain multiple complete implementation classes and class-method groups without the repository's `/*********************************************************************** ... ***********************************************************************/` class separators.

Acceptance requires:

- Add `AsyncSocket.cpp` to the same folder and to all owning MSBuild/filter metadata so Windows and generated Linux builds compile it.
- Move every method body belonging to a non-template class out of `AsyncSocket.h` and into class-grouped sections in `AsyncSocket.cpp`; keep `NetworkProtocolServer<TAsyncSocketServer>` and `NetworkProtocolClient<TAsyncSocketClient>` unchanged.
- Keep namespace-level forward declarations, variables, structs, and free functions before the first class separator in each affected `.cpp`.
- Add one class separator for every complete implementation class or class method group in the common and Windows/Linux/macOS `.cpp` files, preserving contiguous ownership groups as dependencies allow.
- Build `Test/UnitTest/UnitTest.sln`, run the complete configured unit-test suite, and confirm `TestInterProcess_AsyncSocket.cpp` is selected. The final Debug log must have no memory-leak report.
- Confirm the authoritative project source list selects the new common translation unit for Linux and that `Test/Linux/vmake` does not remove it. Run the Linux build wrapper when a Linux distribution is available; do not hand-edit its generated source list.

# PROPOSALS

- No.1 Split non-template async-socket implementation and normalize class sections [CONFIRMED]

## No.1 Split non-template async-socket implementation and normalize class sections

Turn the non-template classes in `AsyncSocket.h` into declarations while keeping their data layout, nested type declarations, and public signatures intact. Move the private templated protocol-callback dispatcher definition too: all of its instantiation sites move into `AsyncSocket.cpp`, so the template remains valid without changing its signature or type-erasure behavior. Do not change either template adapter class.

Define namespace-scope static members and all moved methods in the new common translation unit. Organize that file and all platform translation units with the repository's class-name block separators. Put namespace-level declarations, callback-frame structs, thread-local variables, and free error helpers before the first separator, and reorder implementation class groups only where needed to keep one class's definitions and methods contiguous without changing behavior.

### CODE CHANGE

Added `Source/InterProcess/AsyncSocket/AsyncSocket.cpp` and moved all method bodies for `IAsyncSocketCallback`, `NetworkProtocolCallbackDomain`, `NetworkProtocolConnectionLifecycle`, and `NetworkProtocolConnection` into it. The new translation unit also owns the three thread-local static-member definitions and the private `InvokeProtocolCallback<TCallback>` definition; every instantiation remains in that translation unit. `AsyncSocket.h` now contains declarations for those non-template classes, while the complete `NetworkProtocolServer<TAsyncSocketServer>` and `NetworkProtocolClient<TAsyncSocketClient>` template regions are unchanged.

Registered the new common source once in `UnitTest.vcxproj` and its `Common\InterProcess\AsyncSocket` filter. The Linux project generator reads that project entry and removes only `AsyncSocket.Windows.cpp`, so it selects the new common source without hand-editing generated `vmake.txt` or `makefile` files. `Release/CodegenConfig.xml` discovers common `Source` files automatically, so no generated Release source needed modification.

Organized `AsyncSocket.cpp`, `AsyncSocket.Windows.cpp`, `AsyncSocket.Linux.cpp`, and `AsyncSocket.macOS.cpp` with the repository's 72-character class block separators. Namespace-level forward declarations, helper structs, thread-local variables, and free helper functions precede the first class section. Complete implementation classes and their out-of-class methods are grouped together, with only dependency-safe code movement and no behavioral changes.

### CONFIRMED

Independent structural reviews compared all 35 moved non-template method bodies and four constructor initializer lists with the original header and found them equivalent. The two header-defined template adapter classes are byte-identical to their original regions. Platform code-line audits likewise found the Windows, Linux, and macOS implementation changes limited to separators and dependency-safe reordering. All separators match the existing format, project metadata contains one common-source entry, and `git diff --check` passes.

The prescribed Debug x64 build of `Test/UnitTest/UnitTest.sln` succeeded with zero warnings and zero errors and explicitly compiled `AsyncSocket.cpp`. The complete configured unit-test run selected `TestInterProcess_AsyncSocket.cpp`, passed 13/13 test files and 125/125 test cases, including all five async-socket scenarios, and appended no Debug memory-leak report. A Linux distribution is not installed in the available WSL environment, so the Linux wrapper could not be run; its authoritative project-selection path was inspected instead and confirms that the new common translation unit is included while the Windows translation unit is intentionally excluded.
