# General Instruction

## Solution to Work On

You are working on the solution `REPO-ROOT/Test/UnitTest/UnitTest.sln`,
therefore `SOLUTION-ROOT` is `REPO-ROOT/Test/UnitTest`.

## Projects for Verification

The solution contains:
- `REPO-ROOT/Test/UnitTest/UnitTest/UnitTest.vcxproj`, the unit test project.
- `REPO-ROOT/Test/UnitTest/MiniHttpServer/MiniHttpServer.vcxproj`, the portable CLI/browser verification project for `SocketHttpServerApi`.

Run the browser verification project as `MiniHttpServer <WebsiteFolder> <AssetsFolder>`.

When any *.h or *.cpp file is changed, unit test is required to run.
When shared product source changes, all relevant unit tests are required to run.

When any test case fails, you must fix the issue immediately, even those errors are unrelated to the issue you are working on.

## Linux and macOS Specific

- `REPO-ROOT/Test/Linux/UnitTest` stores the Unix configuration for `UnitTest.vcxproj`.
- `REPO-ROOT/Test/Linux/MiniHttpServer` stores the Unix configuration for `MiniHttpServer.vcxproj`.

You need to build, run, test, and debug each project in its matching folder, otherwise it will not function properly.
On Linux and macOS, only configuration "debug x64" is available, no need to build or run projects with other configurations.
