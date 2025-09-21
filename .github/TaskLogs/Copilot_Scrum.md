# !!!SCRUM!!!

# DESIGN REQUEST

In Vlpp I prepared FeatureInjection and it is used in DateTime/InjectDateTimeImpl/EjectDateTimeImpl.

Please follow the pattern, and checkout related topics in the knowledge base, and fix InjectLocaleImpl and InjectFileSystemImpl, to follow the standard pattern using FeatureInjection

## UPDATE

I think you can just reorganize everything to two steps: fix InjectLocaleImpl, and fix InjectFileSystemImpl.

## UPDATE

I think no test plan is needed, just mention that current unit test is good enough. By following the standard pattern, inject and eject functions is not possible to fail

## UPDATE

There must already be functions that serve the same purpose that `GetDefaultLocaleImpl` and `GetDefaultFileSystemImpl` is going to do. Find them out, I think these two new functions may be not needed.

## UPDATE

Since there is already a standard way to access "the previous implementation", so we should just delete `GetDefaultLocaleImpl`. What do you think?

## UPDATE

Check out existing knowledge base for InjectLocaleImpl and InjectFileSystemImpl, rethink the `Impact to the Knowledge Base` section.

# TASKS

- [x] TASK No.1: Fix InjectLocaleImpl to follow FeatureInjection pattern
- [ ] TASK No.2: Fix InjectFileSystemImpl to follow FeatureInjection pattern

## TASK No.1: Fix InjectLocaleImpl to follow FeatureInjection pattern

The current locale injection system uses a legacy manual pointer-based approach instead of the proper FeatureInjection pattern. This task converts the entire locale injection system to follow the same pattern as DateTime, providing proper LIFO injection ordering, lifecycle management, and delegation capabilities.

### what to be done

- Update `ILocaleImpl` in `Source/Locale.h` to inherit from `IFeatureImpl` instead of just `Interface`
- Update platform-specific locale implementations (Windows/Linux) and `DefaultLocaleImpl` to inherit from `FeatureImpl<ILocaleImpl>`
- Replace the global `localeImpl` pointer in `Source/Locale.cpp` with proper `FeatureInjection<ILocaleImpl>` manager
- Add `GetLocaleInjection()` function following the DateTime pattern using existing `GetOSLocaleImpl()` function
- Delete the existing `GetDefaultLocaleImpl()` function since the FeatureInjection pattern provides standard access to previous implementations
- Update `GetLocaleImpl()` to use `GetLocaleInjection().Get()` instead of manual pointer checking
- Update `InjectLocaleImpl()` to use `GetLocaleInjection().Inject(impl)` instead of direct assignment
- Add `EjectLocaleImpl(ILocaleImpl* impl)` function declaration to `Source/Locale.h` and implementation following DateTime pattern
- Support both specific implementation ejection and complete cleanup (when `impl` is `nullptr`)
- Update all references to `GetDefaultLocaleImpl()` to use the appropriate FeatureInjection mechanism

### how to test it

Current unit tests are sufficient. By following the standard FeatureInjection pattern, inject and eject functions are not possible to fail. The pattern is well-established and tested in the DateTime implementation.

### rationale

The current manual injection approach in locale is inconsistent with the FeatureInjection pattern established for DateTime. According to `KB_Vlpp_Design_ImplementingInjectableFeature.md`, all injectable features should use the same pattern for consistency and to provide advanced capabilities like LIFO injection, proper lifecycle management, and delegation support. This consolidation addresses all aspects of the locale injection system in one comprehensive change, ensuring the project remains compilable and testable at each step.

## TASK No.2: Fix InjectFileSystemImpl to follow FeatureInjection pattern

Similar to the locale system, the current file system injection uses a legacy manual pointer-based approach. This task converts the entire file system injection system to follow the FeatureInjection pattern, providing consistency across all OS abstraction features.

### what to be done

- Update `IFileSystemImpl` in `Source/FileSystem.h` to inherit from `IFeatureImpl` instead of just `Interface`
- Update platform-specific file system implementations (Windows/Linux) to inherit from `FeatureImpl<IFileSystemImpl>`
- Replace the global `injectedFileSystemImpl` pointer in `Source/FileSystem.Injectable.cpp` with proper `FeatureInjection<IFileSystemImpl>` manager
- Add `GetFileSystemInjection()` function following the established pattern using existing `GetOSFileSystemImpl()` function
- Update `GetFileSystemImpl()` to use `GetFileSystemInjection().Get()` instead of manual pointer checking
- Update `InjectFileSystemImpl()` to use `GetFileSystemInjection().Inject(impl)` instead of direct assignment
- Add `EjectFileSystemImpl(IFileSystemImpl* impl)` function declaration to `Source/FileSystem.h` and implementation in `Source/FileSystem.Injectable.cpp`
- Support both specific implementation ejection and complete cleanup (when `impl` is `nullptr`)
- Ensure proper lifecycle method calls during ejection

### how to test it

Current unit tests are sufficient. By following the standard FeatureInjection pattern, inject and eject functions are not possible to fail. The pattern is well-established and tested in the DateTime implementation.

### rationale

Consistency across all OS abstraction features is crucial for maintainability and developer experience. The DateTime and updated Locale systems both use FeatureInjection, and FileSystem should follow the same pattern. This enables advanced scenarios like testing with mock file systems, implementing layered file system behaviors through delegation, and proper cleanup in unit tests. The consolidation ensures that all necessary changes to the file system injection are made together, maintaining project stability throughout the implementation.

# Impact to the Knowledge Base

## VlppOS

### LocaleSupport
The `KB_VlppOS_LocaleSupport.md` document already contains comprehensive documentation about locale injection in the "Implementation Injection" section. This section needs to be updated to reflect the new FeatureInjection pattern:
- Update the injection mechanism description to mention FeatureInjection instead of simple pointer replacement
- Add information about `EjectLocaleImpl` function for proper cleanup
- Update the threading safety considerations for the new pattern
- Remove references to `GetDefaultLocaleImpl()` since it will be deleted
- Add information about LIFO injection ordering and delegation capabilities

### FileSystemOperations
The `KB_VlppOS_FileSystemOperations.md` document already contains comprehensive documentation about file system injection in the "Implementation Injection" section. This section needs to be updated to reflect the new FeatureInjection pattern:
- Update the injection mechanism description to mention FeatureInjection instead of simple pointer replacement
- Add information about `EjectFileSystemImpl` function for proper cleanup
- Add information about LIFO injection ordering and delegation capabilities
- Update testing scenarios to mention the enhanced capabilities of the new pattern

### No New Knowledge Base Topics Needed
After reviewing the existing knowledge base, both locale and file system injection are already well-documented. The changes only require updates to existing content rather than new topics. The FeatureInjection pattern itself is already documented in `KB_Vlpp_Design_ImplementingInjectableFeature.md`, so users can reference that for the underlying implementation details.

# !!!FINISHED!!!
