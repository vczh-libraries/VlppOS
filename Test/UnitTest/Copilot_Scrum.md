# !!!SCRUM!!!

# DESIGN REQUEST

I would like to create a `IFileStreamImpl` class to take over FileStream's constructor, and from Close all the way to Peek.

With the interface's help, we can replace `FILE*` to `Ptr<IFileStreamImpl>`. The constructor will become a `Open` function, returning `false` when it fails. In this case, the `file` variable will be reset to nullptr in `FileStream` constructor. The open function has no arguments

When FileStream is created, it will get the IFileStreamImpl from IFileSystemImpl->GetFileStreamImpl(fileName, accessRight);

The IFileStreamImpl will be declared above FileStream, in FileStream.cpp it includes FileSystem.h, no other include is needed

## UPDATE

I proposed my task split. The first task will be implementing a default IFileStreamImpl, which just exactly the same implementation with today's FileStream, and the constructor will `new` that class directly. Let's say `OSFileStreamImpl`. The second task will be updating IFileSystemImpl

## UPDATE

IFileStreamImpl::Open must be no arguments, arguments will be in OSFileStreamImpl's constructor

## UPDATE

The task 1-1 is perfect, do not change it, and it is already done anyway, changing gives nothing good

For task 2-2:
- There is no WindowsFileStreamImpl and LinuxFileStreamImpl, the OSFileStreamImpl is good enough for both platform.
- You should add a `CreateOSFileStreamImpl` function in FileStream.cpp, but do not declare it in FileStream.h, instead declare it in both FileSystem.Linux.cpp and FileSytem.Windows.cpp. So the GetFileStreamImpl function will just call that function. Please remember it should return Ptr<IFileStreamImpl>
- Only add `#include` in FileStream.cpp for FileSystem.h so the `GetFileSystemImpl` is visible to FileStream class.

## Analysis

Based on the examination of the current FileStream implementation and the existing injection patterns for DateTime and FileSystem, I need to implement a similar injection pattern for FileStream. The key insights are:

1. **Current FileStream Architecture**: FileStream currently uses `FILE*` directly and handles platform-specific file operations inline within the class methods.

2. **Injection Pattern**: Following the established pattern used by DateTime and FileSystem, I need to create an interface (`IFileStreamImpl`) and an injection mechanism that allows replacing the underlying implementation.

3. **Integration with FileSystem**: The request specifies that FileStream should obtain its implementation from `IFileSystemImpl->GetFileStreamImpl(fileName, accessRight)`, which means I need to extend the existing FileSystem injection interface.

4. **Constructor Transformation**: The current constructor logic should be moved to an `Open` method in the implementation, with the constructor becoming a thin wrapper that obtains and uses the implementation.

5. **Method Coverage**: All methods from `Close` to `Peek` (essentially all the IStream interface methods plus constructor logic) need to be delegated to the implementation.

6. **Revised Approach**: Based on the update, the implementation will be split into two distinct phases - first creating a standalone default implementation (`OSFileStreamImpl`), then integrating with the FileSystem injection system.

7. **Interface Design Clarification**: The `IFileStreamImpl::Open` method will take no arguments. Instead, the concrete implementation constructor (like `OSFileStreamImpl`) will receive the fileName and accessRight parameters, and the `Open` method will use these stored values to perform the actual file opening operation.

8. **Updated Integration Approach**: The latest update clarifies that the integration should use a single `OSFileStreamImpl` for both platforms, with a `CreateOSFileStreamImpl` function declared in platform-specific FileSystem files but implemented in FileStream.cpp.

## Phase 1: Default Implementation Creation

This phase creates the foundational interface and a default implementation that directly replaces the current `FILE*` usage without FileSystem integration yet.

### Task 1-1: Define IFileStreamImpl Interface and Create OSFileStreamImpl [PROCESSED]

Create the `IFileStreamImpl` interface and implement a default `OSFileStreamImpl` class that contains exactly the same logic as the current FileStream implementation. The FileStream constructor will directly instantiate this default implementation.

**What to be done:**
- Add `IFileStreamImpl` interface declaration in FileStream.h above the FileStream class
- Define all necessary methods in `IFileStreamImpl` that correspond to file operations: `Open` (no arguments), `Close`, `CanRead`, `CanWrite`, `CanSeek`, `CanPeek`, `IsLimited`, `IsAvailable`, `Position`, `Size`, `Seek`, `SeekFromBegin`, `SeekFromEnd`, `Read`, `Write`, `Peek`
- Create `OSFileStreamImpl` class in FileStream.cpp that implements `IFileStreamImpl`
- `OSFileStreamImpl` constructor will take `(const WString& fileName, AccessRight accessRight)` parameters and store them as member variables
- `OSFileStreamImpl::Open()` method (no arguments) will use the stored fileName and accessRight to perform the actual file opening using current FileStream logic
- Move all current `FILE*` based logic from FileStream into `OSFileStreamImpl`
- Replace `FILE* file` member with `Ptr<IFileStreamImpl> impl` in FileStream class
- Modify FileStream constructor to create `OSFileStreamImpl` with parameters: `impl = Ptr(new OSFileStreamImpl(fileName, _accessRight))`
- Transform constructor logic into a call to `impl->Open()` and handle failure cases appropriately
- Update all FileStream methods to delegate to `impl`

**What to test in Unit Test:**
- All existing FileStream unit tests must pass without modification
- Verify that file operations behave identically to the previous implementation
- Test all combinations of access rights (ReadOnly, WriteOnly, ReadWrite)
- Error handling should remain consistent with current behavior
- Test that `Open()` method correctly uses the constructor parameters for file operations

**What to test manually:**
- Test file creation, reading, and writing operations
- Verify seek operations work correctly with different access rights
- Test error conditions (file not found, permission denied, etc.)
- Ensure resource cleanup works properly
- Verify no performance regression compared to direct `FILE*` usage
- Test that failed `Open()` calls result in proper error states

**Reasons for this task:**
- Establishes the injection interface without external dependencies
- Creates a working default implementation that maintains full backward compatibility
- Enables testing the interface design before integrating with FileSystem
- Provides a solid foundation for the subsequent FileSystem integration
- Minimizes risk by keeping changes localized to FileStream initially
- Clarifies the separation between construction parameters and operation methods

**Support evidence:**
- Current FileStream implementation provides all necessary logic to extract into `OSFileStreamImpl`
- DateTime injection pattern shows how to create default implementations
- IStream interface defines all methods that need to be abstracted
- Platform-specific file operations are already well-encapsulated in the current implementation
- The parameterless `Open` design follows the principle of separating object construction from operation

## Phase 2: FileSystem Integration

This phase integrates the FileStream injection system with the existing FileSystem injection mechanism using the revised approach.

### Task 2-1: Extend IFileSystemImpl and Integrate with OSFileStreamImpl [PROCESSED]

Extend the `IFileSystemImpl` interface to include stream creation functionality and integrate with the existing `OSFileStreamImpl` through a factory function approach.

**What to be done:**
- Add `GetFileStreamImpl(const WString& fileName, FileStream::AccessRight accessRight)` method to `IFileSystemImpl` interface in FileSystem.h
- Create `CreateOSFileStreamImpl(const WString& fileName, FileStream::AccessRight accessRight)` function in FileStream.cpp that creates and returns `Ptr<IFileStreamImpl>` using the existing `OSFileStreamImpl`
- Declare `CreateOSFileStreamImpl` function in both FileSystem.Linux.cpp and FileSystem.Windows.cpp (but do not implement it there)
- Add `GetFileStreamImpl` implementation to both `WindowsFileSystemImpl` and `LinuxFileSystemImpl` classes that call `CreateOSFileStreamImpl`
- Add `#include "FileSystem.h"` in FileStream.cpp to make `GetFileSystemImpl()` visible
- Modify FileStream constructor to call `GetFileSystemImpl()->GetFileStreamImpl(fileName, accessRight)` instead of creating `OSFileStreamImpl` directly
- Ensure the same `OSFileStreamImpl` logic works for both Windows and Linux platforms
- Maintain exact same functionality and behavior as the current implementation

**What to test in Unit Test:**
- All existing FileStream unit tests must continue to pass without modification
- Test that FileSystem injection affects FileStream creation correctly
- Verify error handling when FileSystem implementation is null or fails
- Test integration between FileSystem and FileStream injection systems
- Verify that `GetFileStreamImpl` returns properly constructed implementations
- Test that the factory function approach works correctly with the injection system

**What to test manually:**
- Verify that FileStream behavior remains identical after FileSystem integration
- Test that custom FileSystem implementations can provide custom FileStream implementations through the factory function
- Ensure no regression in file operations across different platforms
- Test injection scenarios with mock FileSystem implementations
- Verify that the `CreateOSFileStreamImpl` function works correctly when called from platform-specific implementations
- Test cross-platform compatibility with the unified `OSFileStreamImpl` approach

**Reasons for this task:**
- Completes the integration with the existing FileSystem injection pattern
- Enables unified dependency injection for both file system and stream operations
- Uses a factory function approach that maintains clean separation between components
- Provides the complete injection infrastructure as originally requested
- Allows for comprehensive testing and customization of file operations
- Maintains platform compatibility through a single implementation
- Follows the updated requirements for simpler platform integration

**Support evidence:**
- Existing FileSystem injection pattern provides the exact model to follow
- DateTime injection integration demonstrates how to extend existing injection interfaces
- Knowledge base shows the established patterns for cross-platform file operations
- The factory function approach is consistent with modular design principles
- The single `OSFileStreamImpl` approach simplifies maintenance while preserving platform compatibility
- Updated requirements provide clear guidance on the preferred integration method

# !!!FINISHED!!!
