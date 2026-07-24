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

`TuiPlayground` is an interactive ASCII-art painter. Type one command in the dark-gray box at the bottom and press Enter to add it to the paper:

- `FC RRGGBB` changes the foreground color; the initial foreground is `FFFFFF`.
- `BC CLEAR` preserves destination backgrounds for later lines and rectangles; `BC RRGGBB` replaces them. The initial background mode is `CLEAR`.
- `LINEV THIN|THICK|DOUBLE x y1 y2` draws a vertical line.
- `LINEH THIN|THICK|DOUBLE x1 x2 y` draws a horizontal line.
- `RECT THIN|THICK|DOUBLE|ROUND x1 y1 x2 y2` draws a rectangle. `ROUND` means a thin line with rounded corners.
- `CLEAR RRGGBB x1 y1 x2 y2` clears a rectangle to the specified background.
- `TYPE x y:TEXT` draws the payload without wrapping and preserves it exactly, including case, spaces, additional colons, and supplementary Unicode characters.

Command names, formats, `CLEAR`, and hexadecimal digits are case-insensitive. Parsing is otherwise strict: use exactly one ASCII space at each displayed separator, six hexadecimal digits, signed decimal coordinates in the platform `vint` range, and ordered ranges. Logical paper `(0,0)` appears at terminal `(1,1)` inside the double-line border; signed off-paper coordinates are accepted and clipped.

The command box wraps complete Unicode scalars by display width, grows upward as needed, keeps its blinking cursor visible, and supports only Backspace editing. Enter submits and clears the box. A parse error instead shows the original command and reason in a centered rounded overlay; all typing is ignored until Enter dismisses it. `q` and `Q` are ordinary command text. Escape is the only exit key.

- Windows: from `REPO-ROOT/Test/UnitTest`, run `& REPO-ROOT/.github/Scripts/copilotBuild.ps1`, then run `& REPO-ROOT/.github/Scripts/copilotExecute.ps1 -Mode CLI -Executable TuiPlayground` in an interactive console.
- Linux: from `REPO-ROOT/Test/Linux/TuiPlayground`, run the absolute `REPO-ROOT/.github/Ubuntu/build.sh`, then run `./Bin/TuiPlayground` in an interactive terminal.
- macOS: from `REPO-ROOT/Test/Linux/TuiPlayground`, run the absolute `REPO-ROOT/.github/Ubuntu/build.sh`, then run `./Bin/TuiPlayground` in an interactive terminal.

For manual verification, combine all commands and styles, overlap them, use `BC CLEAR` and `BC 000000`, type width-one/width-two and supplementary characters, and submit malformed commands. Resize larger and smaller after drawing: the border, wrapped command box, error overlay, and replayed paper must follow the visible terminal without scrolling. On Windows, start with a scrollback buffer taller than the viewport and require no vertical scrollbar while TUI is active. Press Escape and require the original buffer/window geometry and terminal state to be restored.

When any *.h or *.cpp file is changed, unit test is required to run.
When shared product source changes, all relevant unit tests are required to run.

When any test case fails, you must fix the issue immediately, even those errors are unrelated to the issue you are working on.

## Linux/macOS Specific

- `REPO-ROOT/Test/Linux/UnitTest` stores the Unix configuration for `UnitTest.vcxproj`.
- `REPO-ROOT/Test/Linux/MiniHttpServer` stores the Unix configuration for `MiniHttpServer.vcxproj`.
- `REPO-ROOT/Test/Linux/TuiPlayground` stores the Unix configuration for `TuiPlayground.vcxproj`.

You need to build, run, test, and debug each project in its matching folder, otherwise it will not function properly.
On Linux and macOS, only configuration "debug x64" is available, no need to build or run projects with other configurations.
