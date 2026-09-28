# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

In `Vlpp` repo, split `Conversion.Linux.cpp` into `Conversion.Linux.cpp` and `Conversion.Wasm.cpp`.
In `VlppOS` repo, merge `Threading.Wasm.cpp` into `Threading.Linux.cpp`.

In the unit test framework for wasm:
- `Console::TryRead` should be implemented, but in `wasm-unittest.html` in `Tools` the read action always returns `undefined`.
  - `undefined` will convert to empty and ordinary string convert to string.

Release updated `Tools/Ubuntu` to all 4 repos mentioned below.
Release `Vlpp` -> `VlppOS` -> `VlppRegex` -> `VlppReflection` and make sure all wasm unit test project works perfectly.

# TEST [CONFIRMED]

Firefox reproduced `Assertion failure: result` in the new JavaScript string-input case against the original `Console::TryRead` stub, followed by one `wasm_main returns 1.` line. The callback supplied a string but the API returned an empty nullable. The browser had COOP/COEP isolation and no browser errors.

- Add Wasm console regressions that replace the worker read callback and require ordinary strings, empty strings, Unicode and embedded zero code units to become present `WString` results. Verify JavaScript callback exceptions and invalid return types become C++ errors.
- Preserve the default runner EOF test: `undefined` maps to an empty `Nullable<WString>` and `Console::Read()` returns `WString::Empty`.
- Run all four complete retained Wasm suites through their generated HTTP launchers in Firefox, with isolation headers and exactly one `wasm_main returns 0.` completion, no failed cases or browser errors.
- Run native Clang suites for all four libraries and native GCC for Vlpp. Existing conversion and threading cases cover the source-file reorganization, including concurrent/repeated thread waits. Downstream builds verify the generated release artifacts.
- Check source/project/filter inventories and byte-identical Tools propagation. Regenerate releases in dependency order and commit/push the affected repositories.

# PROPOSALS

- No.1 Separate conversion backends, share threading, and bind nullable console input [CONFIRMED]

## No.1 Separate conversion backends, share threading, and bind nullable console input

Keep locale-based native conversion in `Conversion.Linux.cpp` and move the existing locale-independent UTF-8 Wasm conversion into `Conversion.Wasm.cpp`, registering both in the project and filters. Move the two Emscripten threading methods and their guarded include into `Threading.Linux.cpp`; delete the redundant Wasm file and registrations.

Call the worker's `vlConsoleRead` synchronously. Keep the exception/Embind handle adapter at the C++/JavaScript boundary, require either `undefined` or a string, and decode a length-preserving UTF-16 temporary into `WString`. A string of length zero remains present. JavaScript exceptions become C++ errors. The canonical Tools runner installs a callback returning `undefined` before module initialization.

### CODE CHANGE

Added console input regression tests, implemented the binding and requested platform-file changes, and updated the owning console and platform guidance. Committed and pushed Tools/Ubuntu, propagated it using `vgo uci` to the four repositories, then regenerated/copy-synchronized releases in the order Vlpp, VlppOS, VlppRegex, VlppReflection. Updated project/filter registrations and regenerated the affected Unix source inventories using each repository's build wrapper.

The first implementation passed ordinary and Unicode inputs but failed the embedded-zero case: `ConvertStringDirect` in Vlpp's `Source/Strings/Conversion.cpp` ignored the explicit `ObjectString` length and converted only its first zero-terminated segment. Preserve every segment and embedded terminator when sizing and filling the output. Add a platform-neutral conversion regression covering UTF-8, UTF-16, UTF-32, wide and ASCII narrow strings, including leading, adjacent and trailing zero code units. Keep the existing buffer-level zero-terminated conversion contract unchanged.

### CONFIRMED

Verified on Linux on 2026-09-28. All builds used the repository-local `.github/Ubuntu/build.sh`. Native suites ran with `/C`. Full Wasm builds ran through each generated `Bin/app.sh` HTTP launcher in Firefox, using the unchanged retained-test selection.

| Repository | Native Clang files / cases | Wasm files / cases |
|---|---|---|
| Vlpp | 32/32, 466/466 | 32/32, 472/472 |
| VlppOS | 14/14, 277/277 | 7/7, 81/81 |
| VlppRegex | 9/9, 226/226 | 8/8, 192/192 |
| VlppReflection | 9/9, 53/53 | 7/7, 51/51 |

Vlpp also passed native GCC: 32/32 files and 466/466 cases. The console regressions now pass for default `undefined`, empty/ASCII/Unicode/embedded-zero strings, a throwing JavaScript callback and an invalid return type. The shared converter regression passes on Clang, GCC and Wasm. VlppOS retains its complete Wasm threading coverage, including concurrent and repeated waits.

Every browser run reported `crossOriginIsolated`, a responsive page, exactly one `wasm_main returns 0.` line, zero failure entries and no browser errors. Vlpp ran in one application worker; the three VlppOS consumers used that worker plus 32 preloaded pthread workers. The SDK's existing pthread/memory-growth performance warning remains; compilation and linking succeeded.

All ten propagated Tools files match the canonical Tools/Ubuntu copies byte for byte. All upstream Release files copied into the downstream Import folders match byte for byte. CodePack regenerated all four releases; VlppRegex and VlppReflection's own release content is unchanged. Project/filter XML is valid, the generated inventory includes `Conversion.Wasm.cpp`, and the VlppOS inventory no longer references `Threading.Wasm.cpp`. Each final Wasm package has the matching HTML runner, executable launcher, default-page symlink, module and pthread helper where required; `Bin/UnitTest` matches `app.wasm`. Native Windows and macOS were not executed.
