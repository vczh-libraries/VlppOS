# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

I don't think you need to test TUI under web assembly in VlppOS repro, as TUI has no plan to run with web assembly anyway. Please keep Source\TUI\TUI* still under `#if defined VCZH_MSVC || defined VCZH_GCC` since you removed these guards in the last VlppOS commit. Also remove added VCZH_WASM specific test in TestTui.cpp. If this is caused by the GacUI TUI unit test (if any), then those should be guarded away from web assembly.

# TEST [CONFIRMED]

Source inspection reproduces the scope error in VlppOS commit `970e4c0`: TUI.h, TUI.Internal.h, TUI.cpp and TUITypes.h lost their native-only guards, TUI.Linux.cpp acquired a Wasm MeasureChar branch, and TestTui.cpp acquired two Wasm-only cases. The previous completed browser run exercised those cases (109 cases / 11 files). GacUI TestTuiProvider.cpp is unguarded and uses an injected VlppOS TUI backend; its completed browser log also records execution. These existing tests and their inclusion are the reproduction; adding another Wasm TUI test would conflict with this request.

Success requires all TUI implementation and provider tests to remain native-only, without Wasm stubs, constructors or width logic added solely for TUI. Shared GacUI coordinate, key and input declarations must retain one owner and unchanged definitions. Run the complete VlppOS native and browser suites, native GacUI TestTuiProvider.cpp and the complete GacUI browser suite. Browser output must omit both TUI test files entirely and complete once with return zero and no browser errors. Build and exercise the native TuiPlayground through a real PTY. Regenerate releases and check downstream import consistency. Inspect the final native TUI source against its pre-Wasm version.

# PROPOSALS
