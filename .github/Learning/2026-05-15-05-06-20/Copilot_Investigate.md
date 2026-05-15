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

**IMPORTANT** This work changes many repos.
**IMPORTANT** If my proposal is not working in Task 2 because the assumption is wrong, skip Task 3.

Add `new int;` at the last line of `TEST_CASE(L"HttpServer (Channel)")` so that the test case will leak memory.
I found when I was doing this, no memory leak are detected at the end of `Execute.log`. Figure this out. You are granted permission to change files in the `Import` and `.github` folder. Probably `Vlpp.cpp` and `copilotExecute.ps1` will be affected.

Any changes should be also port to `Vlpp` repo (if `Vlpp.h` and `Vlpp.cpp` in `Import` folder is changed) and `Tools` repo (if files in `.github` folder is changed, those files are from `Tools/Copilot`).

After fixing the issue, remove `new int` and check again to see the leak in `Execute.log` is gone, and push all 3 repos if they have local changes.

# UPDATES

# TEST [CONFIRMED]

The reproduction is to temporarily add `new int;` at the end of `TEST_CASE(L"HttpServer (Channel)")`, build Debug x64, and run UnitTest through `copilotExecute.ps1`. Success criteria:

- With the intentional leak present, `Execute.log` contains the memory leak dump after the passed test summary.
- After removing the intentional leak, the same UnitTest run passes and no memory leak dump remains at the end of `Execute.log`.
- If leak-capture code or scripts need changes, matching changes are ported to `Vlpp` and `Tools/Copilot` as required.

# PROPOSALS

- No.1 Verify and port debug-output memory leak capture [CONFIRMED]

## No.1 Verify and port debug-output memory leak capture

### CODE CHANGE

- Updated `Vlpp/Source/UnitTest/UnitTest.cpp` and regenerated `Vlpp/Release/Vlpp.cpp`.
- Ported the regenerated `Vlpp/Release/Vlpp.cpp` to `VlppOS/Import/Vlpp.cpp`.
- Enabled debug CRT allocation tracking while unit tests run by setting `_CRTDBG_ALLOC_MEM_DF` when `VCZH_CHECK_MEMORY_LEAKS` is defined.
- Redirected `_CRT_WARN`, `_CRT_ERROR`, and `_CRT_ASSERT` reports to the debug-output file using the OS handle from `_get_osfhandle(_fileno(file))`, instead of passing `FILE*` to `_CrtSetReportFile`.
- No `Tools/Copilot` script change was needed; `copilotExecute.ps1` was already passing `/DebugOutput` and appending the non-empty memory-leak file to `Execute.log`.

### CONFIRMED

- With temporary `new int;` at the end of `TEST_CASE(L"HttpServer (Channel)")`, Debug x64 UnitTest passed and `Execute.log` appended:
  - `Detected memory leaks!`
  - `TestInterProcess.cpp(613) ... normal block ... 4 bytes long.`
- After removing the temporary `new int;`, Debug x64 build succeeded with `0 Warning(s)` and `0 Error(s)`.
- Final `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64` passed with `Passed test files: 12/12` and `Passed test cases: 115/115`.
- Final `Execute.log` contains no `Detected memory leaks!`, `Dumping objects ->`, or `Object dump complete`.
- Upstream `Vlpp` Debug x64 build succeeded with `0 Warning(s)` and `0 Error(s)`.
- Upstream `Vlpp` UnitTest run passed with `Passed test files: 31/31` and `Passed test cases: 462/462`, with no memory leak dump.
