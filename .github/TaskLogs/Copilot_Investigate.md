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

# UPDATES

## UPDATE

a little fix
- `#if defined VCZH_MSVC || defined VCZH_GCC || defined VCZH_WASM` code like that could just be removed because it means every platform

And follow the original request to enable these unit test projects:
- `VlppParser2/Test/Linux/ParserTest_ParserGen_Generated`
- `Workflow/Test/Linux/(LibraryTest|RuntimeTest|CppTest|CppTest_Metaonly|CppTest_Reflection)`
- `GacUI/Test/Linux/UnitTest`

By the way, in `GacUI/Test/Linux`, the following test projects are not needed anymore:
- `CppTest`
- `CppTest_Metaonly`
- `CppTest_Reflection`
Make sure the `wGac` and `iGac` repo (no need to update them) really don't depend on these 3 test projects, and if yes, remove them.

Follow `Project.md` in the previous 4 repos to update the new 3 repos to mention which projects work with web assembly.

# TEST [CONFIRMED]

Firefox reproduced `vl::filesystem::GetFileSystemInjection()#File system access is not supported in WebAssembly.` in the OPFS root test, followed by exactly one `wasm_main returns 1.` line and no browser errors. The reproduction compiled successfully using the repository build wrapper.

Enable the requested Wasm test files and run them against the existing implementation to reproduce missing filesystem support. Add coverage for OPFS paths, buffered read/write/close, file and folder operations, and UTF-16 names. Verify browser fixture prefill, minimum input lists, fresh OPFS state on reload, binary fidelity, and read-only host access. Run all four Wasm suites and native regression suites. Regenerate and verify downstream releases and Ubuntu tool copies.

# PROPOSALS

- No.1 Supply an OPFS backend and preload fixtures through the Wasm launcher [CONFIRMED]
- No.2 Extend browser verification to Parser2, Workflow and GacUI

## No.1 Supply an OPFS backend and preload fixtures through the Wasm launcher

Implement `OpfsFileSystemImpl` in `Source/FileSystem.Wasm.cpp`, selected by the existing injection chain. Keep root-relative POSIX paths in C++ and use small `EM_ASYNC_JS` adapters to await native OPFS JavaScript APIs. Link with Asyncify and await the Embind entry in the worker, retaining synchronous C++ operations and real pthread tests. The Emscripten [Asyncify documentation](https://emscripten.org/docs/porting/asyncify.html) and the installed SDK's Embind adapter support this boundary.

Each file stream loads readable content into `stream::MemoryStream`; writable close replaces the entire OPFS file. ReadWrite retains existing content as requested, while WriteOnly starts with an empty buffer. File and directory operations report ordinary I/O failures, and failed close raises a C++ error. Directory rename uses copy/delete because portable OPFS directory handles do not provide native rename; reject root, existing destinations and descendant moves before mutation.

Move the canonical launcher into `Tools/Ubuntu/vl/wasm-unittest/app.{html,sh,js}`. Pass the project vbuild path into app.sh, read its JSON in Node, and expose a sorted manifest plus binary GET endpoints for only the included files. Union includes, subtract excludes, infer parent folders, and keep only explicitly requested leaf empty directories. Clear OPFS and populate it before starting the tests. Changes remain in OPFS. Use Node's built-in glob API rather than introduce a custom pattern language.

The existing Regex file is `TestAutomaton.cpp` (the request's `TestAutomation.cpp` is a typo). Its 34 comparisons read exactly `Resources/Baseline/*.txt`; no other inputs are needed. Reflection's builder only writes `Metadata/ReflectionWithTestTypes32.txt`, so preload no files and create `Metadata`. Vlpp maps nothing; VlppOS begins empty and its tests create `/Output`.

### CODE CHANGE

Add/register the backend; remove the Wasm injection failure; enable the requested tests and adapt their paths; add buffered-stream and OPFS operation regressions. Update canonical packaging, launcher, quoted opt-in checks and documentation, verify them, commit/push Tools, and propagate through `vgo uci` to the four libraries. Regenerate releases and synchronize imports in dependency order. Verify all four full Wasm suites and native regressions.

The restored locale test referenced Windows-only transformations; guard only those calls, and retain its portable comparisons/formatting/output on both native Unix and Wasm. Register TestLocale.cpp in the stable Unix inventory. The initial compile also corrected the stream position type to `vl::pos_t`.

The first browser run passed binary snapshot/writeback tests but failed recursive folder deletion. `OpfsGetEntries` compared a JavaScript boolean with a numeric Wasm boolean using strict equality, so both enumeration lists were empty. Convert the imported flag with `!!directory` before comparing; retain the tree-preservation test and the existing enumeration suite as regression coverage.

Restoring TestLocale.cpp reproduced an upstream Unicode corruption: `Vlpp/Source/Strings/String.cpp` narrowed `towlower`/`towupper` results to `char`. The written OPFS log showed the intact original text followed by truncated/non-Unicode case output. Preserve `wchar_t`, add a Vlpp regression for ASCII case changes with unchanged Chinese and supplementary characters, regenerate Vlpp, and import its release into all three consumers. Also exercise OPFS on two concurrent pthread workers with bounded completion waits.

The concurrent check completed both files but hung in `pthread_join`: Emscripten 3.1.6 resumes `EM_ASYNC_JS` promises without its managed callback's thread-exit handling. This matches upstream [issue 17552](https://github.com/emscripten-core/emscripten/issues/17552) and [issue 16940](https://github.com/emscripten-core/emscripten/issues/16940). Complete OPFS calls on pthreads with one `emscripten_sleep(0)` continuation, a public Asyncify API that resumes through the runtime's managed callback. Keep this at the filesystem boundary, so arbitrary pthread callers benefit without changing generic threading or the SDK. Test several pairs of workers to verify joining and worker reuse.

Final runner review found that Node's `**` glob includes the root directory (`.`). Filter glob results to regular files before validating their OPFS names, so broad include patterns work while directory paths remain inferred. The server regression checks union/deduplication, excludes, empty leaves, binary transport, isolation headers and read-only routes.


### CONFIRMED

The default injection now opens OPFS without app-specific hooks. Browser runs used the production generated launcher in isolated Firefox workers with Emscripten 3.1.6. Each suite reported exactly one successful `wasm_main returns 0.` completion, no browser errors and no diagnostic failures. The three VlppOS consumers retained their 32-worker pthread pools.

| Project | Wasm test files / cases | Native Clang test files / cases | Prefilled files / empty folders |
| --- | --- | --- | --- |
| Vlpp | 32 / 473 | 32 / 467 | 0 / 0 |
| VlppOS | 10 / 105 | 15 / 279 | 0 / 0 |
| VlppRegex | 9 / 226 | 9 / 226 | 34 / Output |
| VlppReflection | 9 / 53 | 9 / 53 | 0 / Metadata |

Vlpp also passed 32 files / 467 cases with native GCC. Build commands used each repository's `.github/Ubuntu/build.sh`: full Wasm `-fbw`, incremental Wasm `-bw`, full native Clang `-f`, and Vlpp full GCC `--full-build-gcc`. Native executables ran as `./Bin/UnitTest /C` from their project directories. The upstream Unicode fix was committed and pushed separately as Vlpp `665fef3`.

The OPFS regressions verify 70,000 binary bytes and supplementary Unicode names; whole-file snapshots; retained ReadWrite content; persistence only at close; read-only close; truncation, unavailable streams and access rights; normalization and relative paths; directory enumeration; tree-preserving rename; invalid/root/descendant operations; and four pairs of concurrent workers that finish and join. Existing FileStream, CacheStream, locale output, LZW and threading tests also execute. Regex and Reflection compile the generated VlppOS release, verifying the downstream packaging and filesystem consumers.

Fixture evidence:

- Regex's five pure expressions need five baselines each and its three other expressions need three each: `5 * 5 + 3 * 3 = 34`. The exact set of executed `.txt` comparison names in the browser log equals `Resources/Baseline/*.txt`; there are no unused files. The 34 binary fixture requests equal the manifest.
- Reflection reads no input files. It writes `Metadata/ReflectionWithTestTypes32.txt` in OPFS; the browser verified its nonempty 23,765-byte output.
- A separate production-page prefill check used a Unicode filename containing spaces, `#` and `?`, binary bytes including NUL and 255, and a nested empty folder. Reload removed a stale OPFS entry and restored original fixture bytes after browser-side mutation; the host file stayed unchanged.
- Server checks cover `**`, include unions and deduplication, excludes, inferred parents, explicit empty leaves, binary responses, COOP/COEP isolation, unavailable/unselected routes and rejected PUT requests. Optional mappings without a root and configurations without includes preload no files.
- Bare `WASM=YES` is rejected for both Wasm build modes before cleaning, while the quoted key opts in. JavaScript syntax and shell syntax checks passed.

Canonical Tools changes were committed/pushed before distribution through `vgo uci`. All four Ubuntu tool copies match Tools byte for byte, old flat templates are removed, and the build/launch guidelines use `./Bin/app.sh ./vbuild [port]`. CodePack regenerated releases in dependency order; all affected dependency imports match their owning releases byte for byte. `git diff --check` passed in all five repositories.

Select No.1, the sole confirmed proposal. Source review found no additional changes necessary after the runtime continuation and recursive-glob corrections. Windows and macOS were not run; native Linux and browser Wasm coverage are recorded above. Directory rename is deliberately a non-atomic copy/delete operation, and streams retain complete files in memory, as documented.

Final verification after selecting the confirmed proposal rebuilt VlppOS from clean Wasm objects and reran all 105 cases successfully, including concurrent filesystem workers, with the current canonical launcher package. Its manifest was empty, its default-page symlink and Wasm target copy were correct, and no browser errors or duplicate completion were reported.

## No.2 Extend browser verification to Parser2, Workflow and GacUI

Keep the confirmed OPFS implementation and extend its production launcher to the seven requested projects. Remove redundant guards that cover all supported compiler families, including the equivalent CHECK_ERROR condition in Vlpp, while preserving genuinely platform-specific branches. Regenerate releases and synchronize dependency imports in order.

Reuse the existing Embind entry-point and exception-reporting pattern, retaining each project's initialization and cleanup. Map test paths to the OPFS root and declare only input fixtures and necessary empty directories in each vbuild file. RuntimeTest needs the compiler's generated 32-bit assemblies; GacUI needs its 32-bit compiler baselines and generated skin implementations. Keep the source inventory stable across compilers by selecting architecture-specific generated skin files in small registered Unix translation units, without editing generated C++.

Tracked scripts and CMake files in wGac and iGac reference GacUI's RemotingTest_Core, RemotingTest_RvmHost, CppTest_Rvm and CppTest_Tui sources, but not the three obsolete Linux CppTest configurations or their shared Linux/Main.cpp. Remove only those obsolete configurations and their otherwise unreferenced entry point. Leave wGac and iGac unchanged. Document the browser-capable projects in each of the three Project.md files.

### CODE CHANGE

Distribute the canonical Ubuntu tools to the three repositories. Add seven JSON vbuild configurations, pthread pools, Wasm entry points and root-relative filesystem paths. Refresh dependency imports, register architecture-selecting skin translation units for GacUI's Unix unit test, and remove the obsolete Linux configurations. Verify each requested suite in the browser through the generated launcher, checking one successful completion and no failed assertions/browser errors. Run the required native project sequences and upstream regression suites, including Workflow's compiler fixture generation and RPC stdio verification. Record fixture counts, test counts, dependency audit evidence and any portability fixes discovered before selecting the completed implementation.
