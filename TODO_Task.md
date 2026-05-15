investigate repro

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

## Task 1

**IMPORTANT** This work happens in `VlppOS` repo.

Refactor `NetworkProtocolChannelClient` and `NetworkProtocolChannelServer`.
- Check two latest commit titled `Update TextNetworkProtocol.h` you will find I performed some change to `NetworkProtocolChannelClient::Channel`. I would like you to:
  - Pay attention to the change in `NetworkProtocolChannelClient::Channel`, I improved the code, similar issues will also be in the server version.
  - Extract similar part from both channel to NetworkProtocolChannel as much as you can. If there is any differences, respect the client version.
  - Both channel calls `client->BatchWrite` and `server->BatchWrite`, you can add a `BatchWrite` virtual function to the base class (`NetworkProtocolChannel`), and I think no other places call `client->` and `server->`. And two versions of channels inherit from that base class and implement `BatchWrite` for redirection.
  - Check if any shared code that appear in both client and server, will it be extracted to static helper functions in `NetworkProtocolChannel`, or any better form, so that implementation would become simpler? Make your own judgement. But more importantly, I don't want you to just randomly find something to extract so that you can say you extracted more. All extraction must be reasonable.
- Review data structures.
  - I have changed `NetworkProtocolChannelClient::Channel::queuedPackages` from a list to group, so that the list for each group is naturally maintained and can be feed to `BatchWrite` directly, avoiding unnecessary copying. You can check existing code and see if anything could be improved.

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

## Task 3

**IMPORTANT** This work changes many repos.
**IMPORTANT** If my proposal is not working in Task 2 because the assumption is wrong, skip Task 3.

In Task 2, you have updated:
- `copilotExecution.ps1` and `.gitignore` and `Running-UnitTest.md`, they should be moved to `Tools` repo and override files in `Copilot` folder.
- `Vlpp.cpp`, you should fix related code in `Vlpp` repo, run `Tools/Tools/CodePack.backup.exe` on `Vlpp/Relase/CodegenConfig.xml`, and the `Vlpp.cpp` will be updated. Copy `Vlpp/Release/Vlpp.cpp` to `VlppOS/Import/Vlpp.cpp` and find if there is any `git diff`, if there is any, your patch to `Vlpp` repo is not precise.
- Any other shared files that I forget to mention here.

After finishing, `VlppOS` repo should have no change, you should commit and push `Vlpp` and `Tools` repos.

Find all unit test projects in `VlppRegex`, `VlppReflection`, `VlppParser2`, `Workflow`, `GacUI`, any cpp files calls `UnitTest::RunAndDisposeTests` should have `_CrtDumpMemoryLeaks` calls, replace it to `UnitTest::DumpMemoryLeak`. In order to make it work, you should:
  - Copy `copilotExecution.ps1`, `.gitignore`, `Running-UnitTest.md`, `Vlpp.cpp` to coressponding folders in each repo.
  - Compile the solution, no test running needs to perform.
  - After fixing any potential compile errors, commit and push all these repos.
  - Reminder, there will be many `main.cpp` or whatever cpp files need to update. You can simply search for `_CrtDumpMemoryLeaks` and figure out if it runs test cases. A few might not because they are "CLI" or "GUI"  project instead of "UnitTest" project. Only fix "UnitTest" project.
