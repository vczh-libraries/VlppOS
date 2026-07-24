# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

# Cross-platform TUI

The GacUI integration analysis is in [`../GacUI/ToDo/Task_TUI.md`](../GacUI/ToDo/Task_TUI.md).
The verified Windows, Linux, and macOS implementation details are in [`./Task_TUI.md`](./Task_TUI.md).
Read both files before implementation. If either file conflicts with this file, this file has priority.
This VlppOS work is Phase 1 of the complete GacUI TUI plan; the GacUI adapter and renderer are Phase 2.

Implement cross-platform TUI utilities for Windows, Linux, and macOS, sharing all platform-independent code.

## Ownership and dependency boundary

`vl::console::Console` is defined in the lower-level `Vlpp` repository. `vl::console::TUI` will be defined in `VlppOS`, so `Console` cannot call `TUI::IsInUse()` directly without creating a dependency cycle.

Add `Console::Enable()`, `Console::Disable()`, and `Console::IsEnabled()` to `vl::console::Console`. Console starts enabled. Before changing terminal state, `TUI::Start` uses `CHECK_ERROR(Console::IsEnabled(), ...)`; a caller-disabled Console is not silently re-enabled. After terminal takeover succeeds, `TUI::Start` calls `Console::Disable()`. Every cleanup path reached after that call invokes `Console::Enable()`. The actual Console sinks (`Write(const wchar_t*, vint)`, `TryRead`, `SetColor`, and `SetTitle`) use `CHECK_ERROR(Console::IsEnabled(), ...)`; the other `Read` and `Write` overloads already funnel through these sinks.

These functions control the existing Console read/write/color/title operations. They do not redirect Console operations to TUI, and `Console` never references `TUI`. `Enable` and `Disable` are idempotent boolean setters, not a nesting counter. Keep the state non-nested and owner-thread-only for this task; callers must not manually re-enable Console while TUI owns the terminal. Add Vlpp unit tests for the initial state, repeated `Enable`/`Disable`, and all guarded sinks.

Make this change in the `Vlpp` source, update its `Release` folder, and then update the imported generated files in `VlppOS`. Do not edit `VlppOS/Import` as the source of the fix.

`VlppOS` must not depend on GacUI. TUI event structures may mirror the primitive fields of GacUI's `NativeWindowMouseInfo`, `NativeWindowKeyInfo`, and `NativeWindowCharInfo`, but they must be declared independently in VlppOS. GacUI will adapt between the two APIs.

## Definition of `vl::console::TUI`

Like `vl::console::Console`, `vl::console::TUI` is a static class.

`TUI::Start` takes one portable options structure on every platform. At minimum it contains a requested color mode:

```C++
enum class TuiColorMode
{
	Auto,
	TrueColor,
	Color256,
	Color16,
};

struct TuiStartOptions
{
	TuiColorMode colorMode = TuiColorMode::Auto;
};
```

The backend may downgrade an unsupported request. Expose the selected emission/quantization mode after startup, not a claim about the terminal's actual display capability. Enabling Windows virtual-terminal processing, or inspecting `TERM` and `COLORTERM` on POSIX, is only a capability hint and is not proof of the terminal's actual color rendering.

`TUI::TryGetConsoleSize(vint& width, vint& height)` queries the visible terminal size without starting TUI. GacUI needs this before its native window service enters `TUI::Start`. `GetBufferWidth` and `GetBufferHeight` report the allocated buffer while TUI is active. Expose `TUI::IsStopRequested()` so an owner-thread adapter that emits multiple higher-level callbacks inside one `ITuiCallback` can stop between them; it returns false while inactive and uses the normal owner-thread check while active.

### Single-threaded lifecycle

The lifecycle does not require a TUI worker thread:

1. `TUI::Start` is called on the owner thread, normally from `main`. It first checks the active state: an active call from the recorded owner thread is a no-op, and an active call from another thread uses `CHECK_ERROR`.
2. Only for an inactive-to-active transition, it verifies that Console is enabled, saves the terminal state, activates TUI mode, calls `Console::Disable()`, allocates the initial buffer, records the owner thread, and finally sets `TUI::IsInUse()` to true before invoking any user callback.
3. It invokes `Starting`. If no callback requested `Stop`, it then invokes the initial `BufferSizeChanged`.
4. If startup is still active, `Start` repeatedly calls `TUI::RunOneCycle()` until it returns false.
5. Input, resize, timer, `Starting`, `Stopping`, and all other TUI callbacks are invoked synchronously on the thread that called `TUI::Start`.
6. `TUI::Stop` is called from a TUI callback, or from code synchronously invoked by one. It only sets a stop-request flag. The dispatcher checks that flag after every user callback and invokes no later input, resize, character, key, mouse, or timer callback once it is set.
7. After the current callback and any nested `RunOneCycle` calls unwind, `Start` invokes `Stopping` exactly once, restores the terminal and Console, clears `IsInUse`, and returns.

Expose public `TUI::RunOneCycle()` as an owner-thread, active-TUI operation. It waits until terminal input, resize, or the earliest timer/parser deadline, dispatches the minimum bounded unit of work, and returns false once `Stop` has been requested. `Start` is the initialization/cleanup wrapper implemented around repeated calls to it. It is intentionally reentrant so GacUI's same-thread modal loop can call it from a callback. Store decoded events in a TUI-owned queue before invoking listeners, and never retain a platform buffer, iterator, or lock across a callback, so a nested cycle can safely consume already-read events.

All active TUI operations are thread-affine. Do not add atomics, locks, worker threads, or cross-thread `Stop` support for this task. An active call from a non-owner thread uses `CHECK_ERROR`. On the owner thread, `Start` while active is a no-op; `Stop` during shutdown is also a no-op. `Stop` while inactive is a no-op.

A listener may request `Stop` from `Starting`, `BufferSizeChanged`, timer, mouse, keyboard, or character callbacks. A stop request from `Starting` skips the initial `BufferSizeChanged`. If `Starting` or another non-`Stopping` user callback throws, invoke no further user callbacks, including `Stopping`; restore all acquired state and rethrow. Catch at the callback-dispatch boundary, latch the first `std::exception_ptr`, request shutdown, and rethrow it to the caller of the current dispatch (`RunOneCycle` when applicable). Every enclosing cycle checks that latch, so catching an exception from a nested `RunOneCycle` inside a user callback cannot make the outer `Start` continue; after cleanup, `Start` rethrows the latched exception. If `Stopping` throws, still restore all state before rethrowing.

Use scope-based cleanup so partial initialization failures and exceptions restore every state TUI has already changed before the exception is propagated. The restoration promise applies to normal shutdown and exceptions while the process is alive and owns the terminal. It cannot restore consumed input, concurrent external changes, a killed/crashed process, terminal-private state that cannot be queried, or the dedicated POSIX wakeup pipe intentionally allocated once and retained until process exit.

### Listener and timer contract

`TUI::InstallListener` and `TUI::UninstallListener` maintain non-owning `ITuiCallback*` values. Both return `bool`: reject null and duplicate installation, and return false when uninstalling a pointer that is absent. Define at least:

- `Starting` and `Stopping`.
- `BufferSizeChanged`.
- Mouse movement, buttons, double-clicks, and vertical/horizontal wheels.
- `Char(const TuiCharInfo&)` for one decoded Unicode scalar value plus best-effort modifier state.
- Keyboard events with a placeholder event structure. Do not implement key-code translation in this task.
- A periodic `Timer` callback.

Use independent VlppOS structures rather than GacUI types:

```C++
struct TuiMouseInfo
{
	vint x = 0;
	vint y = 0;
	vint wheel = 0;
	bool ctrl = false;
	bool shift = false;
	bool alt = false;
	bool left = false;
	bool middle = false;
	bool right = false;
};

struct TuiKeyInfo
{
	vint code = 0; // reserved for a future key-code translation phase
	bool ctrl = false;
	bool shift = false;
	bool alt = false;
	bool capslock = false;
	bool autoRepeatKeyDown = false;
};

struct TuiCharInfo
{
	char32_t code = 0;
	bool ctrl = false;
	bool shift = false;
	bool alt = false;
	bool capslock = false;
};
```

Windows fills modifier fields from its input record. A legacy POSIX terminal does not reliably report Shift or Caps Lock separately from the resulting character, so fields that the active protocol cannot observe are deterministically false; do not infer them from letter case.

Add `TUI::StartTimer(vint milliseconds)` and `TUI::StopTimer`; require a positive period. The timer is driven by the same blocking event loop: wait until terminal input, resize notification, or the earliest timer/parser deadline, then invoke `Timer` on the owner thread. This is deadline scheduling, not a busy polling loop, and lets the future GacUI controller drive its global timer and main-thread task pump without an input thread.

Listener installation/removal during dispatch must not invalidate the active iteration. Give each successful installation a fresh generation/token and snapshot ordered `(pointer, token)` pairs for each event. Immediately before invocation, require that the same pointer is still installed with the same token. Uninstalling and reinstalling a pointer therefore cannot make an old snapshot invoke the new installation. A newly installed generation waits until the next event; a nested dispatch is a distinct next event and snapshots the then-current registrations. An uninstalled generation is not called later from the current snapshot.

## Buffer and pixels

TUI maintains a row-major, conceptually two-dimensional buffer implemented by a one-dimensional array. Cell `(x, y)` is:

```C++
TUI::GetBuffer()[y * TUI::GetBufferWidth() + x]
```

When the visible terminal size changes:

- Allocate the new buffer.
- Preserve the overlapping rectangle.
- Initialize newly exposed cells as empty `Char` cells with white foreground and black background.
- Repair or clear any width-two character cut by the new right boundary.
- Replace the public buffer pointer, then invoke `BufferSizeChanged`.

A pointer returned by `GetBuffer()` is invalid after a resize, `Stop`, or the next `Start`.

Use these corrected declarations as the intended shape of the cell model:

```C++
struct TuiColor
{
	vuint8_t r, g, b;
};

enum class TuiMergeableGlyph : vuint8_t
{
	None = 0,
	ThinLine = 1,
	ThickLine = 2,
	DoubleLine = 3,
};

struct TuiMergeablePixel
{
	TuiMergeableGlyph up, down, left, right;
};

enum class TuiUnmergeableGlyph : vuint8_t
{
	RoundCorner,
};

enum class TuiUnmergeableDirection : vuint8_t
{
	LeftTop,
	RightTop,
	LeftBottom,
	RightBottom,
};

struct TuiUnmergeablePixel
{
	TuiUnmergeableGlyph glyph;
	TuiUnmergeableDirection direction;
};

enum class TuiPixelGlyph : vuint8_t
{
	Char,
	Mergeable,
	Unmergeable,
	WideCharContinuation,
};

struct TuiPixel
{
	TuiPixelGlyph glyph;
	union
	{
		char32_t c;
		TuiMergeablePixel mergeable;
		TuiUnmergeablePixel unmergeable;
	};
	TuiColor foregroundColor, backgroundColor;
};
```

An empty cell is `TuiPixelGlyph::Char` with `c == 0`. `WideCharContinuation` is distinct from empty and marks the second cell occupied by a width-two scalar. Every drawing helper must clear the complete old width-two character before overwriting either its leading or continuation cell. Direct buffer writers are responsible for maintaining this invariant; `RenderBuffer` validates it with `CHECK_ERROR`.

### Double-line finding

Double lines can be stored in `TuiMergeablePixel`, but the four-arm space is not closed under arbitrary merging:

- `None`, `ThinLine`, and `ThickLine` form a closed 81-state four-arm space: the all-`None` state maps to an empty cell, and each of the other 80 states has an exact Unicode box-drawing character.
- Unicode U+2550 through U+256C adds double straight lines, corners, tees, crosses, and selected thin/double junctions.
- Unicode does not provide double half-line stubs, thick/double junctions, or most arbitrary asymmetric states containing `DoubleLine`.

Therefore, never merge arm styles with a numeric `max` and never map an unsupported state silently to a space. Build the candidate state by letting the later draw replace any same-direction arm that it supplies, then look it up:

- If the candidate has an exact Unicode code point, keep the merged candidate.
- If it has no exact code point, replace the entire cell with the later operation's unmerged cell.

`TuiPixel::GetChar32` returns zero for empty, continuation, or an invalid raw mergeable state. Drawing helpers must not produce invalid states. `RenderBuffer` uses `CHECK_ERROR` to reject every invalid raw mergeable state instead of silently rendering it as empty. Rounded corners remain unmergeable.

If two mergeable drawings use different foreground colors, geometry still follows the merge rule above and the later foreground color wins for the whole cell. This has priority over the older color-dependent merge rule in GacUI's task. A nonempty background in drawing options also overwrites the affected cell background; an empty `Nullable<TuiColor>` preserves it.

The drawing primitives produce these exact base states before merging:

- Every cell of `DrawLineH`, including both endpoints and a one-cell line, has the selected style on both `left` and `right`.
- Every cell of `DrawLineV`, including both endpoints and a one-cell line, has the selected style on both `up` and `down`.
- `DrawRect` requires `x1 < x2` and `y1 < y2`. Its edge cells use the full horizontal/vertical states above; each corner has exactly the two arms pointing into the rectangle.

This deliberately avoids half-line states for primitive endpoints, so thin, thick, and double lines and rectangles are individually representable. Thin/heavy half-line code points remain valid for direct buffer writers, but there is no double-line equivalent. If merging two primitives creates an unsupported double-containing intersection, the later primitive's representable base cell is the fallback.

## Characters and width

`TUI::MeasureChar(char32_t)` measures one Unicode scalar value, returning its terminal-cell width. `PrintChar` only stores independently renderable scalar values. Combining sequences, variation sequences, emoji ZWJ sequences, bidirectional layout, and grapheme-cluster shaping are outside this task because a `TuiPixel` stores only one `char32_t`.

`PrintChar(options, char32_t c, vint x, vint y)` behaves as follows:

- Reject invalid Unicode scalar values.
- A width-one scalar occupies one `Char` cell.
- A width-two scalar occupies a leading `Char` cell and a `WideCharContinuation` cell with copied colors.
- A width-two scalar is not written unless both cells are inside the buffer.
- A zero-width or non-printable scalar is not stored as a standalone cell.

`TuiPixel::GetWChar` returns the code point only when it converts to exactly one native `wchar_t`; otherwise it returns zero. On Windows, a supplementary scalar needs two UTF-16 code units and therefore returns zero. `TuiPixel::GetChar32` returns the Unicode box-drawing code point, the stored scalar, or zero as described above.

The supported terminal must render the selected Unicode box-drawing characters as one cell. Many of them have East Asian Width `Ambiguous`, so the process cannot guarantee this for every font and terminal configuration.

## Drawing operations

Provide active-buffer methods and buffer-explicit helpers for at least:

- `TUI::PrintChar(options, char32_t c, vint x, vint y)`.
- `TUI::DrawLineV(options, vint x, vint y1, vint y2)`.
- `TUI::DrawLineH(options, vint x1, vint x2, vint y)`.
- `TUI::DrawRect(options, vint x1, vint y1, vint x2, vint y2)`.
- `TUI::Clear(TuiColor backgroundColor, vint x1, vint y1, vint x2, vint y2)`.

Line options contain the line style, foreground color, and optional background color. Rectangle options additionally select sharp or rounded corners. Rounded corners are valid only for thin rectangles.

Coordinates are inclusive. Ordered line/clear ranges require the first coordinate to be no greater than the second; invalid ordered ranges use `CHECK_ERROR`. Helpers clip to the buffer, and a fully clipped operation is a no-op. `PrintChar` never writes only part of a width-two scalar. `Clear` writes empty `Char` cells, uses its argument as background, and resets foreground to white. Before any helper changes a cell, it repairs an existing width-two pair as described above.

`RenderBuffer` validates every scalar, mergeable lookup, and width-two leading/continuation pair with `CHECK_ERROR`. A `Char` cell must contain zero or a scalar whose `MeasureChar` result is one or two; reject standalone control/zero-width cells so direct buffer writes cannot inject terminal control bytes. Then submit the whole logical buffer through the selected platform backend. It does not invoke user callbacks.

## File layout

Put the implementation in `Source/TUI`:

- `TUI.h`
- `TUI.cpp`
- `TUI.Windows.cpp`
- `TUI.Linux.cpp`
- `TUI.macOS.cpp` only if the macOS implementation cannot share the POSIX implementation cleanly

Keep Unicode lookup, buffer manipulation, drawing, color quantization, and width classification in shared code. Keep terminal acquisition, state changes, waiting, input decoding, resize notification, and output submission in platform files.

Use an internal `ITuiBackend` boundary for terminal acquisition, wait/read, size query, rendering, and cleanup. Production selects the Windows or POSIX backend; a unit-test-only injection hook supplies a deterministic fake backend and queued events. Make the test interface plus a scoped install/reset hook reachable under a clearly test-only `vl::console::unittest` namespace in the codepacked VlppOS Release header, so downstream GacUI tests using the imported release can install it; reject installation while TUI is active. The normal production API remains the static `TUI` class, and no backend owns a thread.

Implement all three platforms. Verify Windows now; Linux and macOS runtime verification will be completed later.

## UnitTest project

Add `TestTui.cpp` to the existing UnitTest project and place it in a `TUI` filter in `UnitTest.vcxproj.filters`. Enumerate every new TUI source/header and test file explicitly in the affected `.vcxproj` and `.vcxproj.filters`; do not use wildcards. Buffer-explicit helper overloads begin with:

```C++
TuiPixel* buffer, vint width, vint height
```

They allow drawing tests without activating a terminal.

Cover:

- Every `None`/thin/thick arm state and all supported/unsupported double states.
- Exact `GetChar32` and platform-dependent `GetWChar` behavior.
- Invalid scalar values and width-one/width-two placement.
- Continuation-pair repair, clipping, and resize normalization.
- Line, rectangle, rounded rectangle, merge fallback, background, foreground, and clear semantics.
- Timer and `Start`/`RunOneCycle`/`Stop` callback ordering through the injected fake backend, with every callback thread id equal to the `Start` thread id.
- `IsStopRequested` before, during, and after shutdown, including a bridge-style callback that suppresses later nested callbacks after a direct `Stop`.
- Reentrant `RunOneCycle`, including a modal-style nested pump with already-queued events.
- Repeated `Start`/`Stop`, stop requests from every callback, partial initialization failure, callback exceptions, and Console cleanup.
- Listener duplicate/removal behavior and mutation during ordinary and nested dispatch.

`TestTui.cpp` should be saved in UTF-8 with BOM, therefore MSBuild and some editors could handle it properly. And during testing drawing helper functions, you can do extra run to convert all pixels to chars, and compare with a two dimensional string in C++ representation, to make the test easy to read:
```C++
const auto stringLiteralCodedInTwoDimention = 
  "firstRow"
  "secondRow"
  ...;
```

## TuiPlayground project

Create `Test/UnitTest/TuiPlayground/TuiPlayground.vcxproj` and its `.filters` by following an existing portable CLI project. Add every Debug/Release and Win32/x64 configuration to `UnitTest.sln`, add the matching `Test/Linux/TuiPlayground/vmake` configuration, and update `Project.md` with its Windows and Linux/macOS verification commands.

The playground draws a white double-line rectangle on a black background around the current visible terminal. Resizing the terminal redraws the rectangle at the new border. Provide a character callback that calls `TUI::Stop` for the selected exit input so the playground demonstrates normal restoration.

## Generated and downstream integration

After implementing:

1. Regenerate the Vlpp release and copy its generated C++ source files, excluding `IncludeOnly`, into `VlppOS/Import`.
2. Regenerate `VlppOS/Release` from its `CodegenConfig.xml`.
3. During GacUI Phase 2, copy the generated VlppOS release source files, excluding `IncludeOnly`, into `GacUI/Import`; never hand-edit either import directory.
4. Build and run all tests required by each changed repo's `Project.md`, including the Vlpp Console tests and VlppOS UnitTest/TuiPlayground verification.

# UPDATES

## UPDATE

The overall implementation looks good but I would like you to do a small refactoring.

- Remove `BEGIN_GLOBAL_STORAGE_CLASS` used in TUI and put them in TUI class direcrtly.
  - `Start` to allocate and `Stop` to deallocate.
  - You can simplify this by forward declare an `Impl` class, or just declare `Impl` in the class. I don't really care if the details are exposed in the header file. But if you need extra members per platform, I think declaring `Impl` in the shared header would make it simpler as in each platform specific file you can inherit from it.
- Remove `GenerateUnicodeWidthTable.py`, `UnicodeWidthTable.cpp` should calls platform API instead. So they might just go to platform specific files. Even if the result could be the same across platform.

# TEST [CONFIRMED]

Add deterministic unit coverage for the requested Console enable/disable guard, every TUI pixel and drawing rule, lifecycle/callback/timer/listener/reentrancy contract, resize and cleanup behavior, and backend injection boundary. The tests reproduce the missing feature until the upstream Console API and VlppOS TUI implementation exist and pass.

Build and run the upstream Vlpp Console tests, regenerate and import the Vlpp release, build and run the VlppOS UnitTest project, regenerate VlppOS Release, and build/run the TuiPlayground interactive verification. Success requires all Windows Debug x64 tests to pass without memory leaks; all solution configurations must compile; generated artifacts must match their source repositories; the playground must render, resize, stop on its exit character, and restore the terminal. Linux and macOS implementations must compile from shared project metadata, while runtime verification is deferred by the task.

The upstream Vlpp build fails in `TestConsole.cpp` because `vl::console::Console` does not define `Enable`, `Disable`, or `IsEnabled`. The VlppOS build fails in `TestTui.cpp` because `Source/TUI/TUI.h` does not exist. These failures directly confirm that neither side of the requested ownership boundary has been implemented. The new tests also define the success conditions for scalar widths, pixels, drawing, lifecycle ordering, reentrant dispatch, timers, listener generations, exception cleanup, and the injectable backend.

# PROPOSALS

- No.1 IMPLEMENT THE OWNER-THREADED TUI CONTRACT WITH PLATFORM BACKENDS [CONFIRMED]

## No.1 IMPLEMENT THE OWNER-THREADED TUI CONTRACT WITH PLATFORM BACKENDS

Add the non-nested Console enable state at its owning layer in upstream Vlpp, guard the four platform sink operations, verify the change there, and regenerate/import the release. Implement `vl::console::TUI` in VlppOS as a shared owner-thread dispatcher and buffer/drawing engine behind one injected backend interface. The shared layer owns listener generations, queued decoded events, stop and exception latches, reentrant cycles, timer deadlines, resize preservation, exact box-drawing lookup, merge fallback, validation, and drawing helpers. Windows and POSIX code additionally owns terminal acquisition, saved-state transitions, blocking input/resize waits, decoding, platform character-width measurement, color-mode selection/quantization, output, and cleanup.

Use scope-based cleanup around every successful acquisition and around Console disablement so normal stop, partial startup failure, ordinary callback exceptions, and `Stopping` exceptions all restore the terminal and Console. The production backend factory selects the platform backend, while `vl::console::unittest::ScopedTuiBackend` temporarily installs a deterministic fake backend only while TUI is inactive. Add the portable TuiPlayground project and enumerate every source/test/project entry explicitly. Generate releases only from their source repositories and copy generated dependency artifacts downstream.

### CODE CHANGE

In upstream Vlpp, add the idempotent, process-wide `Console::Enable`, `Console::Disable`, and `Console::IsEnabled` state and guard the Windows and POSIX implementations of the four Console sinks. Regenerate the Vlpp release and copy the generated non-`IncludeOnly` files into `VlppOS/Import`.

In VlppOS, add the complete public TUI pixel, event, listener, timer, drawing, lifecycle, and test-backend API under `Source/TUI`. The shared implementation owns exact box-drawing lookup, merge fallback, width-two repair, Unicode scalar validation, color quantization, listener generations, queued events, reentrant owner-thread dispatch, timer deadlines, stop/exception latches, and scope-based cleanup. The Windows backend saves and restores console modes, cursor and palette state, uses virtual-terminal rendering when available with a classic-console fallback, translates console input records, measures scalar widths with Win32 character-type APIs, and reports resizes. The shared POSIX backend saves and restores `termios`, installs a self-pipe-backed `SIGWINCH` notification, polls input/timer/resize deadlines, decodes UTF-8 and terminal escape sequences, measures scalar widths with `wcwidth`, and emits SGR rendering for Linux and macOS.

After the requested refactoring, replace the `BEGIN_GLOBAL_STORAGE_CLASS(TuiStorage)` object with class-owned `TUI::Impl*`, listener-storage, and injected-backend members. Allocate the active `Impl` for each `Start`, release it after the stop/exception cleanup boundary, and retain only listener registrations and the scoped test-backend binding across starts. Remove `GenerateUnicodeWidthTable.py` and `UnicodeWidthTable.cpp` plus every project/filter reference. Add a deterministic injected backend and 23 focused TUI cases covering every requested pixel/drawing/lifecycle contract. Add `TuiPlayground` to all Visual Studio configurations and the Linux vmake metadata, update the existing Linux unit-test metadata, document its verification commands, and regenerate all VlppOS release/`IncludeOnly` files.

The repository's required 32-bit `Codepack.backup.exe` terminates with status `0xC0000409` while scanning this repository, including from the clean pre-implementation commit with TUI excluded. Build the current CodePack source as x64 and use that same CodePack implementation with `Release/CodegenConfig.xml`; it completes without categorization errors and emits all six expected release files.

### CONFIRMED

The upstream Vlpp Debug x64 solution builds with zero warnings and zero errors. Its complete unit suite passes 32/32 files and 464/464 cases with an empty memory-leak report, including the initial/repeated Console state transitions and every guarded sink. The regenerated Vlpp release and the four imported VlppOS files contain the same Console contract.

The refactored TUI has no global-storage macro, Unicode generator, generated width table, or stale MSBuild/IncludeOnly reference. `TUI::Impl` owns only one active run and its destructor preserves backend and Console cleanup on normal stop, partial initialization failure, and callback exceptions. A separate explicitly allocated listener registry preserves listener generations across repeated starts, while `ScopedTuiBackend` owns the injected backend through its scope without creating a constructed global object.

On Windows, `TUI::MeasureChar` uses `GetStringTypeW` to reject controls and nonspacing marks and distinguish narrow/full-width character classes, with surrogate-pair scalars occupying two cells. The POSIX implementation delegates to `wcwidth`. The regenerated release places these implementations in `VlppOS.Windows.cpp` and `VlppOS.Linux.cpp`, with no width arrays or generator text in the common release.

The complete VlppOS solution builds in Debug x64, Release x64, Debug Win32, and Release Win32 with zero warnings and zero errors. The full Debug x64 suite passes 16/16 files and 244/244 cases with an empty memory-leak report; all 23 TUI cases pass. These cases exhaust all 81 none/thin/thick arm states and every double-containing state, validate supported and unsupported mappings, scalar widths and wide pairs, drawing and merge semantics, resize preservation, malformed-buffer rejection, listener generations, all callback stop paths, reentrant cycles, timer deadlines, owner-thread affinity, exception cleanup, Console restoration, and backend interaction.

In a newly allocated real Windows console, `TuiPlayground` rendered the expected `╔╗╚╝` corners at 120x30, accepted `q`, exited with code zero, and restored the terminal. The active terminal host did not allow its physical window to be resized programmatically during that probe, so resize redraw is additionally confirmed by the deterministic backend test that changes the reported size, verifies buffer replacement/preservation and cut-wide-character repair, and observes `BufferSizeChanged`. Linux and macOS use the shared POSIX source and have complete project metadata, but their runtime verification remains deferred as explicitly required by this phase.
