investigate repro

# Browser test application for SocketHttpServerApi

After [TODO_Task_MiniHttpApi.md](./TODO_Task_MiniHttpApi.md) is complete, add a portable `MiniHttpServer` CLI project and deterministic website fixtures to verify `vl::inter_process::async_tcp_socket::SocketHttpServerApi` in real browsers.

`MiniHttpServer` is the project/executable name. Do not invent a `MiniHttpServerApi` product class; consume `SocketHttpServerApi` exactly as implemented by the prerequisite task.

## Required outcome

- Add `MiniHttpServer.vcxproj` to `Test/UnitTest/UnitTest.sln`.
- Launch the executable with two physical-folder arguments.
- Host the first folder at normalized prefix `http://localhost:8888`.
- Host the second folder at normalized prefix `http://localhost:8889/Assets`.
- Put every JavaScript file in the second folder so the page on port 8888 loads and calls cross-origin resources on port 8889.
- Call browser control to verify the website in Chrome on Windows, Firefox on Linux, and Safari on macOS. A native HTTP client, `curl`, raw sockets, code inspection, or merely shell-opening a URL does not satisfy the browser gate.
- Move the existing `Test/Linux` UnitTest configuration into `Test/Linux/UnitTest` and add `Test/Linux/MiniHttpServer` for Linux and macOS builds.
- Update `Project.md` as part of executing this future task. Do not treat the current creation of this TODO document as authorization to update `Project.md` early.

The feature referred to as “XSS” in the request is cross-origin/CORS behavior. Do not create an XSS vulnerability. Use a module script and a cross-origin fetch so the browser must enforce CORS.

## Project and solution layout

Create:

```text
Test/UnitTest/MiniHttpServer/
|- MiniHttpServer.vcxproj
|- MiniHttpServer.vcxproj.filters
`- Main.cpp
```

Add the project to `Test/UnitTest/UnitTest.sln` with a new project GUID and all four `Debug|Win32`, `Debug|x64`, `Release|Win32`, and `Release|x64` active/build mappings.

Follow `.github/Guidelines/SourceFileManagement.md`: copy the existing UnitTest project as the configuration baseline, then remove unit-test-only sources and enumerate every retained product/source file explicitly. Wildcards are forbidden.

- Keep standard `Header Files`, `Resource Files`, and `Source Files` filters. Header Files may remain empty as required by repository convention.
- Compile the portable VlppOS file/stream/threading, async-socket request, and MiniHttp API sources needed by the application. Include Windows/Linux/macOS native socket sources with the same platform exclusions used by UnitTest.
- Define `VCZH_DEBUG_NO_REFLECTION` in every configuration and `VCZH_CHECK_MEMORY_LEAKS` in Debug configurations.
- List website fixtures as explicit `None` items under `Resource Files` for discoverability, but do not embed or copy them into the binary. Runtime roots must come from the CLI.
- Do not commit `MiniHttpServer.vcxproj.user`.

## CLI and server lifecycle

Use this command-line contract:

```text
MiniHttpServer <WebsiteFolder> <AssetsFolder>
```

- Require exactly two arguments and verify that both resolve to existing folders. Print a short usage/error message and return nonzero on invalid input.
- Derive a small physical-folder handler from `SocketHttpServerApi`. Construct one instance with `http://localhost:8888` for the Website argument and another with `http://localhost:8889/Assets` for the Assets argument, passing `respondToOptions = true` to both so the mandatory browser preflight is handled.
- Start both APIs, print the two ready URLs, and wait for an explicit Enter/stop command. Do not busy-wait.
- On exit, stop both APIs in reverse startup order, drain callbacks, release resources, call `FinalizeGlobalStorage`, and return zero. The derived physical-folder handler's destructor must also call `Stop` before its fields are destroyed, even though normal `main` cleanup already stopped it.
- Ensure Ctrl+C, application errors, and partial startup do not leave either port bound.

The app uses two different ports and therefore does not need to retest same-port sharing. Same-port listener sharing is covered by the prerequisite product unit tests.

## Physical-folder handler belongs in Main.cpp

Implement the tiny static handler in the application, not in `SocketHttpServerApi` or another product source file.

- Map an exact prefix or relative `/` to `index.html`. A relative path ending in `/` may map to that directory's `index.html`; other paths map to regular files below the supplied root.
- Use the context's already UTF-8-decoded prefix-relative path and ignore its separate raw query for file lookup. Normalize platform separators, reject NUL, separators/traversal, and require the result to stay below the physical root; never decode a second time.
- Support browser `GET` and `HEAD`. Return `404` for missing paths and never generate a directory listing.
- Read exact file bytes. Construct `async_tcp_socket::HttpResponse` in application code with status, reason, fields, body, and correct `Content-Type`.
- Provide only the fixture metadata needed here: HTML, CSS, JavaScript modules, JSON, SVG, and an `application/octet-stream` fallback. This mapping is application policy and must not be moved into MiniHttp product APIs.
- Let `SocketHttpServerApi` supply its normal CORS, `OPTIONS`, framing, `HEAD`, date, cache, and shutdown behavior.

## Deterministic website fixtures

Create this minimum fixture tree:

```text
Test/MiniHttpServer/
|- Website/
|  |- index.html
|  |- second.html
|  |- site.css
|  `- image.svg
`- Assets/
   |- index.html
   |- app.js
   `- message.json
```

No `.js` file may exist below `Website`.

The fixtures must visibly prove:

- `index.html` has a normal link to `second.html`; `second.html` links back.
- Both Website pages load only `http://localhost:8889/Assets/app.js` with `<script type="module">`. Module loading is intentional because it is CORS checked.
- `index.html` renders `image.svg` through `<img>` and includes deterministic initial/status text plus a button.
- `app.js` changes the visible status after module load, wires the button to another visible text change, and fetches `http://localhost:8889/Assets/message.json`.
- Make the fetch trigger a real preflight, for example by supplying `Content-Type: application/json` on the cross-origin request. Display either the returned deterministic JSON message or a clear failure marker in the DOM.
- `Assets/index.html` is simple but browseable at the exact `/Assets` prefix so exact-prefix behavior can be checked independently.

Keep all content deterministic and offline. Do not reference CDN scripts, web fonts, analytics, or external images.

## Linux and macOS layout preparation

Move the existing tracked files:

```text
Test/Linux/Main.cpp
Test/Linux/vmake
Test/Linux/vmake.txt
Test/Linux/makefile
```

to:

```text
Test/Linux/UnitTest/Main.cpp
Test/Linux/UnitTest/vmake
Test/Linux/UnitTest/vmake.txt
Test/Linux/UnitTest/makefile
```

Update only the hand-authored `Test/Linux/UnitTest/Main.cpp` and `vmake` for the extra directory level. In particular, the project path becomes `../../UnitTest/UnitTest/UnitTest.vcxproj`, source/import removal paths gain one `..`, and resource/output paths still resolve to `Test/Resources` and `Test/Output`.

Create `Test/Linux/MiniHttpServer/vmake` from the repository's CLI-project pattern:

- Use `CPP_TARGET=./Bin/MiniHttpServer`.
- Read `../../UnitTest/MiniHttpServer/MiniHttpServer.vcxproj`.
- Remove Windows-only sources and retain the Linux/macOS source selected by platform guards.
- Use correct `../../../Import` and `../../../Source` depth where explicit paths are needed.

Never hand-edit `vmake.txt` or `makefile`. Run the absolute `.github/Ubuntu/build.sh` from each new project directory to regenerate them, inspect the generated source lists, and commit the generated files only after the build script produces them.

This two-project directory layout is used for both Linux and macOS verification.

## Deferred documentation updates

During execution of this task, update `Project.md` to:

- list both `UnitTest.vcxproj` and `MiniHttpServer.vcxproj` in `Test/UnitTest/UnitTest.sln`;
- describe `MiniHttpServer` as the CLI/browser verification project and record its two folder arguments;
- change the UnitTest Unix build location to `Test/Linux/UnitTest`;
- add `Test/Linux/MiniHttpServer` as the MiniHttpServer Unix build location; and
- retain the rule that relevant unit tests are required when shared product source changes.

Also update the active Linux instruction in `README.md` from `Test/Linux/makefile` to `Test/Linux/UnitTest/makefile`. Do not rewrite historical completed TODOs solely because their old Linux path appears in recorded history.

## Launch instructions

### Windows and Chrome

Create an ignored `Test/UnitTest/MiniHttpServer/MiniHttpServer.vcxproj.user` for local Debug x64 execution with:

```text
"..\MiniHttpServer\Website" "..\MiniHttpServer\Assets"
```

as `LocalDebuggerCommandArguments`. From `Test/UnitTest`, start the server in an interactive/asynchronous terminal through the required wrapper:

```powershell
& REPO-ROOT\.github\Scripts\copilotExecute.ps1 -Mode CLI -Executable MiniHttpServer -Configuration Debug -Platform x64
```

Keep that terminal alive after both ready messages. Call browser control, select/drive Chrome, and navigate to `http://localhost:8888/`.

### Linux and Firefox

From `Test/Linux/MiniHttpServer`, build with the absolute `REPO-ROOT/.github/Ubuntu/build.sh`, then start the binary asynchronously from the same directory:

```text
./Bin/MiniHttpServer ../../MiniHttpServer/Website ../../MiniHttpServer/Assets
```

Call browser control, select/drive Firefox, and navigate to `http://localhost:8888/`.

### macOS and Safari

Use the same `Test/Linux/MiniHttpServer` build directory, build through the absolute `.github/Ubuntu/build.sh`, and launch with the same relative arguments. Call browser control, select/drive Safari, and navigate to `http://localhost:8888/`.

On every platform, leave the server running until browser checks finish, then send the explicit stop command/Enter and verify a clean exit and released ports.

## Mandatory browser verification

Browser control must perform and record all of the following in the named browser for each platform:

1. Open `http://localhost:8888/` and verify the expected title, heading, initial content, and CSS styling.
2. Verify the SVG is visibly rendered and loaded, using rendered state such as nonzero `naturalWidth` plus a screenshot when available.
3. Verify the module-loaded status produced by `http://localhost:8889/Assets/app.js`.
4. Verify the cross-origin/preflight JSON fetch changes the DOM to the expected success message and does not produce a CORS failure.
5. Click the button and verify its JavaScript reaction.
6. Follow the link to `second.html`, verify that page, and navigate back.
7. Directly browse `http://localhost:8889/Assets` and verify the second folder's index page.
8. Verify `http://localhost:8889/app.js` and `http://localhost:8889/AssetsExtra/app.js` are not served by the `/Assets` API.

Record only browsers and operating systems actually exercised. If one required platform is unavailable in the current environment, mark it explicitly unverified; do not infer Safari/Firefox behavior from Chrome or claim completion of the overall three-platform gate.

## Build and acceptance

- Build `Test/UnitTest/UnitTest.sln` through `.github/Scripts/copilotBuild.ps1` for Debug/Release and Win32/x64 after adding the project. Require zero errors and resolve new warnings.
- On Windows, run the Debug x64 CLI and complete the Chrome browser workflow, then stop cleanly.
- On Linux and macOS, build from both `Test/Linux/UnitTest` and `Test/Linux/MiniHttpServer`, run the server, and complete the Firefox or Safari workflow respectively.
- If fixing a discovered issue changes shared product source, run the focused MiniHttp tests and the full relevant UnitTest suite on that platform.
- Inspect console/browser errors and ensure no memory leaks, crashes, blocked native dialogs, stale processes, or occupied ports remain.
- Update `Project.md` and `README.md`, remove temporary diagnostics, commit all intentional project/layout/fixture/documentation/evidence changes, and push the current branch.

Acceptance requires the new solution project, two CLI folder roots, exact `8888` and `8889/Assets` prefixes, all JavaScript in Assets, visible navigation/reaction/image behavior, genuine CORS/preflight success, browser-control evidence from Chrome/Firefox/Safari on their named platforms, the reorganized Unix project layout, updated project documentation, clean builds, and clean shutdown.
