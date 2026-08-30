The last work in .github/TaskLogs/Copilot_Investigate.md has just been done, and the work must also be verified in downstream repos:
- StartRpcStdio.ps1 in Workflow
- StartRpcStdio.ps1 in GacJS
- GacUI jobs:
  - GacUI/.github/Jobs/job.rpWindows.prompt.md
  - GacUI/.github/Jobs/job.rpXPlat.prompt.md
  - Since we only need to verify if the network protocol upgrade is successful or not, you can limit the verification on `rvmt` app, but try all combinations.
  - This will save some time.

Before doing verification, you need to release VlppOS to Workflow. When Workflow is verified, release VlppOS and Workflow to GacUI, continue the rest of the verification.

You might also need to verify if any knowledge base page is affected by the original work; you can perform the change in Tools repo.
GacJS documents and the website might also need to be checked. If the website is changed, publish the website immediately.
Commit and push all local changes once finishing.

## DETAILS

### Ownership Contract and Downstream Repairs

- The completed VlppOS change makes connection-admission callbacks carry the producer's existing owning `Ptr` through the async-socket, HTTP-request, network-protocol, and channel layers. Preserve that same reference counter in every downstream override and handoff. Never construct a new `Ptr` from a raw callback argument or from `.Obj()`.
- The current `VlppOS/Release` and `Workflow/Import` snapshots still expose the old raw-pointer signatures. Regenerate and import them before using downstream compilation as evidence.
- Search all affected downstream source after each import and adapt every override to the owning signature. The known source locations at review time are:
  - Workflow:
    - `Test/Source/TestCasesRpcStdio_Driver.h`
    - `Test/UnitTest/RpcStdioTest_Driver/Main.cpp`
    - `Test/UnitTest/ChatBotServer/Main.cpp`
  - GacUI:
    - `Source/UnitTestUtilities/GuiUnitTestUtilities.cpp`
    - `Test/RemotingHelpers/RemotingServer/RemotingChannelServer.h`
- Change the source declarations and definitions, using `.Obj()` only when an existing raw-pointer operation requires it. Do not hand-edit any `Import`, `Release`, `IncludeOnly`, or other generated file.

### Release and Import Order

1. On Windows, prepare the documented Tools build prerequisites, including `UseMultiToolTask=true` and `VLPP_VSDEVCMD_PATH`, then run `& C:\Code\VczhLibraries\Tools\Tools\Build.ps1 VlppOS`. Require the build to succeed and CodePack to regenerate the six root `Release/VlppOS*` amalgamations and their six `Release/IncludeOnly/VlppOS*` mirrors from the completed source change.
2. Adapt the Workflow overrides above, then run `& C:\Code\VczhLibraries\Tools\Tools\Build.ps1 Workflow`. This imports the current VlppOS release, verifies Workflow, runs the native Release x64 stdio suite with `StartRpcStdio_SharedMemspSkipList.txt`, and regenerates the Workflow release only after those gates pass.
3. Adapt the GacUI overrides above, then run `& C:\Code\VczhLibraries\Tools\Tools\Build.ps1 GacUI`. This imports the current VlppOS and Workflow releases, verifies GacUI, and regenerates the affected `GacUI.UnitTest.{h,cpp}` and `Test.RemotingHelpers.{h,cpp}` pairs in both `GacUI/Release` and `GacUI/Release/IncludeOnly` from source.
4. Run the GacJS import, code-generation, build, test, and stdio gates against the updated sibling Workflow and GacUI checkouts before using GacJS in the GacUI matrix.
5. Before the Windows matrices, set the working directory to `C:\Code\VczhLibraries\GacUI\Test\GacUISrc`, run `& C:\Code\VczhLibraries\GacUI\.github\Scripts\copilotBuild.ps1 -Configuration Debug -Platform x64`, and run `& C:\Code\VczhLibraries\GacUI\.github\Scripts\copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`. The matrix launchers use these Debug x64 binaries, not the Release binaries produced by the Tools gate.
6. Before a Linux `wGac` or macOS `iGac` matrix, ensure the GacUI release is current, then follow `job.rpXPlat.prompt.md` to run that repo's `import.sh` and `syncProj.sh` and its documented portable Core, host, and platform-app builds. These scripts, rather than manual copies, propagate `GacUI/Release` and `Test.RemotingHelpers` into the platform repo.

- Inspect command output and generated diffs instead of relying only on `Build.ps1` process status. Its outer error handler can print a failure without guaranteeing a nonzero shell exit.
- Fix any compatibility defect in the owning source repository, repeat the affected release/import chain, and rerun the invalidated verification. Do not patch imported copies.

### GacUI Matrix Scope

- Here, `rvmt` means the `RemotingTest_Core /RVMT` rows. It does not include the separate standalone `CppTest_Rvm` rows. Recreate each job's complete current-platform matrix card. In the native-renderer card, retain and leave blank all `CppTest_Rvm`, `/RPT`, and `/FCT` rows. In the GacJS card, retain and leave blank all `/RPT` and `/FCT` rows; that template intentionally has no `CppTest_Rvm` row, so do not invent one.
- Run every applicable `/RVMT` combination from the current matrix documents with fresh processes:
  - Windows native renderer: `/Pipe`, `/Http`, and `/MiniHttp`, each with a manually connected native network host and with the native host auto-launched over stdio `/Cli:<path>`; 6 rows.
  - Windows GacJS renderer: `/Http` and `/MiniHttp`, each with a native network host, native stdio host, GacJS browser `?rvmhost`, GacJS Node `cli.js` network host, and GacJS Node SEA stdio host; 10 rows. `/Pipe` is not available to a fetch-based browser.
  - Linux or macOS native renderer: `/MiniHttp` with a manually connected native network host and with a native stdio host; 2 rows for the active OS.
  - Linux or macOS GacJS renderer: `/MiniHttp` with the same five host modes as the Windows GacJS rows; 5 rows for the active OS.
- `job.rpXPlat.prompt.md` covers only the current cross-platform OS: Linux uses `wGac`, and macOS uses `iGac`. Verifying both systems requires separate matching hosts; never mark one platform complete from another platform or from Windows.
- Maintain the matrix cards required by the job prompts and update every included cell immediately when it starts, passes, fails, or passes after a fix. Record failure and repair details under `Issues Found and Fix`.

### Documentation Propagation

- The ownership change definitely affects these canonical non-manual pages in `Tools/Copilot/KnowledgeBase`:
  - `KB_VlppOS_InterProcessAsyncSocketBasedMiniHttpApi.md`
  - `KB_VlppOS_InterProcessNetworkProtocolsAndChannels.md`
- The corresponding public-document sources are also stale and must be updated:
  - `WebsiteSource/packages/website-doc2/src/articles/vlppos/using-inter-process.xml`
  - `WebsiteSource/packages/website-doc2/src/articles/workflow/rpc/json-channel.xml`
  - `WebsiteSource/packages/website-doc2/src/articles/gacui/modes/remote_core.xml`
- Because these website sources require changes, the publication condition is satisfied. Follow `WebsiteSource/AGENTS.md`: build and test, download both website parts, generate Markdown, publish and push `vczh-libraries.github.io`, wait for its CI, and verify the live pages. Then publish the generated Markdown into Tools and synchronize it to the source repositories. Where that workflow names the absent `job.monorepo.copilotInitAll.prompt.md`, use the current `Tools/Jobs/job.Windows.copilotInitAll.prompt.md` equivalent.
- Inspect GacJS `doc/NetworkProtocol.md`, `doc/Testing_Protocol.md`, the RPC documentation, and the final import/code-generation diff. The changed C++ callback ownership does not alter the wire protocol, and no stale callback signature is currently present in those GacJS documents, so do not manufacture a GacJS document or test-website change. If a separate observable GacJS behavior requires a website change, follow its repository instructions and publish it immediately as originally requested.
- Commit and push only task-owned changes in every affected repository. Preserve unrelated pre-existing work, rebase onto concurrent remote changes as required by the job prompts, and rerun verification only when rebased source changes invalidate it.

## VERIFICATION

1. Confirm the release chain before testing:
   - Both the root and `IncludeOnly` VlppOS release amalgamations match the current owning-`Ptr` source API.
   - `Workflow/Import` matches the current VlppOS release, and `Workflow/Release` is regenerated only after Workflow passes.
   - `GacUI/Import` matches the current VlppOS and Workflow releases, and the regenerated root and `IncludeOnly` `GacUI.UnitTest` and `Test.RemotingHelpers` pairs contain the corrected signatures.
   - No generated file was repaired by hand.
2. Require the complete Workflow gate prescribed by `Workflow/Project.md` after its `.h` and `.cpp` repairs. In addition, explicitly require the native stdio result from:
   - `& C:\Code\VczhLibraries\Workflow\Test\StartRpcStdio.ps1 -SkippedTestCaseListFile C:\Code\VczhLibraries\Workflow\Test\StartRpcStdio_SharedMemspSkipList.txt -Configuration Release -Platform x64`
   - The authoritative result is dynamic: every `IndexRpc.txt` case not named by the exact shared-memory skip list runs once, every skipped name matches that list, every actual result equals its expected result, and the process exits zero. At review time this is 126 passes and 3 `*_SharedMemsp` skips out of 129 cases; all destructor cases must run.
3. From `GacJS/Gaclib`, run `yarn run import`, then `yarn codegen` twice. Require the second code-generation run to produce no further diff before running `yarn build` and `yarn test`. Finally, run `& C:\Code\VczhLibraries\GacJS\Gaclib\StartRpcStdio.ps1` explicitly; that script does not replace the import, code-generation, or test phases.
   - Require zero exit, one terminal result for every indexed case, and the exact dynamic partition defined by `StartRpcStdio_DtorSkipList.txt`. At review time this is 118 passes and 11 intentional destructor/shared-memory skips out of 129 cases. Provider stdout must remain protocol-only, and there must be no crash, hang, unexpected skip, or missing case.
4. Require both the Release GacUI gate and the supported Debug x64 prebuild and `UnitTest` after importing and repairing source. For the supported Debug run, `Build.log` must report success with zero warnings and zero errors, and `Execute.log` must report every selected test file and case passed with no CRT leak dump after the summary. Require the platform-specific builds prescribed by `job.rpXPlat.prompt.md` before any Linux or macOS row.
5. Execute every included matrix row through the complete normal `/RVMT` section and the `RemotingTest_Core /RVMT` requirements of the fatal regression addendum in `DebugRemoteProtocolSop.md`. The explicitly excluded standalone `CppTest_Rvm` rows and their requester-specific fatal addendum are not covered and must not be reported as verified:
   - Wait for the exact readiness condition for the selected host mode. Process creation or `GACJS_RVMHOST_SERVICE_HELD` alone is not Node network readiness; require exact `GACJS_RVMHOST_READY`. A stdio host is owned by Core and must not also be started manually.
   - Require the exact title `Remote View Model Test`, the initial `Hello, !`, and exact `Hello, <marker>!` after typing a unique marker through the visible renderer.
   - In every GacJS row, replace only the renderer, keep the accepted host running, and require the replacement renderer plus a subsequent `Translate` call to succeed without reconnecting or replacing that host.
   - For every non-CLI host mode, start a second same-mode host, require it to be rejected without replacing the accepted host, and prove the original host still serves another `Translate` call. Do not claim second-stdio-host coverage for `/Cli` rows.
   - Close normally through the active visible surface and require no fatal error, retry loop, leaked child, process, listener, or prompt. For each GacJS Node SEA `/Cli` row, POST exact `!Exit` to Core automation and require the SEA child to be reaped; force-killing Core does not verify graceful stdio shutdown.
   - In fresh sessions, run both accepted-host-loss variants required by the SOP. Require exactly one Core-authored `ErrorChannel` package carrying exact `RemotingTest_RvmHost disconnected.` before nonzero Core termination, plus the renderer-specific fatal observation, bounded completion, and complete cleanup. A renderer-local transport error alone is insufficient.
6. For website and knowledge-base work, require WebsiteSource build/test and publication commands to succeed, generated Markdown to contain all new owning-`Ptr` signatures, the five affected Tools knowledge-base pages to be current, publication CI to pass, and the three live public pages to show the updated contract.
7. Review `git status` and diffs in every touched repository. Commit task-owned changes with meaningful messages, pull/rebase if required, push each current branch, and confirm every pushed branch contains the verified commits with a clean task-owned working tree.

## REVIEW COMMENTS
