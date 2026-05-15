# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

- you have to follow `REPO-ROOT/.github/Guidelines/Coding.md` when coding.
- you have to run unit test to make sure your change works.
- you have to commit and push all local changes after finishing any task, before doing the next task.
  - It is important to do task one by one strictly, by me designing tasks in this way, we can achieve:
  - Easy-to-understand commits for file changing that is easy to review.
  - Limit side effects so that you don't have to deal with massive of issues at the same time.
- each task will be treated as a new `# Repro`, that is, to wipe the document before execution.

All tasks below are for completing `vl::inter_process`.
`UnitTest` test project has been configured to only run `TestInterProcess.cpp` under debug x64.
I think this is the only test file you need.

## Task 2

**IMPORTANT** This work happens in `VlppOS` repo.

For debug execution, `_CrtDumpMemoryLeaks` dumps memory leaks:
- It appears even when only `TestInterProcess.cpp` is executed so the previous work may introduce the memory leaks.
- But I am not telling just to fix this, you should do:
  - I specifically grand you permission to change `Vlpp.cpp`, and do this:
    - Besides of `/C`, `/D`, `/R`, `/F:xxx`, recognize `/DebugOutput:file`.
    - Only one `/DebugOutput:file` is allowed, and it does this:
      - Create a `vint debugOutputArgIndex = -1` below `vl::unittest::execution_impl::failureMode`, if the option is specified, set it in `RunAndDisposeTests`.
    - Write a new `UnitTest::DumpMemoryLeak(argc, argv)` function:
      - `#ifdef` `VCZH_MSVC` and `VCZH_CHECK_MEMORY_LEAKS`, does this:
        - Call `_CrtSetReportFile` and `_CrtSetReportMode` so that `_CRT_WARN` redirects to the file.
        - `argv[debugOutputArgIndex+1]` will be the string `/DebugOutput:file`, now you can extract the file name.
        - Calls `_CrtDumpMemoryLeaks` and closes the file.
      - Otherwise, it is blank.
  - In `Main.cpp`, the 3 lines of code will be replaced by `UnitTest::DumpMemoryLeak`.
  - In `copilotExecute.ps1`, **ONLY WHEN** `UnitTest` mode is specified, specify `/C` and `/DebugOutput:xxx`, and use `Execution.log.memoryleaks`. This file should be removed before and after execution just like `Execution.log.unfinished`. After finishing execution, `Execution.log.memoryleaks` will be appended at the end of copied `Execution.log`. `/DebugOutput` should use absolute path.
  - `REPO-ROOT/.github/Scripts/.gitignore` in the same folder should be fixed.
  - `REPO-ROOT/.github/Guidelines/Running-UnitTest.md`'s `### The Correct Way to Read Test Result` should say, only when Windows + Debug, if memory leaks happen, it will appear at the end of the log. and memory leaks should always be fixed after test cases passed. Fixing memory leaks is important and it is a must have, organize your own workd and update this document.
  - My proposal is based on an assumption, `_CrtDumpMemoryLeaks` calls `OutputDebugString` by default.
- After you are able to dump it, try to fix the issue, and keep verifying until the memory leaks are gone.

```
Detected memory leaks!
Dumping objects ->
C:\Code\VczhLibraries\VlppOS\Source\Threading.cpp(95) : {156} normal block at 0x000001A208EF3E30, 16 bytes long.
 Data: <`f X            > 60 66 9D 58 F6 7F 00 00 00 00 00 00 00 00 00 00
C:\Code\VczhLibraries\VlppOS\Source\Threading.cpp(95) : {155} normal block at 0x000001A208EF3C00, 16 bytes long.
 Data: < f X    0>      > A0 66 9D 58 F6 7F 00 00 30 3E EF 08 A2 01 00 00
C:\Code\VczhLibraries\VlppOS\Source\Threading.cpp(95) : {154} normal block at 0x000001A208EF2FD0, 16 bytes long.
 Data: <8f X     <      > 38 66 9D 58 F6 7F 00 00 00 3C EF 08 A2 01 00 00
Object dump complete.
```

# UPDATES

# TEST [CONFIRMED]

Task 2 is verified by the Debug x64 UnitTest project through the repository scripts. Success criteria:

- The unit-test framework accepts `/DebugOutput:file` in combination with `/C`.
- `copilotExecute.ps1` writes the memory-leak report to `Execute.log.memoryleaks`, appends it to `Execute.log`, and removes the temporary file after execution.
- `Execute.log` ends with the normal pass summary and has no appended memory leak dump.
- The Debug x64 build has 0 warnings and 0 errors.

Confirmed with:

- `copilotBuild.ps1 -Configuration Debug -Platform x64`: `Build succeeded`, `0 Warning(s)`, `0 Error(s)`.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`: command line included `/C /DebugOutput:"C:\Code\VczhLibraries\VlppOS\.github\Scripts\Execute.log.memoryleaks"`, with `Passed test files: 12/12`, `Passed test cases: 115/115`.
- `Execute.log` contains no `Detected memory leaks!`, `Dumping objects ->`, or `Threading.cpp` leak entries after the summary.
- `Execute.log.memoryleaks` and `Execute.log.unfinished` do not remain after execution.

# PROPOSALS

- No.1 Add debug-output leak capture and dispose TLS storages [CONFIRMED]

## No.1 Add debug-output leak capture and dispose TLS storages

### CODE CHANGE

- Added `/DebugOutput:file` option parsing to `UnitTest::RunAndDisposeTests`, with duplicate detection and usage text.
- Added `UnitTest::DumpMemoryLeak(argc, argv)` overloads. On MSVC debug leak builds they redirect `_CRT_WARN` to the requested file, call `_CrtDumpMemoryLeaks`, restore the previous report settings, and close the file. On unsupported builds the function is blank.
- Replaced the direct `_CrtDumpMemoryLeaks` call in `Test/UnitTest/UnitTest/Main.cpp` with `UnitTest::DumpMemoryLeak`, and called `ThreadLocalStorage::DisposeStorages()` before leak detection.
- Updated `copilotExecute.ps1` to pass `/C /DebugOutput:"absolute path"` in UnitTest mode, append the memory leak file to `Execute.log`, and remove `Execute.log.memoryleaks` before and after execution.
- Updated `.github/Scripts/.gitignore` and `Running-UnitTest.md` for the new memory leak log behavior.

### CONFIRMED

The reported leak blocks came from `ThreadLocalStorage::PushStorage` records for global `ThreadVariable` instances. Disposing thread-local storages in `Main.cpp` removes those records before memory leak detection runs. The updated UnitTest script passes the new `/DebugOutput` option, the test binary accepts it, all tests pass, and no leak dump is appended to the final log.
