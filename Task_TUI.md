# TUI platform implementation details

This file contains only the verified technical details needed to execute [`TODO_Task.md`](./TODO_Task.md) on Windows, Linux, and macOS. [`TODO_Task_TuiRefactor.md`](./TODO_Task_TuiRefactor.md) supersedes both files for the character-event contract; otherwise the public contract and task scope remain in `TODO_Task.md`.

## Common terminal assumptions

TUI requires an interactive terminal for input and output. Startup must fail cleanly before setting `IsInUse` when the standard handles are redirected or do not provide the required terminal operations.

Use a single portable `TuiStartOptions`. Capability detection is advisory:

- `Auto` selects the best backend indicated by the available API and environment.
- An explicit color request may be downgraded when the backend cannot provide it.
- Expose the selected emission/quantization mode. It is not a measurement of what the attached display ultimately renders.
- Keep RGB in `TuiPixel`; quantize only while producing terminal output.

Use this canonical ANSI approximation for indices 0 through 15: `{#000000, #800000, #008000, #808000, #000080, #800080, #008080, #C0C0C0, #808080, #FF0000, #00FF00, #FFFF00, #0000FF, #FF00FF, #00FFFF, #FFFFFF}`. For deterministic 256-color quantization, append the xterm 6x6x6 cube with component levels `{0, 95, 135, 175, 215, 255}`, not uniformly rounded sixths of 255, and grayscale entries `8 + 10 * n` for `n` from 0 through 23. Compare squared RGB distance against all candidate entries and choose the lowest palette index on a tie. Both 16- and 256-color palettes can be customized, so fixed tables are deterministic approximations unless the backend can query the active palette; the Windows classic backend should quantize against its queried active 16-color table.

All output builders must:

- Convert Unicode scalar values to the selected platform stream encoding.
- Skip `WideCharContinuation` cells in variable-length VT/UTF-8 streams. A rectangular `CHAR_INFO` fallback still emits one physical entry for every logical cell as specified below.
- Render an empty `Char` as a space.
- Coalesce adjacent cells with the same colors where practical.
- Handle partial writes and interrupted writes.
- Avoid claiming that one write or a screen-buffer swap makes a frame atomic or flicker-free.

## Unicode mapping and width

Use the official [Unicode Box Drawing names list](https://www.unicode.org/charts/nameslist/n_2500.html) to generate or verify the arm-state lookup. U+2500 through U+2503, U+250C through U+254B, and U+2574 through U+257F supply exact glyphs for all 80 nonempty none/thin/thick arm states; the all-none state maps to an empty cell. U+2550 through U+256C contains only the supported double and thin/double states described in `TODO_Task.md`.

Keep source spelling ASCII-only by using Unicode character literals or numeric code points such as `U'\u2500'`. These are Unicode box-drawing characters, not ASCII, and no UTF-7 source requirement exists.

There is no reliable Windows console API that returns the displayed terminal-cell width of an arbitrary Unicode scalar. [`GetStringTypeW(CT_CTYPE3)`](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-getstringtypew) returns character classifications, not terminal layout, and Microsoft notes incomplete supplementary-character data. POSIX `wcwidth` is locale-dependent and does not solve grapheme sequences.

Use one shared Unicode 17.0.0 scalar-width table so unit tests and buffer layout are deterministic:

- Invalid scalars, NUL, C0/C1 controls, `Noncharacter_Code_Point`, `Default_Ignorable_Code_Point`, and General Category `Mn`/`Me` return zero.
- East Asian Width `Wide` and `Fullwidth` scalars return two.
- Scalars with `Emoji_Presentation` return two.
- Other scalars, including East Asian Width `Ambiguous`, return one unless a future explicit width policy says otherwise.
- Force supported box-drawing code points to one and document this as a terminal/font compatibility requirement.

Check in the generator and generated table. Generate it from Unicode 17.0.0 [`EastAsianWidth.txt`](https://www.unicode.org/Public/17.0.0/ucd/EastAsianWidth.txt), [`UnicodeData.txt`](https://www.unicode.org/Public/17.0.0/ucd/UnicodeData.txt), [`PropList.txt`](https://www.unicode.org/Public/17.0.0/ucd/PropList.txt), [`DerivedCoreProperties.txt`](https://www.unicode.org/Public/17.0.0/ucd/DerivedCoreProperties.txt), and [`emoji-data.txt`](https://www.unicode.org/Public/17.0.0/ucd/emoji/emoji-data.txt), rather than copying a third-party implementation. Apply the zero-width rules before the width-two rules. [UAX #11](https://www.unicode.org/reports/tr11/) explains why East Asian Width is informative and context-dependent. [UAX #29](https://www.unicode.org/reports/tr29/) explains why a user-perceived grapheme can contain multiple code points; such graphemes remain outside this scalar-cell task.

## Owner-thread event loop

The backend must not create a TUI input or rendering thread.

`TUI::Start` performs initialization and then loops around the reentrant owner-thread `TUI::RunOneCycle`. Each cycle waits until terminal input, resize, or the earliest enabled periodic-timer or one-shot parser deadline. It moves decoded work into a persistent TUI event queue and dispatches a bounded unit. Check the stop-request flag after every user callback; do not dispatch a later queued input or timer callback once it is set.

`TUI::Stop` only changes owner-thread state. It never needs to wake a different thread in this task. A future cross-thread stop feature would require synchronization and a wake object, and is explicitly outside scope.

## Windows

### Acquisition and saved state

Use `GetStdHandle(STD_INPUT_HANDLE)` and `GetStdHandle(STD_OUTPUT_HANDLE)`. Validate both handles with `GetConsoleMode`. Save before modifying:

- Input and output console modes.
- The original active screen-buffer handle when a classic alternate buffer is needed.
- Cursor information and any other queryable state changed by the backend.
- The active 16-color table when the classic path uses or changes it.

Derive the visible size from `CONSOLE_SCREEN_BUFFER_INFO::srWindow`, whose coordinates are inclusive. `dwSize` and `WINDOW_BUFFER_SIZE_RECORD::dwSize` describe the screen buffer, not necessarily the visible viewport.

Useful official references:

- [`GetConsoleMode`](https://learn.microsoft.com/en-us/windows/console/getconsolemode)
- [`SetConsoleMode`](https://learn.microsoft.com/en-us/windows/console/setconsolemode)
- [`CONSOLE_SCREEN_BUFFER_INFO`](https://learn.microsoft.com/en-us/windows/console/console-screen-buffer-info-str)

### Input mode and blocking loop

Starting from the saved input mode:

- Clear `ENABLE_ECHO_INPUT`, `ENABLE_LINE_INPUT`, and `ENABLE_PROCESSED_INPUT`.
- Set `ENABLE_EXTENDED_FLAGS`, clear `ENABLE_QUICK_EDIT_MODE`, and set `ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT`.
- Clear `ENABLE_VIRTUAL_TERMINAL_INPUT` when consuming `INPUT_RECORD` values through `ReadConsoleInputW`.

Clearing `ENABLE_PROCESSED_INPUT` lets Ctrl+C arrive as a key/character input record instead of calling a console control handler on a system-created thread.

The console input handle is waitable. Use `WaitForSingleObject` with the earliest timer/parser deadline, then drain a bounded batch using `ReadConsoleInputW` into the persistent event queue. Both operations remain on the `Start` thread:

- `KEY_EVENT_RECORD` supplies press/release, repeat, modifiers, and UTF-16 input. For each key-down record with a nonzero character, honor `wRepeatCount` and invoke `Char(const TuiCharInfo&)` once per repeat with `uChar.UnicodeChar` forwarded unchanged as one native `wchar_t` unit. High and low surrogates are independent events in record order, isolated surrogates are not replaced, and each emitted unit keeps its record's modifiers. Key-code translation remains a placeholder.
- `MOUSE_EVENT_RECORD` maps movement, button press/release, `DOUBLE_CLICK`, `MOUSE_WHEELED`, and `MOUSE_HWHEELED` to the TUI mouse callbacks. Treat the signed high word of `dwButtonState` as the wheel delta.
- `MOUSE_EVENT_RECORD` coordinates are screen-buffer-relative. Subtract `srWindow.Left` and `srWindow.Top` unless the owned buffer guarantees a `(0, 0)` viewport.
- A resize input record is only a trigger. Query `GetConsoleScreenBufferInfo` again for the visible size before reallocating and invoking `BufferSizeChanged`.

References:

- [`ReadConsoleInputW`](https://learn.microsoft.com/en-us/windows/console/readconsoleinput)
- [Console input buffer](https://learn.microsoft.com/en-us/windows/console/console-input-buffer)
- [Windows wait functions](https://learn.microsoft.com/en-us/windows/win32/sync/wait-functions)

### Output

Prefer the virtual-terminal output path for new work:

1. Add `ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING` to the saved output mode. `DISABLE_NEWLINE_AUTO_RETURN` may be enabled when supported.
2. Enter the alternate screen with `CSI ? 1049 h` and hide the cursor with `CSI ? 25 l`.
3. Render UTF-16 text with cursor-position and SGR sequences through `WriteConsoleW`.
4. On cleanup, reset SGR, show the cursor, leave the alternate screen, and restore the exact saved modes.

Virtual-terminal output supports RGB and indexed SGR syntax, but successfully enabling the mode does not prove the attached terminal displays truecolor. Microsoft's classic console can map extended colors to its configured palette.

If virtual-terminal output is unavailable, use one alternate buffer created by `CreateConsoleScreenBuffer`, activate it with `SetConsoleActiveScreenBuffer`, and render with `WriteConsoleOutputW` using the 16-color attributes. `CHAR_INFO` stores one UTF-16 `WCHAR` for every rectangular screen-buffer cell; do not use historical `COMMON_LVB_LEADING_BYTE` and `COMMON_LVB_TRAILING_BYTE` flags as a generic Unicode width protocol. This fallback cannot faithfully implement TUI's portable width-two contract. Replace every width-two leading cell, and every supplementary scalar that cannot fit one `WCHAR`, with a documented one-cell ASCII fallback such as `?`; write a space with copied colors into the continuation cell. Unit tests must cover this degradation. The VT path is required for faithful width-two output.

References:

- [Console virtual-terminal sequences](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences)
- [Windows console input and output methods](https://learn.microsoft.com/en-us/windows/console/input-and-output-methods)
- [`CHAR_INFO`](https://learn.microsoft.com/en-us/windows/console/char-info-str)
- [`WriteConsoleOutputW`](https://learn.microsoft.com/en-us/windows/console/writeconsoleoutput)

### Cleanup order

During cleanup, after `Stopping` returns when it was invoked:

1. Reset output attributes and cursor visibility.
2. Leave the VT alternate screen, or reactivate the original classic screen buffer and close the temporary buffer.
3. Restore saved output and input modes.
4. Restore other queryable cursor/palette state changed by TUI.
5. If `Console::Disable()` succeeded, call `Console::Enable()`, then clear active TUI state.

Each initialization step registers its inverse immediately so failure halfway through startup rolls back only completed steps.

## Linux and macOS

Linux and macOS can share one POSIX implementation. Add `TUI.macOS.cpp` only for small compile-time differences that cannot remain readable in the shared file. This backend explicitly supports xterm-compatible terminals configured for UTF-8; `isatty` alone does not establish either property, and there is no portable query that proves them. Document this as a startup precondition rather than claiming automatic detection.

### Acquisition and termios

Require terminal file descriptors, verified with `isatty`. Save the exact `termios` structure with `tcgetattr`. Build raw mode from that saved structure, preferably with the platform's `cfmakeraw`, and set `VMIN = 0`, `VTIME = 0` because readiness is controlled by `poll`.

Apply and restore attributes with `tcsetattr`. Do not use `TCSAFLUSH` when promising to preserve pending input because it discards unread data; use a non-discarding transition such as `TCSANOW`.

Do not call `setlocale(LC_ALL, "")` from TUI. It changes process-global locale and is unnecessary for the shared scalar-width table.

References:

- [`termios`, `tcgetattr`, and `tcsetattr`](https://man7.org/linux/man-pages/man3/termios.3.html)
- [`isatty`](https://man7.org/linux/man-pages/man3/isatty.3.html)

### Resize wakeup and event loop

Create one process-lifetime self-pipe with `pipe`, then use `fcntl` to make both ends close-on-exec and nonblocking; do not rely on Linux-only `pipe2` in shared macOS code. Its descriptors are initialized before installing a handler, are never reused or closed by `Stop`, and are left for the OS to close at process exit. Drain stale bytes before each `Start`. This intentional lifetime makes an already-running signal handler safe even when another application-created thread receives `SIGWINCH` during teardown.

Install `SIGWINCH` with `sigaction`, saving the previous action. The signal handler may only:

- Save and restore `errno`.
- Attempt one byte on the nonblocking pipe, retrying `write` only while it fails with `EINTR`. Ignore `EAGAIN`/`EWOULDBLOCK` because a wake byte is already pending; no other handler-side recovery is safe.

It must not allocate, query terminal size, mutate the TUI buffer, or invoke a listener.

Use `poll` on terminal input and the pipe read end, with the earliest timer/parser deadline as the timeout. Retry after `EINTR` after recomputing deadlines and observing queued self-pipe work. When the pipe is readable, drain it, query `ioctl(TIOCGWINSZ)` on the owner thread, and invoke `BufferSizeChanged` only when the visible row/column count actually changed.

Block `SIGWINCH` on the owner thread while installing or restoring its action. Restore the old action during `Stop`. Signal masks are thread-local, so do not claim this blocks delivery to pre-existing worker threads; safety comes from the process-lifetime pipe and the handler's immutable descriptor state. TUI owns the process-wide `SIGWINCH` action while active, but every TUI user callback still runs only on the `Start` thread.

References:

- [`poll`](https://man7.org/linux/man-pages/man2/poll.2.html)
- [Async-signal safety](https://man7.org/linux/man-pages/man7/signal-safety.7.html)
- [`pthread_sigmask`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_sigmask.html)
- [`TIOCGWINSZ` and `SIGWINCH`](https://man7.org/linux/man-pages/man2/TIOCGWINSZ.2const.html)

### Input

Read available bytes only after `poll` reports readiness. Preserve incomplete data across reads:

- Decode UTF-8 incrementally. After a complete valid scalar is available, convert it to the platform-native UTF-32 `wchar_t` and invoke `Char(const TuiCharInfo&)` with that one unit. Replace each maximal malformed subsequence with a native U+FFFD unit and resynchronize.
- Parse mouse escape sequences incrementally; a read boundary is not an event boundary.
- Keep ordinary special-key escape sequences as placeholders for the future keyboard task and consume them without emitting a key callback.
- Distinguish a standalone Escape byte from a longer sequence with a one-shot deadline, not a busy polling loop. After the deadline, emit standalone Escape through `Char(const TuiCharInfo&)` with `code == (wchar_t)0x1B`, matching the Windows character record; do not leak the bytes of a recognized longer key sequence as character callbacks.

Enable xterm-compatible all-motion mouse tracking with `CSI ? 1003 h` and SGR coordinates with `CSI ? 1006 h`. Mode 1002 reports motion only while a button is held and therefore does not satisfy ordinary mouse-move callbacks. SGR coordinates are one-based and must be converted to zero-based cells.

Decode the SGR `Cb` parameter before classifying an event: copy bits 4, 8, and 16 into Shift, Alt/Meta, and Ctrl; extract bit 32 as the motion flag; then remove those four bits to obtain the base button/wheel code. This makes modified events such as Ctrl+wheel classify the same way as their unmodified base codes. Use the final `M`/`m` to distinguish press/motion from release as defined by SGR mode.

SGR mouse input has no double-click event. Synthesize a double-click when the same button is pressed at the same cell within a documented monotonic-time threshold; reset the candidate after another button/cell or after the deadline. Emit `DoubleClick` instead of a second `ButtonDown`, matching the Windows `DOUBLE_CLICK` record path. Map base codes 64/65 to vertical wheel `+120/-120`. Xterm reports button 6 as right and button 7 as left, so map base codes 66/67 to horizontal wheel `+120/-120` respectively when the terminal emits them. Terminals that do not emit horizontal-wheel codes simply cannot produce that callback. Track pressed buttons from press/release records, and populate unavailable character modifier fields as false rather than guessing from the decoded scalar.

### Output

The POSIX terminal protocol uses xterm-compatible extensions rather than APIs standardized by POSIX:

1. Enter the alternate screen with `CSI ? 1049 h`.
2. Hide the cursor with `CSI ? 25 l`.
3. Enable mouse modes 1003 and 1006.
4. Build each frame in memory using UTF-8, cursor positioning, and selected SGR colors.
5. Send it with a `WriteAll` loop around `write`, handling partial results and `EINTR`.

Never hard-code a manually counted byte length for a control-sequence literal. Use `sizeof(sequence) - 1` or the actual string length. In particular, `"\x1b[?1003h\x1b[?1006h"` is 16 bytes.

`TERM` and `COLORTERM` are heuristics. Support an explicit `TuiStartOptions::colorMode` override. The implementation assumes an xterm-compatible terminal that supports the selected alternate-screen, cursor, SGR, and mouse sequences.

The canonical terminal reference for these private modes is the [xterm control-sequence documentation](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html).

### Cleanup order

During cleanup, after `Stopping` returns when it was invoked:

1. Disable SGR mouse modes 1006 and 1003.
2. Reset SGR and show the cursor.
3. Leave the alternate screen.
4. Restore the exact saved `termios`.
5. Restore the previous `SIGWINCH` action and the owner thread's saved signal mask.
6. Restore any terminal-descriptor status flags changed by TUI. Keep the dedicated process-lifetime self-pipe open.
7. If `Console::Disable()` succeeded, call `Console::Enable()`, then clear active TUI state.

The inverse escape sequences are best-effort because terminal private modes are not portably queryable. Under the supported-terminal and exclusive-ownership assumptions, the alternate screen preserves the prior visible content on normal exit.
