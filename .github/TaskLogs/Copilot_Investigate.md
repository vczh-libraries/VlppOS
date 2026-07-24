# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

Complete [`TODO_Task_TuiRefactor.md`](./TODO_Task_TuiRefactor.md) first. This task consumes its native-`wchar_t` character-event contract while keeping painter text and drawing operations scalar-based.

You are going to improve the `TuiPlayground` project to make it a real playground for manual testing:
- The TUI playground becomes an ASCII art painting app by typing commands, put the usage in `Project.md`.
- Commit and push all local changes after finishing.

Before actually implementing, fix a bug first. On Windows, `TUI` does not expand to the whole window because the console window always displays a vertical scroll bar. `TUI` does not scroll because it uses the buffer for rendering. Disable the vertical scroll bar so that `TUI` appears to take over the whole area.

Now implement the ASCII art painter like this:
- Automatic layout happens when the console window is resized.
- At the very bottom there will be a text box:
  - The background will be dark gray, making it distinct from the painting area and indicating that it is for typing.
  - A cursor will flash every half second at the end of the typed text, indicating where the next character will appear.
  - No actual cursor editing is supported. Pressing Backspace is the only way to delete typed text, and it deletes the last character.
  - Character width must be respected while implementing automatic line wrapping. Decode native `wchar_t` character-event units into complete `char32_t` scalar values first. Complex scripts do not need to be supported; treat the input as a simple left-to-right script and render each scalar separately. When multiple lines are needed, the text box expands or shrinks immediately. If the last line is completely occupied, render the cursor on the next line.
- The rest of the area will be the ASCII art display area:
  - Its border is always a double-line rectangle, just like it is now.
  - The app remembers all valid typed commands and replays them whenever repainting is needed. The border clips anything outside the painting area, including anything that would overwrite the border itself. The paper does not scroll; its `(0, 0)` is always at terminal coordinate `(1, 1)` because the double-line border occupies one cell.
- Here are all commands to support. Command and format names are case-insensitive:
  - `FC FFFFFF`: set foreground color using HTML color format without '#', default to white.
  - `BC CLEAR|000000`: background color, default to clear.
  - `LINEV FORMAT x y1 y2`, FORMAT would be `THIN|THICK|DOUBLE`.
  - `LINEH FORMAT x1 x2 y`
  - `RECT FORMAT x1 y1 x2 y2`, FORMAT would be `THIN|THICK|DOUBLE|ROUND`.
  - `CLEAR FFFFFF x1 y1 x2 y2`, clear a rectangle using the specified background color.
  - `TYPE x y:TEXT`, put text without automatic line wrapping.
- Parsing is strict and happens when the user presses Enter.
- When parsing fails, display an error message like:
  - `ERROR, original command:xxxx`
  - `REASON:xxxx`
  - Display a rounded rectangle occupying the available painting width and align it vertically in the center.
  - The error has two logical text lines. Both support line wrapping, and each wrapped line is horizontally centered.
  - When the user presses Enter again, the error disappears and the user can continue typing.
- Regardless of success or failure, clear the command text box immediately after submission.

Data structure:

Here is the suggested data structure to store everything in the UI, so that when the console window resized, repaints could happen and render everything correctly, including:
- Rendering error message, or empty.
- Typing commands.
- All accumulated valid commands.

```C++
struct SetForegroundColorCommand
{
	TuiColor						color = { 255, 255, 255 };
};

struct SetBackgroundColorCommand
{
	Nullable<TuiColor>				color; // null means BC CLEAR
};

struct DrawLineVCommand
{
	TuiMergeableGlyph				glyph = TuiMergeableGlyph::ThinLine;
	vint							x = 0;
	vint							y1 = 0;
	vint							y2 = 0;
};

struct DrawLineHCommand
{
	TuiMergeableGlyph				glyph = TuiMergeableGlyph::ThinLine;
	vint							x1 = 0;
	vint							x2 = 0;
	vint							y = 0;
};

struct DrawRectCommand
{
	TuiMergeableGlyph				glyph = TuiMergeableGlyph::ThinLine;
	TuiRectCorner					corner = TuiRectCorner::Sharp;
	vint							x1 = 0;
	vint							y1 = 0;
	vint							x2 = 0;
	vint							y2 = 0;
};

struct ClearRectCommand
{
	TuiColor						backgroundColor = { 0, 0, 0 };
	vint							x1 = 0;
	vint							y1 = 0;
	vint							x2 = 0;
	vint							y2 = 0;
};

struct TypeCommand
{
	vint							x = 0;
	vint							y = 0;
	U32String						text;
};

using PaintingCommand = Variant<
	SetForegroundColorCommand,
	SetBackgroundColorCommand,
	DrawLineVCommand,
	DrawLineHCommand,
	DrawRectCommand,
	ClearRectCommand,
	TypeCommand
	>;

struct CommandError
{
	U32String						originalCommand;
	U32String						reason;
};

struct PlaygroundState
{
	Nullable<CommandError>			error;
	U32String						typingCommand;
#if defined VCZH_WCHAR_UTF16
	wchar_t							pendingHighSurrogate = 0;
#endif
	collections::List<PaintingCommand>	commands;
	bool							cursorVisible = true;
};
```

## DETAILS

- `PlaygroundState` is owned directly by the TUI callback. Do not store a `TuiPixel*`, terminal dimensions, wrapped lines, translated coordinates, or a cached painted canvas in it because all of these values are derived during repaint and the TUI buffer pointer is invalidated by resize. A temporary painting-interior buffer allocated for one repaint is allowed.
- `commands` contains every successfully parsed command, including `FC` and `BC`, in submission order. Every repaint starts with white foreground and a clear background, then replays this list. Do not persist a second copy of the current foreground or background.
- An empty `SetBackgroundColorCommand::color` represents `BC CLEAR`; a present black `TuiColor` represents `BC 000000`. A clear background means that line and rectangle operations preserve the destination background. `TYPE` also preserves the destination background; for a width-two character, use the leading destination cell's background for both cells.
- `ROUND` is stored as `TuiMergeableGlyph::ThinLine` with `TuiRectCorner::Round`. All other `RECT` formats use `TuiRectCorner::Sharp`. Reject any other combination while parsing.
- Store coordinates in logical paper coordinates without translation. Signed coordinates outside the paper are valid. During each repaint, replay the original coordinates into a temporary interior-sized buffer using the buffer-explicit TUI drawing helpers so clipping happens in logical space without translated-integer overflow or invented edges at a clipped rectangle boundary. Copy the bounded interior cells into the active buffer at terminal `(1, 1)`, then draw the outer border so commands cannot overwrite it or the command text box.
- `TuiCharInfo::code` is one native `wchar_t` unit after the prerequisite refactoring, while `TuiPixel::c` and the drawing and measurement APIs remain `char32_t`. Convert character events to zero or one complete `char32_t` scalar before appending, measuring, wrapping, parsing, or capturing an error command. Under `VCZH_WCHAR_UTF16`, retain a high surrogate in `pendingHighSurrogate` and combine it only when the next `Char` event supplies a low surrogate, using `vl::encoding::UtfConversion<wchar_t>::To32`. Key, mouse, timer, resize, and repaint callbacks neither flush nor invalidate the pending unit. An isolated low surrogate is ignored. If the next character unit after a pending high surrogate is anything else, discard the pending unit and reprocess the current unit; if the current unit is another high surrogate, it becomes the new pending unit. Under `VCZH_WCHAR_UTF32`, convert the one native unit directly and validate the resulting scalar.
- Do not use `vl::encoding::UtfToUtfReaderBase` or `vl::encoding::UtfStringToStringReader` across callbacks; they cannot retain and resynchronize an incomplete event-by-event UTF-16 sequence. Never append or measure an individual surrogate.
- Use `U32String` for typed commands, `TYPE` payloads, and error strings so Backspace removes one completed `char32_t` on every platform. After native-unit conversion, treat U+0008 and U+007F as Backspace, U+000D and U+000A as Enter, and U+001B as Escape. A valid control following an unmatched high surrogate must still execute after the unmatched unit is discarded. The physical Delete key remains unsupported because `TuiKeyInfo::code` is reserved for future key-code translation. Use `TUI::MeasureChar` for input wrapping and cursor placement. Other than these controls, ignore input that cannot be rendered as an independent width-one or width-two scalar.
- Parse exactly the command shapes shown above with one ASCII space at every displayed separator, no leading whitespace, no trailing whitespace outside a `TYPE` payload, exactly six hexadecimal color digits, signed decimal coordinates checked for `vint` overflow, and ordered ranges accepted by the corresponding TUI drawing helper. Compare only command names, formats, `CLEAR`, and hexadecimal digits case-insensitively. Preserve the `TYPE` payload after the required colon exactly, including case, spaces, additional colons, and non-BMP characters. Submitting an empty command and submitting `TYPE x y:` without at least one payload scalar are parse errors.
- Escape exits the playground. `q` and `Q` are ordinary command text. Reset `pendingHighSurrogate` whenever the command box is cleared or submitted and when TUI starts or stops. While an error is displayed, Enter dismisses it without submitting an empty command, Escape exits, and every other native character unit is ignored without creating pending UTF-16 state. Clear pending state again when the error is dismissed. The typing command has already been cleared, and the blinking cursor is hidden until the error is dismissed.
- Draw the error overlay inside the painting interior so the outer double-line border remains unchanged. Wrap the `ERROR` and `REASON` logical lines in source order, cap the overlay to the available painting height, and display the leading wrapped rows that fit; clipped rows become visible after a later enlarging resize. Skip the overlay border or text portions that do not fit rather than writing into the outer border or command text box.
- Start the TUI timer with a 500-millisecond period. Each timer callback toggles `cursorVisible` and repaints. All state remains owner-thread-only; do not add atomics, locks, or worker threads.
- Cap the command text box at the terminal height and display its trailing wrapped rows so that the cursor stays visible. The painting area may become empty. If the terminal is narrower than a width-two scalar, retain the scalar in `typingCommand` but omit it until a later resize makes it renderable. Draw the painting border or error rectangle only when the available dimensions satisfy the corresponding TUI drawing helper.
- On Windows, remove the vertical scroll bar without preventing the visible window from growing or shrinking. Do not assume that shrinking the screen buffer once during startup is sufficient; keep any buffer-based solution synchronized with viewport changes. Queue a resize event whenever the visible viewport dimensions change, including viewport-only changes that do not produce `WINDOW_BUFFER_SIZE_EVENT`; checking after console input or an existing timer deadline is acceptable, but do not add busy polling. Keep both virtual-terminal and classic output paths correct. Save and restore the original screen-buffer dimensions and window geometry on normal shutdown, partial backend startup failure, and callback exceptions. Respect the Win32 ordering constraints between window and buffer resizing, and keep partial-startup rollback inside the Windows backend because generic TUI cleanup begins only after backend startup succeeds.

## VERIFICATION

- Build `Test/UnitTest/UnitTest.sln` in Debug x64 using `.github/Scripts/copilotBuild.ps1`. Run the complete `UnitTest` project using `.github/Scripts/copilotExecute.ps1`, and check the final log for failures and Debug memory leaks.
- Start `TuiPlayground` through `.github/Scripts/copilotExecute.ps1` from a Windows console that has a scrollback buffer taller than its visible window. Confirm that no vertical scroll bar remains while TUI is active, repeatedly growing and shrinking the window continues to report and use the full visible area without bringing the scroll bar back, and the original buffer/window geometry and terminal state return after Escape.
- Verify the 500-millisecond cursor blink, both platform Backspace character codes, width-one and width-two input wrapping, an exactly full final row placing the cursor on the next row, text-box growth and shrinkage, a one-column terminal retaining width-two input for a later resize, and layouts in which the text box consumes the whole terminal.
- On a UTF-16 Windows build, feed a valid supplementary-character high/low pair and confirm that it becomes one `U32String` scalar, is measured and rendered as one width-two character, survives in `TYPE` payloads and error text, and is removed by one Backspace. Cover an isolated low surrogate, high-plus-high, high-plus-BMP, and a high surrogate followed by each supported control; malformed units must be ignored without swallowing the following valid scalar or control. Confirm that ignored input while an error is visible cannot leave pending state after dismissal.
- On UTF-32 Linux/macOS builds, confirm that one native `wchar_t` event takes the direct one-scalar path and produces the same painter state.
- Submit every command and format in lower-, upper-, and mixed-case forms. Combine `FC`, `BC CLEAR`, `BC 000000`, overlapping lines and rectangles, `CLEAR`, and `TYPE` so foreground replacement, background preservation, wide-character placement, and command ordering are all visible.
- Resize the terminal larger and smaller after multiple commands. Confirm that commands replay from logical paper `(0, 0)`, offscreen content clips at the painting interior, the border and command text box are never overwritten, and newly visible content is reconstructed from command history.
- Verify strict-parser failures for an empty submission, an empty `TYPE` payload, bad keywords, formats, whitespace, color lengths/digits, missing fields or colon, integer overflow, and invalid ordered ranges. Confirm that the overlay preserves the original Unicode command, displays a useful reason as two centered wrapping logical lines inside a thin rounded rectangle, stays inside the outer border, clips predictably in a short painting area, clears the command text, ignores non-Enter/non-Escape input, and disappears on Enter.
- Confirm that `q` and `Q` can be typed and Escape exits. Keep the existing injected-backend coverage for generic startup/callback failure and Console cleanup, but do not treat it as verification of real Win32 geometry restoration; verify the production Windows backend's normal restoration manually.
- Update `Project.md` with the complete command grammar, input/error behavior, Escape exit key, and Windows/Linux/macOS build and manual verification instructions. Keep the source and build configurations portable. Perform Windows runtime verification now; build and repeat all platform-independent runtime checks on Linux and macOS when those hosts are available.

## REVIEW COMMENTS

# UPDATES

# TEST [CONFIRMED]

Use the existing injected TUI backend for deterministic playground behavior and the production Win32 backend for the geometry contract:

- Compile `Test/UnitTest/TuiPlayground/Main.cpp` into `Test/Source/TestTui.cpp` under a test-only main-function guard so the tests exercise the executable's actual callback and state without duplicating painter logic or adding source files.
- Capture the last fake-backend frame and verify the current implementation reproduces two root symptoms before the proposal: `q`/`Q` stop event processing instead of becoming command text, and the bottom row has the painting area's black background instead of a distinct dark-gray command box.
- Expand the committed playground suite with strict-parser coverage for every command and format in lower, upper, and mixed case; exact whitespace and token shapes; six-digit colors; signed `vint` limits and overflow; ordered ranges; empty input and empty `TYPE` payload; and exact preservation of payload case, spaces, colons, and supplementary scalars.
- Drive native character events through the fake backend. Under UTF-16, cover a valid high/low pair, isolated low, high/high, high/BMP, and high followed by Backspace, Delete-character Backspace, Enter, and Escape. Verify malformed input does not swallow the next scalar or control, one Backspace removes one completed scalar, ignored error-overlay input leaves no pending unit, and `q`/`Q` are ordinary input. Keep the corresponding direct one-unit UTF-32 path compiled for Linux/macOS.
- Capture frames at multiple fake terminal sizes. Verify 500-millisecond cursor timing, width-one and width-two wrapping, a full last row moving the cursor to the next row, immediate command-box growth/shrinkage, trailing-row display when capped to terminal height, one-column retention of a width-two scalar, empty painting layouts, a dark-gray command background, and a hidden cursor during errors.
- Submit and replay `FC`, `BC CLEAR`, `BC 000000`, every line/rectangle style, `CLEAR`, and `TYPE`. Verify command ordering, foreground replacement, background preservation, width-two background selection, logical coordinates, offscreen clipping, repaint reconstruction after resize, and protection of the outer border and command box.
- Verify error overlays preserve the original Unicode command, render `ERROR` then `REASON` as horizontally centered wrapped rows in a thin rounded rectangle, remain inside the painting interior, show leading rows under height clipping, clear the command box, ignore non-Enter/non-Escape input, dismiss on Enter, and exit on Escape.
- Build the complete Debug x64 solution through `copilotBuild.ps1`, run the complete `UnitTest` project through `copilotExecute.ps1`, require every file/case to pass, and inspect the log tail for Debug memory leaks.
- Run `TuiPlayground` through `copilotExecute.ps1` in a real Windows console initialized with a scrollback buffer taller than its viewport. Inspect Win32 buffer/window geometry and screen cells while active; repeatedly grow and shrink the viewport; require the active buffer to match the visible viewport, no vertical scrollbar condition, repaint dimensions to follow, and the original buffer size, window rectangle, modes, and terminal contents to return after Escape.
- In the same production-console run, exercise command entry, cursor blinking, both Backspace character values, width-one/width-two wrapping, supplementary input, successful command combinations, strict failures, overlay dismissal, resize replay, `q`/`Q`, and Escape. Windows runtime evidence is required now; Linux/macOS builds and platform-independent runtime checks remain explicitly deferred unless those hosts are available.
- Verify `Project.md` contains the complete grammar and input/error/exit behavior plus Windows, Linux, and macOS build/run instructions. If shared TUI source changes, regenerate the complete `Release` output through the repository's existing code-generation workflow and verify the generated Windows implementation matches source.

Passing all automated and production-console checks, with zero compiler warnings/errors, no failed tests, no Debug leak dump, and exact geometry restoration, confirms the task.

The initial Debug x64 solution build completed with zero warnings and zero errors. The complete pre-fix unit-test run reached the new `TuiPlayground regression` category and failed at `q and Q are command text and only Escape exits`: the existing callback stopped on `q`, leaving the queued `Q` and Escape events unprocessed. The existing redraw implementation also clears the entire frame to black and draws only the outer rectangle, so it cannot produce the required dark-gray bottom command row. These executable-level checks reproduce the current playground behavior without copying its implementation into the test.

# PROPOSALS

- No.1 SYNCHRONIZE THE WINDOWS VIEWPORT AND REPLAY A SCALAR-BASED PAINTER [CONFIRMED]

## No.1 SYNCHRONIZE THE WINDOWS VIEWPORT AND REPLAY A SCALAR-BASED PAINTER

Make the Windows backend the sole owner of console geometry while TUI is active. Save the original screen-buffer size and window rectangle before either output path starts. After entering the VT alternate screen, or before activating the classic alternate buffer, resize the active buffer and zero-based window together in Win32-safe order so the buffer exactly matches the visible viewport. Track the last visible dimensions and resynchronize after console input and timer-deadline wakeups; this removes scrollbars after shrink, permits host-driven growth, and produces a resize event even when only `srWindow` changed. On every shutdown path, reactivate/leave the alternate screen first as appropriate, then restore the original buffer and window geometry while still restoring cursor and input/output modes. Keep startup rollback inside `WindowsTuiBackend::Start`.

Replace the playground's border-only callback with the requested owner-thread state machine. Keep only `CommandError`, the completed-scalar typing string, the optional UTF-16 high unit, accumulated `Variant` commands, and cursor visibility as durable state. Decode each native unit with `UtfConversion<wchar_t>::To32`, resynchronizing malformed UTF-16 without swallowing the current BMP scalar or control; validate UTF-32 scalars directly. Parse fixed ASCII grammar manually so exact separators, overflow, ordered ranges, case-insensitive keywords/formats/colors, and the untouched `TYPE` payload are explicit.

Derive layout on every repaint. Measure completed scalars, retain but omit a width-two scalar when the terminal is one column, move an exactly-full cursor to the next row, cap the command box to terminal height, and show trailing wrapped input rows on a dark-gray background. Allocate an interior-sized temporary paper only for that frame, reset paint style to white/clear, replay every command in order through buffer-explicit drawing helpers using the original signed logical coordinates, and copy bounded cells to terminal `(1,1)`. Preserve destination backgrounds for clear-style lines/rectangles and `TYPE`, then draw the outer double border and command box so replay cannot overwrite either.

When parsing fails, clear input immediately and derive two wrapped logical error lines. Draw the leading rows that fit, horizontally centered, inside a vertically centered thin rounded rectangle spanning the available painting-interior width; omit border/text portions that cannot fit. Hide the cursor and ignore all input except Enter dismissal and Escape exit without retaining UTF-16 state. Start a 500-millisecond TUI timer, toggle cursor visibility on every deadline, and repaint on every character, Backspace, submission, dismissal, timer, or resize.

Retain the test-only inclusion guard so `TestTui.cpp` exercises the executable's actual internal callback. Expand its fake backend to capture frames and add command/state/frame cases for parser strictness, native-unit decoding, wrapping/cursor/error behavior, replay/clipping/background semantics, resize reconstruction, and timer configuration. Document the complete user workflow and all three platform commands in `Project.md`. Regenerate `Release/VlppOS.Windows.cpp` from `Release/CodegenConfig.xml`, build/run the complete suite, and perform the real Win32 console geometry and interaction verification required by the test plan.

### CODE CHANGE

- Replace the border-only `TuiPlayground` callback with the requested `PlaygroundState` and command variants. Add strict, case-insensitive command parsing with exact separators, six-digit colors, overflow-checked signed `vint` coordinates, ordered-range validation, and exact `TYPE` payload preservation.
- Decode each native `wchar_t` event into zero or one scalar before input handling. The Windows UTF-16 path retains one high surrogate, combines only a valid following low surrogate through `UtfConversion<wchar_t>::To32`, and reprocesses a valid current unit after malformed input. Store input, payloads, and errors as `U32String`; make U+0008/U+007F Backspace, CR/LF Enter, and Escape exit while leaving `q`/`Q` as ordinary text.
- Derive wrapped input rows and cursor placement on every repaint, including the exact-full-row next-line cursor, trailing-row display under height capping, and retention/temporary omission of width-two input in a one-column terminal. Draw the text box dark gray, start a 500-millisecond timer, and hide the cursor while an error is active.
- Replay all accumulated commands in submission order into a temporary painting-interior buffer from white/clear defaults. Use the buffer-explicit TUI drawing helpers with original signed logical coordinates, preserve destination backgrounds for clear-style drawing and `TYPE`, copy only bounded interior cells to terminal `(1,1)`, and draw the double-line outer border afterward.
- Render parse failures as two source-order, independently wrapped and centered Unicode lines inside a vertically centered thin rounded rectangle in the painting interior. Cap to leading rows that fit, keep the command box clear, ignore non-Enter/non-Escape input without retaining UTF-16 state, and dismiss the overlay on Enter.
- In `WindowsTuiBackend`, save the original buffer/window geometry before changing modes. Add Win32-safe window/buffer resizing, synchronize the active buffer to the zero-origin visible viewport at startup and after input or timer deadlines, and queue resize events for viewport-only changes. Cover both VT and classic alternate-screen paths and preserve the original startup exception during backend-local rollback.
- Restore VT/classic output, cursor, modes, buffer size, and window rectangle on every shutdown path. The VT path requests the original terminal viewport before leaving the alternate screen, waits for the asynchronous terminal transition to stabilize, and then applies the original Win32 buffer/window geometry. Regenerate `Release/VlppOS.Windows.cpp` through CodePack.
- Include the executable implementation in `TestTui.cpp` under `VCZH_TUI_PLAYGROUND_TEST`, expand the fake backend to capture frames/timer waits, and add deterministic parser, UTF-16/UTF-32, wrapping, cursor, replay, clipping, background, resize, and error-overlay cases.
- Document the complete painter grammar, input/error behavior, Escape-only exit, cross-platform build/run commands, and manual verification procedure in `Project.md`.

### CONFIRMED

The pre-fix tests reproduced both root symptoms: the old callback stopped on `q` before consuming `Q` or Escape, and its bottom row retained the painting area's black background. The implementation now passes the expanded `TuiPlayground regression` category, including every command shape and format, strict malformed inputs and integer limits, exact `TYPE` payloads, native-unit resynchronization and controls, width-aware wrapping/cursor behavior, 500-millisecond blinking, command replay/background/clipping semantics, resize reconstruction, and Unicode error-overlay behavior.

The final Debug x64 solution build completed with zero warnings and zero errors. The complete unit-test run passed 16/16 files and 259/259 cases. The final execution log contains no failed-case or Debug memory-leak marker, and `git diff --check` reports no whitespace error.

`TuiPlayground` was launched only through `copilotExecute.ps1` in a Windows pseudoconsole initialized with a 70x50 buffer and 70x18 viewport. While active, the buffer and viewport both became 70x18, establishing the no-scrollback/no-vertical-scrollbar condition. The production probe observed both phases of the 500-millisecond cursor, kept `qQ` as input, removed them with U+0008 and U+007F, submitted color/clear/rounded-rectangle/Unicode `TYPE` commands, and verified replayed text at logical paper coordinates.

The same production run resized the real terminal through its standard resize control from 70x18 to 50x12, 60x15, 45x10, and 55x14. Every transition produced an exact buffer/viewport match and replayed the retained commands. A Unicode invalid command displayed both `ERROR` and `REASON`, ignored ordinary input, dismissed on Enter, and Escape exited with code zero while restoring the original viewport and sentinel screen contents.

To separate backend restoration from the wrapper's own child-exit scrollback adjustment, one temporary post-`TUI::Start` pause was built for inspection and then removed. While the actual `TuiPlayground` process was still alive after Escape, Win32 reported the exact original 70x50 buffer and 70x18 viewport and the original sentinel contents. This exposed the VT host's asynchronous alternate-screen transition; requiring ten stable one-millisecond viewport samples before the final Win32 geometry call made restoration deterministic. The temporary pause and diagnostic instrumentation are absent from the final source.

CodePack regenerated all six release outputs from `Release/CodegenConfig.xml`; only the expected Windows implementation differs. `Project.md` now contains the complete grammar and Windows/Linux/macOS instructions. Linux and macOS runtime hosts are unavailable in this Windows session, so their shared painter implementation and conditional UTF-32 test path compile structurally but runtime execution remains deferred to those hosts as required.
