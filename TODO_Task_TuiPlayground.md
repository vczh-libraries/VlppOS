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
  - Character width must be respected while implementing automatic line wrapping. Complex scripts do not need to be supported; treat the input as a simple left-to-right script and render each `char32_t` separately. When multiple lines are needed, the text box expands or shrinks immediately. If the last line is completely occupied, render the cursor on the next line.
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
- Use `U32String` for typed commands, `TYPE` payloads, and error strings so Backspace removes one `char32_t` on every platform. Treat U+0008 and U+007F as Backspace, U+000D and U+000A as Enter, and U+001B as Escape. The physical Delete key remains unsupported because `TuiKeyInfo::code` is reserved for future key-code translation. Use `TUI::MeasureChar` for input wrapping and cursor placement. Other than these controls, ignore input that cannot be rendered as an independent width-one or width-two scalar.
- Parse exactly the command shapes shown above with one ASCII space at every displayed separator, no leading whitespace, no trailing whitespace outside a `TYPE` payload, exactly six hexadecimal color digits, signed decimal coordinates checked for `vint` overflow, and ordered ranges accepted by the corresponding TUI drawing helper. Compare only command names, formats, `CLEAR`, and hexadecimal digits case-insensitively. Preserve the `TYPE` payload after the required colon exactly, including case, spaces, additional colons, and non-BMP characters. Submitting an empty command and submitting `TYPE x y:` without at least one payload scalar are parse errors.
- Escape exits the playground. `q` and `Q` are ordinary command text. While an error is displayed, Enter dismisses it without submitting an empty command and all other character input is ignored. The typing command has already been cleared, and the blinking cursor is hidden until the error is dismissed.
- Draw the error overlay inside the painting interior so the outer double-line border remains unchanged. Wrap the `ERROR` and `REASON` logical lines in source order, cap the overlay to the available painting height, and display the leading wrapped rows that fit; clipped rows become visible after a later enlarging resize. Skip the overlay border or text portions that do not fit rather than writing into the outer border or command text box.
- Start the TUI timer with a 500-millisecond period. Each timer callback toggles `cursorVisible` and repaints. All state remains owner-thread-only; do not add atomics, locks, or worker threads.
- Cap the command text box at the terminal height and display its trailing wrapped rows so that the cursor stays visible. The painting area may become empty. If the terminal is narrower than a width-two scalar, retain the scalar in `typingCommand` but omit it until a later resize makes it renderable. Draw the painting border or error rectangle only when the available dimensions satisfy the corresponding TUI drawing helper.
- On Windows, remove the vertical scroll bar without preventing the visible window from growing or shrinking. Do not assume that shrinking the screen buffer once during startup is sufficient; keep any buffer-based solution synchronized with viewport changes. Queue a resize event whenever the visible viewport dimensions change, including viewport-only changes that do not produce `WINDOW_BUFFER_SIZE_EVENT`; checking after console input or an existing timer deadline is acceptable, but do not add busy polling. Keep both virtual-terminal and classic output paths correct. Save and restore the original screen-buffer dimensions and window geometry on normal shutdown, partial backend startup failure, and callback exceptions. Respect the Win32 ordering constraints between window and buffer resizing, and keep partial-startup rollback inside the Windows backend because generic TUI cleanup begins only after backend startup succeeds.

## VERIFICATION

- Build `Test/UnitTest/UnitTest.sln` in Debug x64 using `.github/Scripts/copilotBuild.ps1`. Run the complete `UnitTest` project using `.github/Scripts/copilotExecute.ps1`, and check the final log for failures and Debug memory leaks.
- Start `TuiPlayground` through `.github/Scripts/copilotExecute.ps1` from a Windows console that has a scrollback buffer taller than its visible window. Confirm that no vertical scroll bar remains while TUI is active, repeatedly growing and shrinking the window continues to report and use the full visible area without bringing the scroll bar back, and the original buffer/window geometry and terminal state return after Escape.
- Verify the 500-millisecond cursor blink, both platform Backspace character codes, width-one and width-two input wrapping, an exactly full final row placing the cursor on the next row, text-box growth and shrinkage, a one-column terminal retaining width-two input for a later resize, and layouts in which the text box consumes the whole terminal.
- Submit every command and format in lower-, upper-, and mixed-case forms. Combine `FC`, `BC CLEAR`, `BC 000000`, overlapping lines and rectangles, `CLEAR`, and `TYPE` so foreground replacement, background preservation, wide-character placement, and command ordering are all visible.
- Resize the terminal larger and smaller after multiple commands. Confirm that commands replay from logical paper `(0, 0)`, offscreen content clips at the painting interior, the border and command text box are never overwritten, and newly visible content is reconstructed from command history.
- Verify strict-parser failures for an empty submission, an empty `TYPE` payload, bad keywords, formats, whitespace, color lengths/digits, missing fields or colon, integer overflow, and invalid ordered ranges. Confirm that the overlay preserves the original Unicode command, displays a useful reason as two centered wrapping logical lines inside a thin rounded rectangle, stays inside the outer border, clips predictably in a short painting area, clears the command text, ignores non-Enter input, and disappears on Enter.
- Confirm that `q` and `Q` can be typed and Escape exits. Keep the existing injected-backend coverage for generic startup/callback failure and Console cleanup, but do not treat it as verification of real Win32 geometry restoration; verify the production Windows backend's normal restoration manually.
- Update `Project.md` with the complete command grammar, input/error behavior, Escape exit key, and Windows/Linux/macOS build and manual verification instructions. Keep the source and build configurations portable. Perform Windows runtime verification now; build and repeat all platform-independent runtime checks on Linux and macOS when those hosts are available.

## REVIEW COMMENTS
