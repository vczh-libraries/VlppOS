# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

Main goal of this task is to make an implementation of default `IFileSystemImpl` for `VCZH_WASM` based on `OPFS`.
- `FileSystem.Wasm.cpp` with class `OpfsFileSystemImpl` with `GetOSFileSystemImpl` exposed, this function will be called automatically when the file system is first needed.
- When opening a readable file via `GetFileSystemImpl`, you are going to read the whole file and store it into a `MemoryStream` internally, serving `IFileStreamImpl` operations. When `Close` is called and the file is writable, the whole content of `MemoryStream` will be write back to `OPFS`.
- Although the coding convention is limiting the usage of embedded JavaScript in `EM_JS`, but this implementation is not app specific, you can call any JavaScript code to access OPFS in `EM_JS`, keep them short, and do not use the C++ side of OPFS integration.

In order to verify `OpfsFileSystemImpl`, we are going to update web assembly enabled unit test projects in:
- `Vlpp/Test/Linux`: Rewrite `vbuild` to an empty JSON configuration: `{"WASM=YES": {}}`
- `VlppOS/Test/Linux/UnitTest`: enable following test files with web assembly:
  - `TestFileSystem.cpp`
  - `TestLocale.cpp`
  - `TestStream.cpp`
  - `TestStreamLzw.cpp`
- `VlppRegex/Test/Linux`: enable following test files with web assembly:
  - `TestAutomation.cpp`
- `VlppReflection/Test/Linux/UnitTest`: enable following test files with web assembly:
  - `TestReflection_Builder.cpp`.

The `vbuild` file is currently having a "WASM=YES" stream, and `vbuild -bw|-fbw` will test this sub string to see if a test project could be built with web assembly. Now the rule is changed:
- We are not going to test `WASM=YES`, instead we should test `"WASM=YES"`
- Upon calling `app.sh`, it should give the `vbuild` configuration file to the `app.sh`.
  - Currently all code is copied from `*/Ubuntu/vl/wasm-unittest.*`.
  - I think renaming is a little wired, we should have files sitting in `*/Ubuntu/vl/wasm-unittest/` folder with no renaming.
  - Then we could put `wasm-unittest.html`->`app.html` and `wasm-unittest.sh`->`app.sh` to this file, update `wasm.sh` to fix the file name.
  - We are going to add a `app.js`, currently `app.sh` starts node directly to serve files, we should instead let `app.sh` starts `node` with `app.js`, and `app.js` will start an http service, as well as reading `vbuild` configurations to serve files to `app.html`.
  - Before `app.html` running the unit test, it should ask the http server for the file list:
    - If `OPFS` already stores something for this website, clean everything.
    - GET `/OPFS` -> a list of file names, empty folder names (non-empty folder is not included as they can be inferred by file names), or in a tree data structure, you make your decision according to the convenience of calling OPFS to prefill files, this request returns JSON.
    - GET `/OPFS/path/to/the/file` -> `app.js` will read the file from disk and the request gets the binary content, `app.html` and then stores the file to `OPFS`.
    - Now `OPFS` has prefilled files, we can run unit test and the unit test has access to file system.
    - When a file is changed, we don't need to write it back to the disk.
  - Fix everything in `Tools` and release the ubuntu tool to all mentioned 4 repos.

The format of `vbuild` looks like this:
```JSON
{
    "WASM=YES": {
        "rootFolder": "relative/path/to/the/folder/containing/this/file",
        "folders": [
            "list/of/empty/folder/to/create",
            "these/are/not/patterns"
        ],
        "includes": [
            "folder/**/*.txt",
            ...
        ],
        "excludes": [
            "folder/**/IDoNotLikeThese/*.txt",
            ...
        ]
    }
}
```

All fields are optional, when `rootFolder` absents, the rest of 3 are ignored. When `includes` absents, no files are pre-loaded (because excludes anything from an empty set is still empty).

Although native apps have the concept of pwd, but since OPFS is owned by the app, so the pwd in web assembly will always report the root aka "/".
`rootFolder` here means the place being mapped to "/", first match all files with includes (combine all result together from each array item but no duplication), and then excludes those patterns from includes, the rest of the files will be copied into `OPFS`.

`Vlpp/Test/Linux` does not read file, so it maps nothing.

For `VlppOS/Test/Linux/UnitTest`, here is an example. In `main.cpp` we will see
```C++
WString GetTestOutputPath()
{
	return L"../../Output/";
}
```
And scan all cpp files in the list above we could know it only create files in the `Output` folder, so the `vbuild` looks like:
```JSON
{
    "WASM=YES": {
        "rootFolder": "../../"
    }
}
```
So `OPFS` is empty from the beginning. The `Output` folder will be created in the unit test.

And `main.cpp` becomes
```C++
#if defined VCZH_GCC
WString GetTestOutputPath()
{
	return L"../../Output/";
}
#elif defined VCZH_WASM
WString GetTestOutputPath()
{
	return L"/Output/";
}
#endif
```

For `VlppRegex` and `VlppReflection`, you are going to figure out the minimum list to mirror, and the simplest way to write file patterns in `vbuild`. I will verify later if the list of loaded files are actually minimum or not.

Update any document in `Tools` repo saying about web assembly file system, now you have the default `OPFS` implementation.

# TEST [CONFIRMED]

Firefox reproduced `vl::filesystem::GetFileSystemInjection()#File system access is not supported in WebAssembly.` in the OPFS root test, followed by exactly one `wasm_main returns 1.` line and no browser errors. The reproduction compiled successfully using the repository build wrapper.

Enable the requested Wasm test files and run them against the existing implementation to reproduce missing filesystem support. Add coverage for OPFS paths, buffered read/write/close, file and folder operations, and UTF-16 names. Verify browser fixture prefill, minimum input lists, fresh OPFS state on reload, binary fidelity, and read-only host access. Run all four Wasm suites and native regression suites. Regenerate and verify downstream releases and Ubuntu tool copies.

# PROPOSALS

- No.1 Supply an OPFS backend and preload fixtures through the Wasm launcher

## No.1 Supply an OPFS backend and preload fixtures through the Wasm launcher

Implement `OpfsFileSystemImpl` in `Source/FileSystem.Wasm.cpp`, selected by the existing injection chain. Keep root-relative POSIX paths in C++ and use small `EM_ASYNC_JS` adapters to await native OPFS JavaScript APIs. Link with Asyncify and await the Embind entry in the worker, retaining synchronous C++ operations and real pthread tests. The Emscripten [Asyncify documentation](https://emscripten.org/docs/porting/asyncify.html) and the installed SDK's Embind adapter support this boundary.

Each file stream loads readable content into `stream::MemoryStream`; writable close replaces the entire OPFS file. ReadWrite retains existing content as requested, while WriteOnly starts with an empty buffer. File and directory operations report ordinary I/O failures, and failed close raises a C++ error. Directory rename uses copy/delete because portable OPFS directory handles do not provide native rename; reject root, existing destinations and descendant moves before mutation.

Move the canonical launcher into `Tools/Ubuntu/vl/wasm-unittest/app.{html,sh,js}`. Pass the project vbuild path into app.sh, read its JSON in Node, and expose a sorted manifest plus binary GET endpoints for only the included files. Union includes, subtract excludes, infer parent folders, and keep only explicitly requested leaf empty directories. Clear OPFS and populate it before starting the tests. Changes remain in OPFS. Use Node's built-in glob API rather than introduce a custom pattern language.

The existing Regex file is `TestAutomaton.cpp` (the request's `TestAutomation.cpp` is a typo). Its 34 comparisons read exactly `Resources/Baseline/*.txt`; no other inputs are needed. Reflection's builder only writes `Metadata/ReflectionWithTestTypes32.txt`, so preload no files and create `Metadata`. Vlpp maps nothing; VlppOS begins empty and its tests create `/Output`.

### CODE CHANGE

Add/register the backend; remove the Wasm injection failure; enable the requested tests and adapt their paths; add buffered-stream and OPFS operation regressions. Update canonical packaging, launcher, quoted opt-in checks and documentation, verify them, commit/push Tools, and propagate through `vgo uci` to the four libraries. Regenerate releases and synchronize imports in dependency order. Verify all four full Wasm suites and native regressions.
