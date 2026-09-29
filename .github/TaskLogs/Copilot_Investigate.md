# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

I don't think you need to test TUI under web assembly in VlppOS repro, as TUI has no plan to run with web assembly anyway. Please keep Source\TUI\TUI* still under `#if defined VCZH_MSVC || defined VCZH_GCC` since you removed these guards in the last VlppOS commit. Also remove added VCZH_WASM specific test in TestTui.cpp. If this is caused by the GacUI TUI unit test (if any), then those should be guarded away from web assembly.

# TEST [CONFIRMED]

Source inspection reproduces the scope error in VlppOS commit `970e4c0`: TUI.h, TUI.Internal.h, TUI.cpp and TUITypes.h lost their native-only guards, TUI.Linux.cpp acquired a Wasm MeasureChar branch, and TestTui.cpp acquired two Wasm-only cases. The previous completed browser run exercised those cases (109 cases / 11 files). GacUI TestTuiProvider.cpp is unguarded and uses an injected VlppOS TUI backend; its completed browser log also records execution. These existing tests and their inclusion are the reproduction; adding another Wasm TUI test would conflict with this request.

Success requires all TUI implementation and provider tests to remain native-only, without Wasm stubs, constructors or width logic added solely for TUI. Shared GacUI coordinate, key and input declarations must retain one owner and unchanged definitions. Run the complete VlppOS native and browser suites, native GacUI TestTuiProvider.cpp and the complete GacUI browser suite. Browser output must omit VlppOS TestTui.cpp and GacUI TestTuiProvider.cpp/TestControls_EasyLayout.cpp entirely and complete once with return zero and no browser errors. Build and exercise the native TuiPlayground through a real PTY. Regenerate releases and check downstream import consistency. Inspect the final native TUI source against its pre-Wasm version.

# PROPOSALS

- No.1 Restore native-only TUI and separate shared window input types [CONFIRMED]

## No.1 Restore native-only TUI and separate shared window input types

Restore the VlppOS TUI files and TestTui.cpp to their versions before `970e4c0`, removing all Wasm-only terminal support and compatibility edits. GacUI's general UI code depends on the coordinates, VKEY and input payloads currently declared in TUITypes.h, independently of its terminal provider. Move these declarations unchanged to Source/WindowTypes.h, with a normal inclusion guard and no platform restriction. Keep TUITypes.h as a native-only forwarding header so existing native includes still work and every Source/TUI/TUI* file retains its native guard. Register WindowTypes.h in both owning Visual Studio project/filter inventories; CodePack will publish its declarations through VlppOS.h.

Guard GacUI's TuiController, TuiWindow, TuiGraphics, TuiGraphicsRenderers and TuiTextLayout declarations/implementations and TestTuiProvider.cpp with the same native platform condition. Keep renderer-independent reflected types, resources and skin declarations available to the resource compiler; none supplies a terminal backend on Wasm. Preserve the shared ITuiApplication accessor, which ordinary GUI initialization and reflection already reference and which remains null without a native terminal provider.

### CODE CHANGE

Restore TUI.h, TUI.Internal.h, TUI.cpp, TUI.Linux.cpp and TestTui.cpp from the parent of `970e4c0`; replace TUITypes.h with the guarded include of the unchanged declarations in WindowTypes.h. Add native guards to the nine GacUI provider files and its test. Update the TUI specification and both Project.md files to state browser exclusions and the new shared declaration owner. Regenerate VlppOS and GacUI releases with CodePack and copy the changed VlppOS release artifacts to the five existing downstream Import directories. Verify native behavior, browser exclusion and generated-file consistency before confirming the proposal.

Source review also found TUI.Windows.cpp using a negative compiler guard and a failing fallback. Replace it with the positive VCZH_MSVC guard required by the shared coding guideline, including its platform-only implementation. This keeps inactive platform sources harmless and does not change the Windows branch.

The dependency audit found eight additional TUI-backed cases in GacUI TestControls_EasyLayout.cpp: every case calls tui_provider_tests::RunGuiTest directly or through RunResourceTest, installing TuiGraphicsResourceManager and a live injected TUI controller. Guard that entire file with the same native condition, retain its native verification, and require its absence in browser output. There is no independent browser path for these tests.

The first GacUI Wasm link reported undefined easy_layout_xml_tests::Resource: TestResource.cpp reuses the XML text builder from TestControls_EasyLayout.cpp. Keep that unchanged platform-independent helper outside the native guard, while all eight TEST_CASE registrations and TUI setup remain guarded. This preserves compiler-error coverage without introducing a browser TUI path.

### CONFIRMED

The terminal APIs, implementations and test backends are excluded from WebAssembly. The shared input declarations remain available to general GacUI code through WindowTypes.h, with unchanged definitions, names, defaults and key values. TUITypes.h remains a native-only forwarding header. TUI.h, TUI.Internal.h, TUI.cpp and TUI.Linux.cpp match their pre-Wasm versions byte for byte; TestTui.cpp likewise restores its original native-only contents. TUI.Windows.cpp retains its implementation inside a positive Windows guard.

| Verification | Passed files | Passed cases |
| --- | --- | --- |
| VlppOS, complete native Linux/Clang suite | 15/15 | 279/279 |
| VlppOS, complete Firefox/Wasm suite | 10/10 | 107/107 |
| GacUI, native TUI provider and EasyLayout tests | 2/2 | 27/27 |
| GacUI, complete Firefox/Wasm suite | 91/91 | 1786/1786 |

The browser runs use the generated production launcher, isolated Firefox workers and the existing pthread pools. Both complete exactly once with `wasm_main returns 0.` and no browser errors. VlppOS omits TestTui.cpp and removes exactly its two Wasm-only cases (109 to 107); its pthread console title, Unicode output and color checks continue to pass. The GacUI file inventory exactly matches the previous complete run minus TestTuiProvider.cpp and TestControls_EasyLayout.cpp. Its count drops by exactly their 19 + 8 cases (1813 to 1786), with no other test-file exclusions. All 96 prefilled GacUI fixtures are consumed. TestResource.cpp's EasyLayout compiler checks remain enabled through the shared XML helper.

Wasm symbol inspection finds only debug metadata in the VlppOS TUI implementation/TestTui objects and in GacUI's five terminal-provider implementation objects and TestTuiProvider object. TestControls_EasyLayout retains the portable XML builder, while its test registration and TUI setup are absent. These are compile-time exclusions, not runtime file filters.

Builds use each repository's absolute .github/Ubuntu/build.sh, with incremental Wasm -bw and full native -f modes. Large builds are serialized with MAKEFLAGS=-j2 and BINARYEN_CORES=2. Native GacUI verification uses an isolated checkout at 7e19be81b; later GacUI changes are documentation only. Native tests run with /C; the GacUI native run selects the two affected files. The browser entry retains its complete unfiltered suite.

TuiPlayground builds and passes a real Linux PTY exercise using TERM=xterm-256color, COLORTERM=truecolor and LC_ALL=C.UTF-8. It starts at 100 by 30 cells, resizes to 20 by 8 and back, and exercises Unicode, RGB/style output, wide-cell overwrite and navigation. The process exits zero, emits alternate-screen restoration and restores termios exactly. This checks terminal bytes and operations, not visible font effects or the full visual SOP. Windows and macOS were not executed in this investigation.

CodePack regenerates VlppOS and GacUI releases. All VlppOS imports in VlppRegex, VlppReflection, VlppParser2, Workflow and GacUI match their owning release files byte for byte. Project/filter XML parses, source inventories retain the inactive platform files, and whitespace checks pass. Project.md, the TUI specification/index and the shared-input learning note describe the native-only boundary and new declaration owner. wGac and iGac remain unchanged.

Select No.1, the sole confirmed proposal. Code review is complete. The implementation was committed and pushed before verification at the user's request; no uncommitted implementation changes remain to revert or reapply. The initial link failure was resolved by keeping the shared XML helper outside the native guard, without restoring browser TUI support. Verification logs are retained in ~/.cache/vlpp-native-tui.
