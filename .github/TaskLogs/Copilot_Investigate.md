# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

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


# UPDATES

# TEST [CONFIRMED]

Confirm the stale-release and downstream-compatibility problem structurally, then verify the repaired ownership contract through the complete generated-artifact and consumer chain.

- Compare the current `VlppOS/Source` connection-admission signatures with both root and `IncludeOnly` `VlppOS/Release` amalgamations. The released signatures must use the producer's existing `Ptr` at the async-socket, HTTP-request, network-protocol, and channel layers, with no raw-pointer-to-`Ptr` reconstruction.
- After each documented Tools build, inspect the generated diff and imported snapshots. `Workflow/Import` must match the current VlppOS release; `GacUI/Import` must match the current VlppOS and Workflow releases; regenerated GacUI root and `IncludeOnly` helper pairs must contain the owning signatures. Generated files must only be changed by their generators/importers.
- Build and test Workflow through its complete `Project.md` gate and explicitly run `StartRpcStdio.ps1` with the exact shared-memory skip list. Every `IndexRpc.txt` case must have exactly one terminal result, every non-skipped case must pass, every skipped case must be named by the supplied list, all destructor cases must run, and the process must exit zero.
- In `GacJS/Gaclib`, run import, run code generation twice and require the second run to be idempotent, then run build, tests, and the explicit stdio suite. The stdio result must exactly partition the indexed cases according to `StartRpcStdio_DtorSkipList.txt`, keep provider stdout protocol-only, and have no crash, hang, unexpected skip, or missing case.
- Run the GacUI Release gate, then the prescribed Debug x64 build and `UnitTest`. Require zero warnings/errors, all selected tests passed, and no CRT leak dump. Run the platform builds required by the job prompt before any cross-platform row.
- Recreate the matrix cards from `job.rpWindows.prompt.md` and `job.rpXPlat.prompt.md`, leave excluded rows blank, and execute every applicable `RemotingTest_Core /RVMT` row. Each row must satisfy the full ordinary interaction, renderer replacement, duplicate-host rejection where applicable, graceful shutdown, both accepted-host-loss variants, exact Core-authored fatal package, bounded completion, and cleanup requirements in `DebugRemoteProtocolSop.md`.
- Update the three canonical public-document sources and publish the site through the WebsiteSource workflow. Require successful build/test/generation/publication, passing publication CI, updated live pages, five current Tools knowledge-base pages, and synchronized generated Markdown in source repositories. Audit GacJS documentation and do not introduce changes where the C++ ownership-only contract does not affect the wire protocol.
- Finish with clean task-owned working trees, reviewed diffs, and pushed verified commits in every affected repository.

The problem is confirmed before regeneration: current VlppOS source declarations already use `Ptr<IAsyncSocketConnection>`, `Ptr<IHttpRequestConnection>`, `Ptr<INetworkProtocolConnection>`, and `Ptr<IChannelClient<TPackage>>`, while the tracked root release still exposes the corresponding raw-pointer callbacks and forwards `.Obj()`. The specified Workflow and GacUI source overrides also still use raw callback parameters. This proves the generated release/import surfaces and downstream consumers have not yet propagated the completed ownership change.

# PROPOSALS

- No.1 Regenerate the owning API and repair every downstream source consumer

## No.1 Regenerate the owning API and repair every downstream source consumer

Use the repository generators and importers as the only path for propagating the completed VlppOS ownership contract. First run the documented Tools release gate for VlppOS and audit all root and `IncludeOnly` amalgamations. Then update the Workflow source overrides to accept the owning local-client `Ptr`, run the complete Workflow gate and explicit stdio suite, and allow its release to regenerate only after those checks pass. Import both verified releases through the GacUI Tools gate after updating the two GacUI source overrides, and audit the regenerated helper pairs.

Exercise the resulting contract in the actual consumers rather than treating compilation as sufficient: run the GacJS import/code-generation/build/test/stdio sequence, GacUI Debug x64 unit tests, every included Windows and current cross-platform `/RVMT` combination, and the ordinary and fatal regression behaviors required by the current job prompts and SOP. Preserve the accepted host's ownership across renderer replacement, reject duplicate non-CLI hosts without displacing it, and verify graceful and fatal teardown.

Update the three canonical website XML sources to describe the owning callback arguments and unchanged wire protocol, publish and verify the live site, regenerate the Markdown knowledge base, and synchronize the five affected Tools/source-repository pages through the documented publication workflow. Audit GacJS documentation without changing it unless an independently observable wire or behavior change is found.

This proposal preserves the producer's existing reference counter end to end, avoids forbidden raw-pointer `Ptr` reconstruction, keeps generated artifacts attributable to their generators, and uses every downstream runtime boundary named in the request as evidence.

### CODE CHANGE

- Regenerated the VlppOS root release amalgamations through `Tools/Tools/Build.ps1 VlppOS`; the owning `Ptr` callback signatures and handoffs now match source, while the generated `IncludeOnly` mirrors continue to include the corrected source declarations and definitions.
- Updated the three Workflow source overrides to receive `Ptr<JsonChannelClient>` and preserved that pointer. Existing `dynamic_cast` identity checks use `.Obj()` only at the raw-pointer operation; no new reference counter is constructed.
- The complete Workflow gate passed and regenerated its release after importing VlppOS. The explicit Release x64 stdio run produced one terminal result for all 129 indexed cases: 126 passed and the exact three `*_SharedMemsp` cases were skipped; every destructor case ran.
- Updated the two GacUI source overrides to receive the owning callback `Ptr`, then imported the verified VlppOS and Workflow releases through the complete GacUI gate. The generated `GacUI.UnitTest` and `Test.RemotingHelpers` release artifacts contain the corrected overrides. The supported Debug x64 build reported success with zero warnings and zero errors, and UnitTest passed all 89 selected files, 1714 cases, with no CRT leak dump.
- In GacJS, import and two consecutive code-generation runs completed with the identical generated-diff hash `1dabe277a127453c3ecb7db55d99c59724ffeb52`. All nine packages built, all ten package test projects passed (including 53 website tests), and the explicit stdio suite produced one terminal result for every indexed case: 118 passed and the exact 11 configured destructor/shared-memory cases were skipped. The generated protocol snapshots remained unchanged and the GacJS working tree is clean; its wire-protocol documentation therefore requires no manufactured update.
- Recreated and completed the Windows native-renderer `/RVMT` card for `/Pipe`, `/Http`, and `/MiniHttp`, each with network and native stdio hosts (six rows). Every row passed the ordinary greeting/translation flow, duplicate-host rejection where applicable, graceful exit, idle-next-call host loss, and delivery-acknowledgement host loss with the exact Core-authored fatal error and cleanup. The separate `CppTest_Rvm`, `/RPT`, and `/FCT` rows remain intentionally blank.
- Recreated and completed the Windows GacJS-renderer `/RVMT` card for `/Http` and `/MiniHttp`, each with native network, native stdio, browser, Node network, and SEA stdio hosts (ten rows). Every included row passed renderer replacement while retaining the accepted host, a subsequent `Translate`, duplicate-host rejection where applicable, graceful exit, and both host-loss variants. Browser delivery loss was isolated with a temporary generated-host integration test that stopped the accepted browser host after delivery and before replacement acknowledgement; both transports passed, and the temporary test was removed. `/RPT` and `/FCT` rows remain intentionally blank.
- Updated the three public XML articles with the owning callback contract, built and tested WebsiteSource (57 tests), downloaded both static site parts, and generated Markdown. The published site diff was restricted to the three intended pages after excluding line-ending-only regeneration noise; site commit `d4c11562a` is pushed to `master`.
- Cross-platform native and GacJS matrices were not claimed: this run is on Windows, and the Linux `wGac` and macOS `iGac` rows require their matching platform hosts.
