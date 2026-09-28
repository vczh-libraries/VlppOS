# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

I would like you to convert this repo to be compatible with web assembly, as well as two others:
- `VlppOS/Test/Linux/UnitTest`
- `VlppRegex/Test/Linux`
- `VlppReflection/Test/Linux/UnitTest`
There are other test projects but they do not have plan to work with web assembly yet.

First you need to fix `vbuild`. Currently only `../Vlpp/Test/Linux` builds wasm, but there is no configuration to tell if this project works with web assembly or not. So here I add one thing to `vbuild -bw|-fbw`:
- Try to read the `vbuild` file in pwd, if this file exists, and if this file has this line "WASM=YES", it means the project works with web assembly. Otherwise `vbuild -bw|-fbw` should just deny to run.
  - To make the work simpler, we could just test if this string exists in the file, no need to worry about if it is actually a line. Or if testing a line is easier, then do the line test.
- Add such `vbuild` to `../Vlpp/Test/Linux` and `Test/Linux/UnitTest`.

Second you need to upgrade the Source folder:
- InterProcess/*/*/* should deny `VCZH_WASM`, there is two options:
  - If a file only works in a certain platform, nothing needs to do.
  - If a file works in all platform, you need to add `#if defined VCZH_MSVC || defined VCZH_GCC`.
  - It includes InterProcess/NetworkProtocolHttp.*
  - It does not include other files inInterprocess
- Tui/*.* should deny `VCZH_WASM`.
- FileSystem.Linux.cpp should deny `VCZH_WASM`, and currently we don't invent a file system implementation, so do not create a wasm version. For now trying to access file system just crash because no implementation is offered.
- Local.Linux.cpp should work with `VCZH_WASM`.
- You need to implement a `Threading.Wasm.cpp`.

Third you need to upgrade test cases:
- Only `UnitTest` needs to build with web assembly.
- Test files are in Test/Source, you are going to make the following files deny `VCZH_WASM`:
  - TestFileSystem.cpp
  - TestInterProcess*.*
  - TestLocale.cpp
  - TestStream.cpp but only deny `Test FileStream` and `Test CacheStream with seekable stream`
  - TestStreamLzw.cpp
  - TestTui.cpp

You can read about `../Vlpp/TODO_Task.md` and figure out how the original work was done.
You need to first change `Tools` and `Vlpp`, release `Vlpp` to `VlppOS`, and then release ubuntu tool chain to `VlppOS`, and then handle `VlppOS`:
- You need to probably fix `CodegenConfig.xml` to make all `*.Wasm.*` included in generated `*.Linux.*`.
commit and push all local changes before doing the following.

The whole `VlppRegex` repo does not involve any OS specific thing, first release `Vlpp` and `VlppOS` to `VlppRegex`, and then make `VlppRegex/Test/Linux` works with web assembly, but deny `TestAutomation.cpp` as it uses `FileStream`. All other test cases should be fine.
commit and push all local changes before doing the following.

Now its time for `VlppReflection/Test/Linux/UnitTest`, release `Vlpp` and `VlppOS` to `VlppReflection`, deny `TestReflection_Builder.cpp` as it uses `FileStream`. All other test cases should be fine.
commit and push all local changes before doing the following.

# TEST

- Verify all four Wasm build aliases reject missing or disabled project opt-in before invoking make, including before cleaning a full build. Verify `WASM=YES` admits builds without executing the configuration file.
- Build and run the complete Vlpp suite in the browser and natively after updating its packaging configuration.
- Build and run the retained VlppOS, VlppRegex and VlppReflection UnitTest suites in the browser, with exactly one successful `wasm_main` completion and no unexpected skipped tests. Preserve the VlppOS concurrency tests.
- Run complete native suites to verify guarded files remain available. Verify other projects reject Wasm builds.
- Regenerate releases using CodePack and copy generated release files to downstream imports. Confirm Wasm implementations occur in Linux releases and verify consumers compile those releases.

# PROPOSALS

- No.1 Explicit project opt-in and guarded platform implementations

## No.1 Explicit project opt-in and guarded platform implementations

Read the opt-in file as text. Keep compiler-independent source inventories. Package Wasm implementations with Linux releases. Exclude the requested native services and tests using positive native guards. Leave the injectable filesystem without a default implementation on Wasm. Reuse the fallback locale and character encoding implementation.

The existing `TestThread.cpp` requires concurrent threads, mutexes, semaphores, events, reader/writer locks, thread pools, task queues and TLS. Use Emscripten pthreads, share applicable POSIX implementations, and implement browser sleep/CPU queries in `Threading.Wasm.cpp`, with guarded join/recycling and finite-pool behavior in the shared implementation. Add optional pthread configuration and worker artifact tracking to the canonical toolchain, and serve isolation headers from the generated launcher. Follow the existing exception-safe `WasmMain` entry contract.

### CODE CHANGE

- Tools now checks the text opt-in before build/clean, emits optional pthread pool configuration, tracks the worker output, and serves COOP/COEP with the generated Node launcher. Tools was committed and pushed before `vgo uci` propagation.
- Vlpp opts in and packages `*.Wasm.*` in its Linux release; regenerated releases were copied into VlppOS. Vlpp was committed and pushed after its browser, Clang and GCC suites passed.
- VlppOS excludes the requested native services and tests with positive platform guards, shares locale and character encoding code, fails explicitly on filesystem access, and keeps the complete threading suite. Its POSIX backend now supports Wasm, reclaims retained pthreads through serialized joins, and uses four thread-pool workers. Added a concurrent/repeated wait test to cover one pthread joined by several waiters.
- Fixed explicit dependent-type syntax needed by the installed Emscripten compiler. The new Wasm file is registered in project/filter metadata and included in the regenerated Linux release.
- VlppRegex received the generated Vlpp/VlppOS releases and canonical Ubuntu toolchain, opted its Linux UnitTest project into Wasm, and uses the same browser entry contract. The requested filesystem test is named `TestAutomaton.cpp`; guarded that file for native platforms and retained every other test file. No library source changes were needed.

### VLPP AND VLPPOS VERIFICATION

- Opt-in: 20 checks passed across all four aliases, absent/empty/disabled/enabled files and a marker embedded in text; configuration contents were never executed. Both unsupported VlppOS projects rejected incremental/full Wasm builds.
- Vlpp: browser 32 files / 469 cases; native Clang and GCC 32 files / 465 cases, all passed.
- VlppOS final browser run: 7 files / 81 cases, all passed, including the added concurrent-wait regression. Exactly one completion, 33 observed workers, cross-origin isolation enabled and no browser errors.
- Unchanged Wasm build compiled/linked nothing. Removing `app.worker.js` caused it to be regenerated; `Bin/UnitTest` matched `app.wasm`.
- Native UnitTest passed 14 files / 277 cases. Native MiniHttpServer and TuiPlayground builds passed.
- A separate consumer compiled the generated Vlpp/VlppOS releases for Wasm and passed a browser check that filesystem access fails explicitly while memory streams remain available.
- VlppRegex and VlppReflection implementation and verification follow the requested commit/push checkpoint.
- Emscripten 3.1.6 emits its advisory warning about pthreads with growable memory; the setting permits allocations beyond the initial 128 MiB. Windows/macOS execution is not claimed.

### VLPPREGEX VERIFICATION

- Wasm build passed. Firefox passed all 8 files / 192 cases, including all Unicode lexer/walker/colorizer paths, with exactly one successful completion and no browser errors.
- Native Clang build passed and all 9 files / 226 cases passed, including the file-based automaton baseline comparisons.
- Every Vlpp/VlppOS import is byte-identical to its generated upstream release. Preserved the Unicode test file's BOM.
- The second checkpoint commits and pushes VlppRegex and this investigation before starting VlppReflection.
