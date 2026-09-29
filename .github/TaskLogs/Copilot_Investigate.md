# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

I don't think you need to test TUI under web assembly in VlppOS repro, as TUI has no plan to run with web assembly anyway. Please keep Source\TUI\TUI* still under `#if defined VCZH_MSVC || defined VCZH_GCC` since you removed these guards in the last VlppOS commit. Also remove added VCZH_WASM specific test in TestTui.cpp. If this is caused by the GacUI TUI unit test (if any), then those should be guarded away from web assembly.

# TEST [CONFIRMED]

Source inspection reproduces the scope error in VlppOS commit `970e4c0`: TUI.h, TUI.Internal.h, TUI.cpp and TUITypes.h lost their native-only guards, TUI.Linux.cpp acquired a Wasm MeasureChar branch, and TestTui.cpp acquired two Wasm-only cases. The previous completed browser run exercised those cases (109 cases / 11 files). GacUI TestTuiProvider.cpp is unguarded and uses an injected VlppOS TUI backend; its completed browser log also records execution. These existing tests and their inclusion are the reproduction; adding another Wasm TUI test would conflict with this request.

Success requires all TUI implementation and provider tests to remain native-only, without Wasm stubs, constructors or width logic added solely for TUI. Shared GacUI coordinate, key and input declarations must retain one owner and unchanged definitions. Run the complete VlppOS native and browser suites, native GacUI TestTuiProvider.cpp and the complete GacUI browser suite. Browser output must omit both TUI test files entirely and complete once with return zero and no browser errors. Build and exercise the native TuiPlayground through a real PTY. Regenerate releases and check downstream import consistency. Inspect the final native TUI source against its pre-Wasm version.

# PROPOSALS

- No.1 Restore native-only TUI and separate shared window input types

## No.1 Restore native-only TUI and separate shared window input types

Restore the VlppOS TUI files and TestTui.cpp to their versions before `970e4c0`, removing all Wasm-only terminal support and compatibility edits. GacUI's general UI code depends on the coordinates, VKEY and input payloads currently declared in TUITypes.h, independently of its terminal provider. Move these declarations unchanged to Source/WindowTypes.h, with a normal inclusion guard and no platform restriction. Keep TUITypes.h as a native-only forwarding header so existing native includes still work and every Source/TUI/TUI* file retains its native guard. Register WindowTypes.h in both owning Visual Studio project/filter inventories; CodePack will publish its declarations through VlppOS.h.

Guard GacUI's TuiController, TuiWindow, TuiGraphics, TuiGraphicsRenderers and TuiTextLayout declarations/implementations and TestTuiProvider.cpp with the same native platform condition. Keep renderer-independent reflected types, resources and skin declarations available to the resource compiler; none supplies a terminal backend on Wasm. Preserve the shared ITuiApplication accessor, which ordinary GUI initialization and reflection already reference and which remains null without a native terminal provider.

### CODE CHANGE

Restore the five changed TUI implementation/header files and TestTui.cpp from the parent of `970e4c0`; replace TUITypes.h with the guarded include of the unchanged declarations in WindowTypes.h. Add native guards to the nine GacUI provider files and its test. Update the TUI specification and both Project.md files to state browser exclusions and the new shared declaration owner. Regenerate VlppOS and GacUI releases with CodePack and copy the changed VlppOS release artifacts to the five existing downstream Import directories. Verify native behavior, browser exclusion and generated-file consistency before confirming the proposal.

Source review also found TUI.Windows.cpp using a negative compiler guard and a failing fallback. Replace it with the positive VCZH_MSVC guard required by the shared coding guideline, including its platform-only implementation. This keeps inactive platform sources harmless and does not change the Windows branch.

The dependency audit found eight additional TUI-backed cases in GacUI TestControls_EasyLayout.cpp: every case calls tui_provider_tests::RunGuiTest directly or through RunResourceTest, installing TuiGraphicsResourceManager and a live injected TUI controller. Guard that entire file with the same native condition, retain its native verification, and require its absence in browser output. There is no independent browser path for these tests.

The first GacUI Wasm link reported undefined easy_layout_xml_tests::Resource: TestResource.cpp reuses the XML text builder from TestControls_EasyLayout.cpp. Keep that unchanged platform-independent helper outside the native guard, while all eight TEST_CASE registrations and TUI setup remain guarded. This preserves compiler-error coverage without introducing a browser TUI path.
