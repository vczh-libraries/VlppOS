# General Instruction

## Solution to Work On

You are working on the solution `REPO-ROOT/Test/UnitTest/UnitTest.sln`,
therefore `SOLUTION-ROOT` is `REPO-ROOT/Test/UnitTest`.

## Projects for Verification

The solution contains:
- `REPO-ROOT/Test/UnitTest/UnitTest/UnitTest.vcxproj`, the unit test project.
- `REPO-ROOT/Test/UnitTest/MiniHttpServer/MiniHttpServer.vcxproj`, the portable CLI/browser verification project for `SocketHttpServerApi`.
- `REPO-ROOT/Test/UnitTest/TuiPlayground/TuiPlayground.vcxproj`, the portable interactive TUI verification project.

Run the browser verification project as `MiniHttpServer <WebsiteFolder> <AssetsFolder>`.

### MiniHttpServer Browser Verification

- Windows: from `REPO-ROOT/Test/UnitTest`, set the Debug x64 arguments to `"..\MiniHttpServer\Website" "..\MiniHttpServer\Assets"`, build and run through `copilotBuild.ps1` and `copilotExecute.ps1`, and drive Chrome with browser control.
- Linux and macOS: from `REPO-ROOT/Test/Linux/MiniHttpServer`, run the absolute `REPO-ROOT/.github/Ubuntu/build.sh`, then run `./Bin/MiniHttpServer ../../MiniHttpServer/Website ../../MiniHttpServer/Assets`; drive Firefox on Linux and Safari on macOS with browser control.
- Open `http://localhost:8888/`; expect the styled page and SVG, `Module status: loaded from Assets.`, `Fetch status: cross-origin JSON loaded from Assets.`, and no console or CORS errors.
- Click the button and expect `Button status: action handled by Assets module.`; open the second page and return, expecting the module and fetch statuses to succeed.
- Open `http://localhost:8889/Assets` and expect the Assets index; expect `http://localhost:8889/app.js` and `http://localhost:8889/AssetsExtra/app.js` not to be served.
- Press Enter and expect a clean exit with ports 8888 and 8889 released.

### TuiPlayground Verification

Before changing TUI or TuiPlayground, read [the TUI specification](.github/KnowledgeBase/KB_VlppOS_TerminalUserInterface.md) and [the playground SOP](.github/Jobs/DebugTuiPlaygroundSOP.md).

GacUI consumes shared `vl::presentation` input types from `Source/TUI/TUITypes.h`. Check downstream compatibility when changing declarations, defaults, key values or event semantics, and regenerate/verify imports when required.

Windows TUI development targets Windows 10 or newer. Use virtual-terminal output for true color and text styles; their visual appearance depends on the terminal and font.
For visual verification of all four text styles, follow the SOP's Windows Terminal setup; Windows 10's built-in console host does not guarantee those effects even when VT and true-color output are available.

Follow [DebugTuiPlaygroundSOP.md](.github/Jobs/DebugTuiPlaygroundSOP.md) for production launch, typing, navigation, history, shape preview, resize and terminal-restoration checks. [The TUI specification](.github/KnowledgeBase/KB_VlppOS_TerminalUserInterface.md) defines the shared input and rendering contracts.

When any *.h or *.cpp file is changed, unit test is required to run.
When shared product source changes, all relevant unit tests are required to run.

When any test case fails, you must fix the issue immediately, even those errors are unrelated to the issue you are working on.

## Linux/macOS Specific

- `REPO-ROOT/Test/Linux/UnitTest` stores the Unix configuration for `UnitTest.vcxproj`.
- `REPO-ROOT/Test/Linux/MiniHttpServer` stores the Unix configuration for `MiniHttpServer.vcxproj`.
- `REPO-ROOT/Test/Linux/TuiPlayground` stores the Unix configuration for `TuiPlayground.vcxproj`.

You need to build, run, test, and debug each project in its matching folder, otherwise it will not function properly.
On Linux and macOS, only configuration "debug x64" is available, no need to build or run projects with other configurations.

## WebAssembly

Only `Test/Linux/UnitTest` opts in through `WASM=YES` in its `vbuild` file. From that folder, run `../../../.github/Ubuntu/build.sh -bw` (incremental) or `-fbw` (full), then `./Bin/app.sh`. Open `http://127.0.0.1:8888/`; pass a port to `app.sh` to change it. Node.js serves the generated files with the COOP/COEP headers required for pthread shared memory.

The suite runs in a Web Worker using 32 preloaded pthread workers. Threading, locale string operations, memory streams, serialization and encoding remain tested. Native filesystem, concrete inter-process transports and TUI are unavailable; accessing the filesystem fails immediately. The requested filesystem-dependent test files and two file-stream cases are excluded from Wasm.

Require all retained cases to pass and exactly one `wasm_main returns 0.` line. Run the native UnitTest suite as well when changing shared source. `Bin/UnitTest` is a copy of the Wasm module in this mode, not a native executable. The Linux release includes both shared POSIX code and guarded Wasm implementations.
