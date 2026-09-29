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

## UPDATE

if you add anything to the code to patch the size, I would like you to `#ifdef VCZH_WASM` so that they don't affect other platforms.

## UPDATE

The OS has died, I restarted it, so if anything is under debugging you have to restart the process again.

## UPDATE

I would like you to commit everything first and you continue to do the test

# TEST [CONFIRMED]

Firefox reproduced `vl::filesystem::GetFileSystemInjection()#File system access is not supported in WebAssembly.` in the OPFS root test, followed by exactly one `wasm_main returns 1.` line and no browser errors. The reproduction compiled successfully using the repository build wrapper.

Enable the requested Wasm test files and run them against the existing implementation to reproduce missing filesystem support. Add coverage for OPFS paths, buffered read/write/close, file and folder operations, and UTF-16 names. Verify browser fixture prefill, minimum input lists, fresh OPFS state on reload, binary fidelity, and read-only host access. Run all four Wasm suites and native regression suites. Regenerate and verify downstream releases and Ubuntu tool copies.

For the continuation, run the requested Parser2 generated-parser suite, all five requested Workflow suites, and GacUI UnitTest through their generated browser launchers. Require successful C++ completion, no browser errors, and fixture manifests limited to files actually used. Verify native regression suites and generated outputs, audit wGac/iGac references before deleting the three obsolete GacUI Linux configurations, and compare generated dependency imports and distributed Ubuntu tools with their owners. The equal-size reflection regression must cover construction before registration, registered derived types and unregistered sibling interfaces; all of its implementation and test additions must be guarded by `#ifdef VCZH_WASM`.

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

The initial GacUI Wasm compilation found that VlppOS's shared coordinate and input declarations in TUITypes.h were restricted to native platforms. These declarations contain no terminal operations and are also GacUI's common public types. GacUI's TUI provider tests already inject an in-memory backend and also need the portable buffer/event-loop core. Expose TUITypes.h, TUI.h, TUI.Internal.h and TUI.cpp on all platforms. Keep the OS terminal backends native-only; the Wasm default backend factory fails explicitly when no backend is injected. Add a regression for that boundary, regenerate VlppOS and refresh consumers. GacUI's existing provider suite exercises its injected backend in the browser.

Parser2's first compile also found a parenthesized aggregate initialization unsupported by the installed Emscripten Clang 15. Use ordinary brace initialization for TraceProcessingArgs in the test callback, preserving its values and behavior.

Compiling the portable TUI core with Clang 15 exposed its anonymous union's deleted implicit default constructor. Give TuiPixel an explicit constructor selecting the existing zero-initialized character member; preserve glyph, color and style defaults and verify them in the Wasm boundary regression.
Replace its two designated aggregate constructions in PrintChar with a normally initialized pixel and field assignments. Share MeasureChar in TUI.Linux.cpp with Wasm, using the SDK's locale-independent Unicode wcwidth implementation while retaining native thread-local locale selection and the native-only terminal backend.

GacUI's MiniHTTP automation adapter is the only GacUI source consumer of native asynchronous TCP sockets. Guard its declaration and implementation with the same native-platform condition as that VlppOS API. The ordinary automation-service unit tests use in-process mocks and remain enabled.

Use brace aggregate initialization for GacUI's TuiColor test expectations as well. Preserve every assertion and color value. Correct the new unsupported-backend regression to pass TUI::Start its required options argument.
Clang 15 also rejects an implicit capture of a structured binding inside the message-dialog test's generic lambda. Capture the dialog name explicitly by value in the scheduled callback, retaining its original lifetime and value.

The unoptimized Workflow RuntimeTest reaches RPC execution but Firefox reports a host recursion limit in nested interpreter/reflection calls. Use Wasm-only `-O1` in the existing project compile/link options to reduce wrapper call depth while retaining debug information, exceptions and all assertions; native compilation options remain the same. Confirm this against the complete unchanged runtime suite.

Apply the same Clang 15 capture correction to five document-configuration test loops: use ordinary local variables for the two indexed loops and ordinary range iteration where the index is unused. Preserve all inputs, names and assertions.

The complete optimized runtime suite passes 4 files / 264 cases with all 242 fixtures read. Keep the Wasm-only optimization and document why it is required.

The unoptimized CppTest_Reflection Wasm module builds, but Firefox fails during WebAssembly instantiation with an out-of-memory error before any test runs (528 MiB module). Apply the same Wasm-only `-O1` compile/link setting to reduce generated code size, retaining all reflection tests, debug information and exceptions. Verify the entire suite in the browser.

GacUI already produces a 539 MiB module before Asyncify instrumentation, and its unoptimized link exceeds 35 GiB of resident memory. Given the full-reflection module instantiation failure at this scale, stop that unoptimized link and use Wasm-only `-O1` for GacUI as well. This is a build-size and call-depth correction; no tests or fixtures are removed. Confirm against the full browser suite and a native rebuild.

GacUI also fails to instantiate with `-O1`: the resulting module is 796.8 MiB, including 312.9 MiB of executable Wasm code. Following [Emscripten size-optimization guidance](https://emscripten.org/docs/optimizing/Optimizing-Code.html) and [Asyncify guidance](https://emscripten.org/docs/porting/asyncify.html), use Wasm-only `-Oz` for this project to reduce instrumented code. Keep `-g`, C++ exceptions, all C++ assertions, the pthread pool and complete Asyncify coverage; do not exclude virtual filesystem calls or tests from instrumentation. The native verification checkout has the same C++ sources and native compiler flags.

The native GacUI checkout passed 93 files / 1,813 cases. Its source/configuration diff remained identical to the prepared patch. The only generated differences are 28 snapshot files: Windows-versus-Unix file paths, UTF-16-versus-UTF-32 caret offsets, current-day date-picker highlights, and asynchronous file-dialog frame/element ordering. These are kept out of the main working tree; no fake-file-dialog scheduling code or snapshot baselines are part of this port.

The size-optimized GacUI browser suite loads successfully, then exposes a reflection defect in the existing EasyLayout XML test. DWARF in `GuiEasyLayout.o` confirms that `GuiEasyCellLayout`, `GuiEasyRowLayout` and `GuiEasyColumnLayout` are all 144 bytes in Wasm. `VlppReflection/Source/Reflection/DescriptableObject.h` currently updates the descriptor only when a later `Description<T>` has a strictly larger object size, so rows and columns retain the cell descriptor and fail reflected assignment. Accept equal sizes as well, allowing the later derived description to replace the base while retaining the existing unregistered-type fallback. Add aligned base/derived regression classes covering direct/reflected construction, construction before type registration, and an unregistered subclass. First reproduce the regression without the fix, then verify the reflection suites and downstream GacUI/Workflow consumers after regeneration and import synchronization.

The aligned reflection regression reproduces natively before the change: both classes are 128 bytes and the object created before registration reports the base descriptor. With the equal-size correction, all 54 reflection cases pass on native Linux and Firefox/Wasm; metadata generation passes 175 cases and metadata round-trip validation passes 174 cases with unchanged metadata artifacts. The correction is compiled only with full reflection; metadata-only and no-reflection consumers retain their existing behavior.

The downstream native GacUI rerun crashes during color-dialog resource construction. An AddressSanitizer-preloaded diagnostic run identifies a null descriptor in `Value::CanConvertTo`; LLDB stalled before test execution in this environment. Temporarily log the dynamic C++ type from `DescriptableObject::GetTypeDescriptor`, regenerate the reflection release into the isolated native diagnostic checkout, and restore that instrumentation after identifying the missing descriptor. The equal-size correction remains provisional until the complete downstream suites pass.

The diagnostic identifies `GuiResourceManager`: it implements the equally sized `IGuiResourceManager` and `IGuiPlugin` interfaces and is constructed before reflection registration. The later plugin slot stays null forever, while the earlier manager slot becomes registered. A plain `<=` therefore loses the useful slot. Retain a linked list of fallback descriptor slots only when an equal-size tie occurs while both slots are unresolved. Resolve the newest registered slot in `GetTypeDescriptor` and free the optional fallback nodes with the object. Ordinary construction after registration keeps its existing allocation-free selection path. Extend the aligned regression with an unregistered sibling interface and an object constructed before registration. Preserve the equal-sized derived-class checks and rerun downstream native/browser verification with the corrected implementation.

The user requires the size correction to affect WebAssembly only. Put all equal-size handling, fallback storage/lifetime/lookup, and the aligned regression types/registration/test case behind `#ifdef VCZH_WASM`. Preserve the original native object layout and strict-size selection code. Restore native GacUI's original reflection behavior, regenerate releases and synchronize imports, then verify the Wasm correction and native compatibility.

Code review adds explicit size-equality assertions to the Wasm-only aligned regression. These ensure that the test continues to exercise descriptor ties if class layouts change in the future.

Repeated native interprocess verification exposes an existing Socket HTTP shutdown race: the third repetition fails `!state->eventWholeStopReturned.WaitForTime(0)` while a poll response is deliberately held. `StopCore` removes the connection from the server's stopping list as soon as callbacks finish, before its in-flight poll completes. A concurrent whole-server Stop can therefore miss that poll. Retain stopped connections until callbacks, poll registration and the in-flight poll all drain; attempt release from callback completion, poll completion and Stop completion. Keep the existing held-poll regression and require 25 consecutive complete native interprocess runs plus the native suite. This native transport correction is separate from the Wasm-only reflection size fix and will receive its own commit, as required by the coding guidelines for existing unstable bugs.

The final native GacUI rerun with the Wasm-only reflection guard passes 93 files / 1,812 cases; the initial run's extra assertion was creating a previously absent output folder. Its GacUI sources and reflection imports match the final port. The subsequent native HTTP lifecycle correction is verified in VlppOS's repeated and complete native suites. The other checkout differences are the documented Wasm-only `-O1` versus `-Oz` setting and its Project.md note, plus regenerated snapshots in the same file-dialog, date-picker and supplementary-character categories. These generated snapshots remain excluded from the change.

The native Clang EasyLayout test originally passed because its base and derived layouts differ: DWARF reports 272 bytes for `GuiEasyCellLayout` and 280 for `GuiEasyRowLayout`, whereas the original Wasm objects are both 144 bytes. Thus the strict-size heuristic works for this native layout but fails for Wasm. The aligned regression also reproduced the general defect natively before being restricted to Wasm at the user's request. Removing the guarded additions from the final reflection header and implementation gives the original native code (apart from whitespace), including its object layout and descriptor selection. The final browser reflection suite passes 54 cases with explicit size-equality assertions; native reflection remains 53 cases and metadata generation/round-trip tests pass 175/174 cases.

The shutdown correction passes 20 complete native interprocess repetitions. Repetition 21 then hangs in the separate raw AsyncSocket Channel test, with the main thread waiting on a futex and one completion thread waiting in `io_cqring_wait`; the held-poll regression itself passed. Direct LLDB attach is blocked by the system's ptrace policy. Reproduce this separately in an isolated native debug checkout with only the existing raw-socket test cases selected, launch it as LLDB's child, and inspect the blocked shutdown before changing the native backend. Restore any diagnostic selection/instrumentation after use.

Launching the isolated test under LLDB 18 also stalls before C++ execution: lldb-server holds the child in `ptrace_stop` without delivering the launch-stop event. Use temporary in-process SIGUSR2 stack capture in the isolated checkout as a fallback, then symbolize the captured addresses. No system ptrace setting or shipped test source is changed.

The focused raw-socket run reproduces the original five-second channel timeout. Increase only the isolated diagnostic checkout's timeout to the documented 500-second debugging window, so blocked threads remain available for stack capture; the main working tree retains its five-second assertion.

The captured native stacks show the server and Jerry workers already idle while Tom waits for `eventTom`; the io_uring worker is simply idle, not blocked in shutdown. The channel test signals the server immediately when both Stop messages arrive, before the last client's `BatchWrite` returns and signals its own completion. Check the existing connected-after-send assertion with temporary diagnostics to distinguish this test-ordering race from a backend defect.

Add temporary diagnostics for swallowed Vlpp errors in the isolated io_uring completion loop and thread-pool callbacks, preserving the original control flow, to identify the callback that did not signal Tom's completion.

The instrumented raw-socket stress run completes 400 cases without an error, so the connected-after-send hypothesis is not yet established. Add an in-memory message/lifecycle trace only in the isolated checkout and dump it when a channel round exceeds ten seconds. Include protocol error/disconnect callbacks and client/server completion, preserving all assertions. This will distinguish a lost message from premature teardown without logging every successful exchange.

The trace identifies the raw-channel failure: the worker reports `Channel server should have three client ids.` `ConnectLocalClient` broadcasts the IDs and greetings synchronously; Tom can start the exchange, Jerry can send Stop and destroy its client, and the server's later client-count assertion then sees fewer than three clients. Worker exception suppression and server teardown explain both the missing worker count and the earlier missing Tom completion. There is no evidence for the connected-after-send hypothesis. Add a manual server-client-ready event to `ChannelChatData`, signal it after the server validates all three IDs, and make Tom wait for it before sending the first Hello. This preserves the three-client assertion, the five-second timeout, and all transport code. Verify the unchanged complete native interprocess file 25 consecutive times and the complete native suite. Commit this existing test race separately from the browser port and HTTP lifecycle correction.

With the server-client-ready barrier and the held-poll lifetime correction, all 25 consecutive runs of the complete native interprocess file pass 29/29 cases. The original five-second timeout, three-client assertion and held-poll assertion are unchanged. The isolated diagnostics have been removed; the complete native VlppOS suite is the final check before committing these two existing races separately.

The complete GacUI browser suite is progressing through its long control/selection matrix. Allow up to two hours in the external verification harness so its initial one-hour deadline does not discard a healthy run; this changes no C++ test, application timeout, assertion or shipped launcher.

The complete GacUI browser run passes the first 82 test files, then stops advancing in `TestRemote_UnitTestFramework_Async.cpp` / `Async Channel`. This case moves the UI and resource loading onto a real pthread while the original worker waits on an event. Inspect the running worker diagnostics and the console/OPFS boundaries before changing implementation; the browser launcher installs console callbacks on its main worker, which may leave pthread calls without them. Keep the full asynchronous test enabled.

Live inspection finds all 32 pthread workers lack the launcher console callbacks. Four active pool workers are waiting on semaphores; the UI pthread has no active Wasm frames and has retained its stack, consistent with an interrupted asynchronous continuation. The main test worker remains waiting for its completion event. Rebuild a temporary `/F:TestRemote_UnitTestFramework_Async.cpp` selection, log C++ errors at its thread boundary and log worker promise rejections in the external harness. Back up and restore these diagnostic edits before the final full-suite build.

The isolated asynchronous case reproduces the stall without reporting a C++ exception or worker promise rejection. Before making a library change, expose temporary Asyncify/OPFS diagnostics through an external browser route to identify the suspended operation and its worker state. These route-only diagnostics do not change repository or build artifacts.

The complete pthread trace confirms `TypeError: globalThis.vlConsoleColor is not a function` in `UnitTestRemoteProtocol::ProcessRemoteEvents` while printing a normal frame message. The exception escapes an Asyncify continuation after OPFS snapshot writing, leaving the caller waiting. Fix the upstream Wasm console boundary in Vlpp: synchronously execute console operations on the main runtime worker, where the launcher callbacks and Embind handles live; catch C++ exceptions in the dispatch thunk and rethrow on the caller. Catch JavaScript callback failures at the existing EM_JS boundary and translate them to the existing C++ errors. Keep the whole implementation inside the existing Wasm-only file guard. Add a Wasm-only VlppOS pthread regression covering write, color, title, Unicode/empty input and callback failure propagation. Regenerate Vlpp and synchronize imports, then verify Vlpp/VlppOS, the focused GacUI case, and the complete affected browser suites. Restore the temporary GacUI test selection and exception logging before final verification.

The console dispatch and error translation pass Vlpp's complete non-pthread Wasm suite (32 files / 473 cases) and VlppOS's complete pthread suite (11 files / 109 cases), with exactly one successful browser completion and no JavaScript errors. The new tests verify the Unicode output marker, UTF-16-to-WString input including an embedded zero, present empty input, EOF, and all four callback failures returning as C++ errors to the worker. Main-thread and non-pthread paths remain covered by Vlpp's existing tests.

The host restart clears the temporary browser harness, logs and diagnostic backups, while all repository changes survive. The focused GacUI module appears to have finished linking before the restart; validate it in a fresh browser before rebuilding. Recreate the harness and future verification artifacts under `~/.cache/vlpp-wasm-extend`, restore temporary diagnostics from the reviewed diff, and serialize memory-heavy builds with reduced parallelism. Previously observed completed test results remain recorded above; interrupted runs are not counted as passes.

After the restart, the production launcher completes the focused GacUI asynchronous file: 1 file / 3 cases, including all Ready/Hover/Press frames, exactly one `wasm_main returns 0`, and no browser errors. The upstream console dispatch fixes the confirmed stall. Remove the temporary file filter and restore the original GacUI thread exception handling, then rebuild and run the complete suite.

Crash recovery finds 30 empty object/dependency pairs in Workflow CppTest_Reflection and an incomplete RuntimeTest module. Remove the invalid ignored outputs and rebuild through the normal script; validate the surviving object sections before reusing them. Final verification runs will be recorded in the persistent cache.

The restarted RuntimeTest browser remains before its first C++ log while the other suites progress. Inspect its private verification worker and fixture-prefill state before treating it as a C++ failure; no test or timeout is changed.

RuntimeTest's worker disappears after fetching 157/242 fixtures, before C++ module initialization, with no browser error. This matches a documented Firefox worker-lifetime failure when asynchronous work has no strongly rooted Worker binding ([Mozilla bug 1594848](https://bugzilla.mozilla.org/show_bug.cgi?id=1594848)). Test that hypothesis by retaining the Worker through a pagehide listener in the external verification HTML, without changing C++ or repository files. A focused forced-GC startup regression will distinguish a real lifetime fix from a successful retry.

Forced page garbage collection confirms the launcher defect: the original worker disappears after two collections, before downloading its first fixture, with no error or completion. Retaining it through a window pagehide listener completes the same 242-file prefill; the complete unchanged RuntimeTest also passes 264/264 cases and reads every fixture. Add that page-lifetime reference to the canonical Tools launcher, with worker termination on pagehide, and distribute it through `vgo uci` to the seven affected libraries. Run 25 consecutive forced-GC startup checks, using an external route that reports completion immediately after the real prefill, and keep the complete C++ suite results separate. Commit this existing launcher defect separately once the stress verification passes.

All 25 consecutive retained-worker startup checks pass under forced garbage collection, downloading all 242 files each time without an error. The unfixed baseline loses its worker after two collections and never reports completion. The canonical launcher is also exercised without the diagnostic startup route by complete Workflow CppTest_Reflection (232 cases) and LibraryTest (21 cases). Commit the two-line launcher correction and its documentation separately; distributed library copies remain in the port patch.

The user requests a checkpoint commit of everything before continuing tests. Commit and push the reviewed port in all seven libraries now, keeping No.2 pending until the remaining verification finishes. Tools launcher fix `0a2f00e` is already pushed. After the restart, Parser2 passes 463 cases and all five Workflow browser suites pass (Library 21, Runtime 264, and each CppTest variant 232); Parser2 and Runtime read all 328/242 fixtures. Runtime also passes with the final packaged launcher. GacUI's complete browser run is still progressing through its control matrix without failures. Its focused asynchronous file passed 3 cases after the console correction. The final VlppOS browser regression build is ready. Continue GacUI and that regression after the checkpoint; do not claim the unfinished full GacUI run as a pass.
