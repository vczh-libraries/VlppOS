# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

vmake seems to not working in the UnitTest project, figure out why and fix it

# UPDATES

# TEST [CONFIRMED]

From `Test/Linux`, run the repository-prescribed `.github/Ubuntu/build.sh -f` so `vmake --make` regenerates `vmake.txt` and `makefile` from `Test/UnitTest/UnitTest/UnitTest.vcxproj`. Confirm the generated Linux source list includes `AsyncSocket.Linux.cpp` and `TestInterProcess_AsyncSocket.cpp`, excludes the Windows and macOS async-socket implementations, and the generated link command places `-luring` after the object files. A clean build and the complete UnitTest run must then succeed. Also compare the generated source list with the `ClCompile` entries and the `CPP_REMOVES`/`CPP_ADDS` selection so a stale generated file cannot hide a broken `vmake` configuration.

The source-list portion succeeds: `vutil_CppFromVcxproj` extracts both new files, the Linux `CPP_REMOVES` selection removes macOS and Windows implementations, and a regeneration with the previously extracted liburing development package produces the correct source list, object rules, link order, and a successful clean build. There is no timestamp cache or unsupported `CPP_VCXPROJ` variable involved.

The normal environment reproduces the failure because `/usr/include/liburing.h` is absent and the canonical Tools `vapt --install` package list does not include `liburing-dev`. `vutil_CppDependencies` invokes `clang++ -MM`, but wraps it in command substitution followed by `echo | sed`; the wrapper therefore returns success even when Clang reports the missing header. `vmake-cpp` embeds that failed command inside another `echo`, masking the status again. `vmake --make` consequently succeeds while generating a literal `./Obj/` rule instead of `./Obj/AsyncSocket.Linux.o`, after which the build reports a misleading missing-rule failure. The tracked `vmake.txt` and `makefile` also predate both async-socket platform additions, so they present the same missing-source symptom until a successful regeneration.

# PROPOSALS

- No.1 [CONFIRMED] Declare the liburing dependency and make vmake dependency failures fatal

## No.1 Declare the liburing dependency and make vmake dependency failures fatal

Add `liburing-dev` to the canonical Ubuntu `vapt` help and installation target list in `../Tools`. Fix `vutil_CppDependencies` at the same source of truth so it captures `clang++ -MM` output, returns Clang's nonzero status before formatting, and only prints dependencies on success. Fix `vmake-cpp` so it captures and checks the helper result before emitting each object rule instead of evaluating it inside `echo`. Propagate shared Ubuntu build-tool changes into VlppOS with `vgo uci VlppOS`.

Regenerate the UnitTest `vmake.txt` and `makefile` through the prescribed build with a valid liburing development environment rather than hand-editing them. The generated files must contain the Linux async implementation and shared async test, and a missing liburing header must stop `vmake` with the original compiler diagnostic instead of leaving a malformed successful makefile.

### CODE CHANGE

In `../Tools`, added `liburing-dev` to the Ubuntu package installer and documented the dependency-generation failure contract. Changed `vutil_CppDependencies` to retain the `clang++ -MM` result and return its nonzero status before formatting any output. Changed `vmake-cpp` to capture and validate each dependency result before emitting the corresponding object rule. Propagated the two shared build-tool changes with `vgo uci VlppOS`.

Regenerated `Test/Linux/vmake.txt` and `Test/Linux/makefile` through `.github/Ubuntu/build.sh -f` with the liburing development headers and library available. No UnitTest source or test case was changed.

### CONFIRMED

Without liburing in the environment, `.github/Ubuntu/build.sh -f` now exits with status 1 at `liburing.h file not found`, and the generated makefile contains no literal `./Obj/` rule. With liburing available, the same command performs a clean build successfully. The regenerated source list contains `AsyncSocket.Linux.cpp` and `TestInterProcess_AsyncSocket.cpp`, excludes the macOS and Windows implementations, and the link command places all objects before `-luring`. The complete UnitTest run passes 11/11 test files and 115/115 test cases.
