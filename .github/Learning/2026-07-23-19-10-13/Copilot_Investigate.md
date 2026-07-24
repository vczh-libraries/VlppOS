# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

# Native `wchar_t` TUI character events

This task refactors the already implemented cross-platform TUI from [`TODO_Task.md`](./TODO_Task.md). Complete and commit this task before implementing [`TODO_Task_TuiPlayground.md`](./TODO_Task_TuiPlayground.md).

For the `TuiCharInfo` character-event contract, this task has priority over the decoded-scalar rules in `TODO_Task.md`, [`Task_TUI.md`](./Task_TUI.md), and the future GacUI integration plan.

- Commit and push all changes after finishing this task.
- Do not implement the ASCII-art painter in this task.

## GOAL

Change `TuiCharInfo::code` from `char32_t` to the platform-native `wchar_t`:

```C++
struct TuiCharInfo
{
	wchar_t							code = 0;
	bool							ctrl = false;
	bool							shift = false;
	bool							alt = false;
	bool							capslock = false;
};
```

`ITuiCallback::Char(const TuiCharInfo&)` reports one native `wchar_t` code unit per character callback, not necessarily one complete Unicode scalar value:

- Windows uses UTF-16 `wchar_t`. A supplementary scalar is reported as independent high- and low-surrogate `Char` callbacks in native character-unit order. Other callback types may occur between them.
- Linux and macOS use UTF-32 `wchar_t`. A decoded scalar is normally reported in one callback.
- Each code unit is an ordinary independently dispatched TUI event. If a callback requests `TUI::Stop` after a high surrogate, the queued low surrogate is suppressed by the existing stop contract.

This change intentionally aligns TUI character input with native `wchar_t` consumers. It does not change the scalar-based drawing and buffer APIs: in particular, `TuiPixel::c` remains `char32_t`.

## DETAILS

### Public and internal event contract

- Change only `TuiCharInfo::code` to `wchar_t`. `TuiPixel::c`, `TuiPixel::GetChar32`, `TUI::MeasureChar`, and both `TUI::PrintChar` overloads remain `char32_t`.
- `unittest::TuiBackendEvent::charInfo` follows the public structure automatically. Test backends and callbacks must inject and observe native code units without converting them back to scalars in the dispatcher.
- Preserve modifier fields from the native input record that produced each unit. Repeat counts and modifiers apply to each native record and emitted unit.
- Do not add a worker thread, lock, atomic, new public conversion helper, or key-code translation as part of this refactoring.

### Windows backend

- Change the Windows backend's character queue to accept `wchar_t`.
- For every key-down `KEY_EVENT_RECORD` with a nonzero `uChar.UnicodeChar`, enqueue that UTF-16 code unit unchanged and honor `wRepeatCount`.
- Delete the backend's pending-high-surrogate field, surrogate-pair combination, U+FFFD substitution for isolated surrogates, and related cleanup. Isolated high or low surrogates are exposed unchanged because `TuiCharInfo` now represents one native code unit.
- Keep key, modifier, repeat, event-order, reentrancy, stop, and callback-exception behavior unchanged.

### Linux and macOS backend

- Keep the existing incremental UTF-8 decoder, including its validation, incomplete-sequence buffering, and U+FFFD recovery for malformed UTF-8.
- After one scalar is decoded, enqueue it as the platform-native UTF-32 `wchar_t`. Platform-specific POSIX code may assume `wchar_t` is UTF-32.
- Standalone Escape and all other character/control callbacks use `wchar_t` values. Mouse and key placeholder behavior is unchanged.

### Converting native units back to scalars

- Scalar-oriented consumers use `vl::encoding::UtfConversion<wchar_t>::To32`, declared by Vlpp and available through the existing VlppOS includes.
- Do not use `vl::encoding::UtfToUtfReaderBase` or `vl::encoding::UtfStringToStringReader` across separate callbacks. They are pull readers for already available input, treat zero as the end, and cannot wait and resynchronize after an incomplete UTF-16 pair.
- Shared code distinguishes the native encoding with `VCZH_WCHAR_UTF16` and `VCZH_WCHAR_UTF32`. Platform-specific code may assume Windows is UTF-16 and Linux/macOS is UTF-32.
- On UTF-16, retain at most one high surrogate and call `vl::encoding::UtfConversion<wchar_t>::To32` with adjacent high/low units in the character-unit stream. The consumer owns its malformed-sequence policy and must reprocess a current valid unit after discarding or replacing an unmatched pending unit so input and controls are not swallowed.
- On UTF-32, one native unit is converted directly, then validated as a Unicode scalar before scalar-only APIs such as `TUI::MeasureChar` or `TUI::PrintChar` receive it.

### Source, tests, and generated output

Update at least:

- `Source/TUI/TUI.h`.
- `Source/TUI/TUI.Windows.cpp`.
- `Source/TUI/TUI.Linux.cpp`.
- `Test/Source/TestTui.cpp`.
- The current `Test/UnitTest/TuiPlayground/Main.cpp`, using `L'q'`, `L'Q'`, and a native Escape value until the painter task replaces its input handling.

Regenerate the complete VlppOS release from `CodegenConfig.xml`; do not hand-edit `Release`. Confirm that the generated public header and Windows/Linux implementations contain the native-unit contract.

Update every affected character-event clause in [`Task_TUI.md`](./Task_TUI.md) to describe native `wchar_t` units. This includes removing the Windows scalar-combination rule, replacing any complete-scalar-before-`Char` guarantee, and expressing Escape with a native `wchar_t` value. `TODO_Task.md` remains the historical implemented requirement and is superseded by this task for character events.

Do not edit the sibling GacUI repository in this task. This contract supersedes the affected bridge description in the future `../GacUI/ToDo/Task_TUI.md`; before GacUI Phase 2 is implemented, that task must be updated so the bridge forwards `TuiCharInfo::code` directly instead of splitting a `char32_t`, because Windows already receives separate high/low `Char` events from TUI.

## VERIFICATION

- Add a compile-time or equivalent test that `TuiCharInfo::code` is exactly `wchar_t`.
- Update the fake backend and callback helpers to store `wchar_t` values. Do not create temporary invalid one-unit UTF-16 strings merely to log a surrogate; record raw unit values for such tests.
- Verify that ordinary, isolated-high-surrogate, and isolated-low-surrogate character units and all modifier fields survive backend-event dispatch unchanged under `VCZH_WCHAR_UTF16`.
- Under `VCZH_WCHAR_UTF16`, enqueue a known high/low surrogate pair and verify two callbacks with the exact units and order. In a separate case, request `TUI::Stop` from the high-surrogate callback and verify that the low-surrogate callback is suppressed.
- Under `VCZH_WCHAR_UTF32`, enqueue the corresponding supplementary `wchar_t` value and verify one callback.
- Keep the existing lifecycle, listener mutation, nested `RunOneCycle`, stop, timer, resize, callback-exception, and Console-cleanup cases passing after converting their character helpers and literals to `wchar_t`.
- Build `Test/UnitTest/UnitTest.sln` in Debug x64 through `.github/Scripts/copilotBuild.ps1`. Run the complete `UnitTest` project through `.github/Scripts/copilotExecute.ps1`, and inspect the final log for failures and Debug memory leaks.
- Build and run the current `TuiPlayground` through the repository scripts. Confirm that `q`, `Q`, and Escape still stop it and restore the terminal.
- Verify that regenerated `Release` output matches the changed source contract and that no stale `char32_t` assumption remains for `TuiCharInfo`.
- Keep Linux/macOS source and build configurations valid. Build and run the complete unit tests on those hosts when they are available, confirming the one-unit UTF-32 path.

## REVIEW COMMENTS

# UPDATES

# TEST [CONFIRMED]

Add native-unit character-event coverage to `Test/Source/TestTui.cpp` before changing production code:

- Require `decltype(TuiCharInfo::code)` to be exactly `wchar_t`; this is the root-cause-sensitive reproduction against the current `char32_t` contract.
- Make the fake backend and callback helper inject and record raw `wchar_t` values without constructing strings from isolated UTF-16 surrogates.
- Under `VCZH_WCHAR_UTF16`, dispatch an ordinary unit, isolated high and low surrogates, and an adjacent supplementary high/low pair. Verify exact unit order and every modifier field. In a separate run, stop from the high-surrogate callback and verify the low-surrogate callback is suppressed.
- Under `VCZH_WCHAR_UTF32`, dispatch the corresponding supplementary scalar as one `wchar_t` unit and verify one callback with unchanged modifiers.
- Keep the existing lifecycle, nested-cycle, listener-generation, stop, timer, resize, exception, rendering, and Console-cleanup cases using native character literals.

The initial Debug x64 build compiled with zero warnings and zero errors. The pre-fix complete unit-test run reached `Test/Source/TestTui.cpp` and failed exactly at `std::is_same_v<decltype(TuiCharInfo::code), wchar_t>`, confirming that the current public contract is still `char32_t`. After the proposal, the complete suite must pass with no Debug memory leaks. Final verification also requires regenerated `Release` files to contain the native-unit contract, the current TuiPlayground to stop on `L'q'`, `L'Q'`, and native Escape while restoring the terminal, and the available-host build matrix to remain valid.

# PROPOSALS

- No.1 ADOPT NATIVE `wchar_t` CHARACTER UNITS END TO END [CONFIRMED]

## No.1 ADOPT NATIVE `wchar_t` CHARACTER UNITS END TO END

Make the public event field and both platform queues use the platform-native `wchar_t` unit without any shared-dispatch conversion. Windows should enqueue each nonzero `KEY_EVENT_RECORD::uChar.UnicodeChar` unchanged for every repeat and remove all pending-surrogate state and replacement behavior. POSIX should retain its validated incremental UTF-8 scalar decoder, cast each completed scalar or recovery value to its UTF-32 `wchar_t`, and use a native Escape unit.

Keep every scalar-oriented drawing and buffer API as `char32_t`. Convert the current playground and test callback inputs to native literals, document the new unit contract in `Task_TUI.md`, regenerate the complete release from `Release/CodegenConfig.xml`, and verify source/generated files contain no stale scalar assumption specifically for `TuiCharInfo`.

### CODE CHANGE

- Change only `TuiCharInfo::code` in `Source/TUI/TUI.h` from `char32_t` to `wchar_t`; leave every pixel, width, and drawing scalar signature unchanged.
- In `Source/TUI/TUI.Windows.cpp`, remove `WindowsTuiBackend::highSurrogate`, make `QueueChar` accept `wchar_t`, and enqueue `record.uChar.UnicodeChar` unchanged once per repeat without combination, substitution, or cleanup state.
- In `Source/TUI/TUI.Linux.cpp`, make `QueueChar` accept `wchar_t`; keep the existing UTF-8 decoder and validation, casting each decoded scalar and U+FFFD recovery result to the platform UTF-32 unit, and emit Escape as `(wchar_t)0x1B`.
- Finish the native-unit test-helper conversion in `Test/Source/TestTui.cpp` and update `Test/UnitTest/TuiPlayground/Main.cpp` to compare `L'q'`, `L'Q'`, and `(wchar_t)0x1B`.
- Update the affected Windows and POSIX input clauses in `Task_TUI.md`, including the task-priority note, independent UTF-16 unit order, complete-scalar decoding only before POSIX conversion, and native Escape value.
- Regenerate all six tracked VlppOS release files through CodePack using `Release/CodegenConfig.xml`; do not edit generated output manually.

### CONFIRMED

The public source and generated release header now define only `TuiCharInfo::code` as `wchar_t`; `TuiPixel::c`, `TuiPixel::GetChar32`, `TUI::MeasureChar`, and both `TUI::PrintChar` overloads remain `char32_t`. Shared dispatch still forwards `TuiBackendEvent::charInfo` directly without conversion.

The Windows backend has no pending-surrogate field, pairing branch, isolated-surrogate substitution, or cleanup state. Its queue accepts `wchar_t`, copies all four modifier fields from the producing input record, and enqueues the record's nonzero UTF-16 unit once per repeat. The POSIX backend retains its incremental UTF-8 validation and buffering, but now queues completed scalars, U+FFFD recovery, and standalone Escape as native UTF-32 `wchar_t` units.

The Debug x64 solution builds with zero warnings and zero errors. The complete unit-test run passes 16/16 files and 247/247 cases, including exact type identity, ordinary and isolated UTF-16 units, adjacent high/low order, all modifiers, stop suppression after a high surrogate, and every pre-existing lifecycle/callback case. The final execution log contains no Debug memory-leak signal.

CodePack regenerated all six release outputs from `Release/CodegenConfig.xml`; the three files whose content should change—`VlppOS.h`, `VlppOS.Windows.cpp`, and `VlppOS.Linux.cpp`—contain the same native-unit contract as source, and searches find no stale `TuiCharInfo` scalar or Windows surrogate-combination assumption.

The current TuiPlayground was launched through `copilotExecute.ps1` in fresh native 120x30 consoles. `q`, `Q`, and Escape each exited with code zero and restored pre-start buffer sentinels. The `Q` and Escape probes also observed the exact double-line corners U+2554, U+2557, U+255A, and U+255D while active. Linux and macOS runtime hosts are not available in this Windows session; their shared POSIX source, generated release implementation, and conditional `VCZH_WCHAR_UTF32` test path are present, but runtime execution remains deferred to those hosts.
