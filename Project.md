# General Instruction

## Solution to Work On

You are working on the solution `REPO-ROOT/Test/UnitTest/UnitTest.sln`,
therefore `SOLUTION-ROOT` is `REPO-ROOT/Test/UnitTest`.

## Projects for Verification

The solution contains:
- `REPO-ROOT/Test/UnitTest/UnitTest/UnitTest.vcxproj`, the unit test project.
- `REPO-ROOT/Test/UnitTest/MiniHttpServer/MiniHttpServer.vcxproj`, the portable CLI/browser verification project for `SocketHttpServerApi`.

Run the browser verification project as `MiniHttpServer <WebsiteFolder> <AssetsFolder>`.

### MiniHttpServer Browser Verification

- Windows: from `REPO-ROOT/Test/UnitTest`, set the Debug x64 arguments to `"..\MiniHttpServer\Website" "..\MiniHttpServer\Assets"`, build and run through `copilotBuild.ps1` and `copilotExecute.ps1`, and drive Chrome with browser control.
- Linux and macOS: from `REPO-ROOT/Test/Linux/MiniHttpServer`, run the absolute `REPO-ROOT/.github/Ubuntu/build.sh`, then run `./Bin/MiniHttpServer ../../MiniHttpServer/Website ../../MiniHttpServer/Assets`; drive Firefox on Linux and Safari on macOS with browser control.
- Open `http://localhost:8888/`; expect the styled page and SVG, `Module status: loaded from Assets.`, `Fetch status: cross-origin JSON loaded from Assets.`, and no console or CORS errors.
- Click the button and expect `Button status: action handled by Assets module.`; open the second page and return, expecting the module and fetch statuses to succeed.
- Open `http://localhost:8889/Assets` and expect the Assets index; expect `http://localhost:8889/app.js` and `http://localhost:8889/AssetsExtra/app.js` not to be served.
- Press Enter and expect a clean exit with ports 8888 and 8889 released.

When any *.h or *.cpp file is changed, unit test is required to run.
When shared product source changes, all relevant unit tests are required to run.

When any test case fails, you must fix the issue immediately, even those errors are unrelated to the issue you are working on.

## Linux and macOS Specific

- `REPO-ROOT/Test/Linux/UnitTest` stores the Unix configuration for `UnitTest.vcxproj`.
- `REPO-ROOT/Test/Linux/MiniHttpServer` stores the Unix configuration for `MiniHttpServer.vcxproj`.

You need to build, run, test, and debug each project in its matching folder, otherwise it will not function properly.
On Linux and macOS, only configuration "debug x64" is available, no need to build or run projects with other configurations.
