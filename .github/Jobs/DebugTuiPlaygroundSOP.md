# TUI Playground End-to-End Operation SOP

This document owns production feature operations and observable results. [Project.md](../../Project.md) owns project locations; [the specification](../KnowledgeBase/KB_VlppOS_TerminalUserInterface.md) owns API/input/rendering contracts.

## Session Setup

- Windows: from `REPO-ROOT/Test/UnitTest`, build with `& REPO-ROOT/.github/Scripts/copilotBuild.ps1` and launch `& REPO-ROOT/.github/Scripts/copilotExecute.ps1 -Mode CLI -Executable TuiPlayground` in a real interactive console.
- Linux/macOS: from `REPO-ROOT/Test/Linux/TuiPlayground`, build through the absolute `REPO-ROOT/.github/Ubuntu/build.sh` and run `./Bin/TuiPlayground` in an interactive UTF-8 xterm-compatible terminal.
- Record OS, terminal, locale/font, selected emission mode, dimensions and process result.
- On Windows, record the terminal host separately from the shell: PowerShell and cmd can run in either the built-in console host or Windows Terminal. For visual coverage of all four text styles, use Windows Terminal and set the profile's `intenseTextStyle` to `bold` or `all`; its default `bright` does not request a heavier font. Windows 10's built-in console can transport attributes that its own renderer does not display; enabling VT and selecting TrueColor do not guarantee every text effect. See [terminal rendering limitations](../KnowledgeBase/KB_VlppOS_TerminalUserInterface.md#windows-terminal-rendering-limitations).
- Begin with existing content and a unique sentinel. Windows must have scrollback taller than its viewport. Save buffer size, window rectangle/origin, cursor position/visibility/size, attributes and input/output modes.
- Capture the same state immediately after the application returns, before wrapper/prompt output touches the restored console.

## Rules for Every Operation

1. Inspect the current frame, send input through the production console/terminal boundary, then require the exact visible result. Fake backends and direct callback calls do not satisfy this SOP.
2. After navigation/resize, locate the header, paper and command box again.
3. All operations must clip on small screens without scrolling/crashing.
4. Record unavailable Linux/macOS hosts as pending; common-parser tests on Windows do not verify a POSIX terminal.
5. Keep terminal attribute verification separate from visual verification. ConPTY output decoded by a headless terminal proves emitted attributes and cell layout, but does not prove that the user's terminal draws bold, italic or strikeline. Record unsupported effects for that host and perform visual style checks in a supporting terminal.

## Command Grammar

- `FC RRGGBB` changes the foreground color; the initial foreground is `FFFFFF`.
- `BC CLEAR` preserves destination backgrounds for later lines and rectangles; `BC RRGGBB` replaces them. The initial background mode is `CLEAR`.
- `FS [B][I][U][S]` replaces the text style for subsequent `TYPE` commands. Include a compact combination of `B` (bold), `I` (italic), `U` (underline), and `S` (strikeline); every omitted flag becomes false. `FS` alone resets all four flags, which are initially false. Flags are case-insensitive, may appear in any order, and repeated flags have the same effect as one occurrence.
- `LINEV THIN|THICK|DOUBLE x y1 y2` draws a vertical line.
- `LINEH THIN|THICK|DOUBLE x1 x2 y` draws a horizontal line.
- `RECT THIN|THICK|DOUBLE|ROUND x1 y1 x2 y2` draws a rectangle. `ROUND` means a thin line with rounded corners.
- `CLEAR RRGGBB x1 y1 x2 y2` clears a rectangle to the specified background.
- `TYPE x y:TEXT` draws the payload without wrapping and preserves it exactly, including case, spaces, additional colons, and supplementary Unicode characters.
- `HELP` displays this concise list of accepted command shapes.
- `EXIT` quits the application.

Command names, formats, `CLEAR`, and hexadecimal digits are case-insensitive. Parsing is otherwise strict: use exactly one ASCII space at each displayed separator, six hexadecimal digits, signed decimal coordinates in the platform `vint` range, and ordered ranges. Logical paper `(0,0)` appears at terminal `(1,2)` inside the double-line border; signed off-paper coordinates are accepted and clipped.

The command box wraps complete Unicode scalars by display width, grows upward as needed, keeps its blinking cursor visible, and supports only Backspace editing. Enter submits and clears the box. `HELP` and parse errors display left-aligned information in a centered rounded box with an opaque black background. If every original line fits inside the available paper width excluding the box border, the text area shrinks to the longest line. Otherwise lines wrap at the maximum interior width and the box occupies the available paper width. Resize recomputes wrapping, box size and position. All typing, including Escape, is ignored until Enter dismisses it. `q` and `Q` are ordinary command text. `EXIT` is the only in-application way to quit.


## Initial Frame and Typing

1. Require row 0 exactly ` Canvas  History  Shapes `. Canvas is selected; selected background 000080, unselected 808080, all text FFFFFF.
2. Require the double border on row 1, paper origin (1,2), bottom command box background 404040.
3. Type `TYPE 0 0:Ab [brackets]` and Enter. Require the exact paper text and one history record.
4. Type mixed width-one/width-two/supplementary Unicode in a draft. Backspace removes one complete scalar; held text/Backspace keys repeat in order without duplicate control actions.
5. Grow the draft through wrapped rows. The box grows to at most height-1 and keeps its cursor visible. Shrink to widths 1..4 and restore; retain all scalars/draft without orphan wide cells.
6. Submit malformed commands and HELP. Require a rounded modal overlay; only Enter dismisses, once, without empty submission. Escape, Tab, arrows, mouse and ordinary text do nothing. q/Q are ordinary command text.

## Text Styles, Colors and Information Layout

1. Submit `FC 123456`, `CLEAR 234567 0 0 40 10`, then `FS B` and `TYPE 0 0:Bold`. Repeat on separate rows with `FS I`, `FS U`, `FS S`, and `FS BIUS`. Require the four individual effects and their combination, with RGB foreground 123456 and background 234567 on a supporting true-color terminal. For visual inspection, also repeat with `FC FFFFFF` and a black background for contrast. Bold may use the terminal's intensity preference; use the Windows Terminal profile setting described above to test a heavier font with RGB colors. `TYPE` preserves the destination background; `BC` controls later geometric drawing.
2. Submit `FS I` after `FS BIUS`; subsequent `TYPE` text must be italic only. Submit `FS` then `TYPE 0 6:Plain`; require all effects disabled. Earlier text retains its style. Include a literal space and width-two text such as `TYPE 12 0:A一 B` while styled, then overwrite either half of the wide character with a line and with plain text.
3. Draw lines and rectangles while `FS BIUS` is active. Require ordinary geometric glyphs and empty cells without inherited text effects. Resize and inspect replay; styles, colors and exact history entries must persist. Header, history, command input and information text remain unstyled.
4. Submit `FS BX` and `FS B I`; require modal parse errors. `FS`, `FS bius` and reordered/duplicate valid flags must succeed. Keep `TYPE`; there is no `TEXT` command.
5. Paint a bright background and text under the modal area. Open `HELP` in a wide window; require a centered box sized to the longest help line, one common left edge for every line, and solid black backgrounds throughout its border/interior. Underlying text must disappear inside the entire box.
6. Resize the open HELP box to a narrow window and back. Require wrapping at the available interior width, a full-width box while wrapping, then a centered minimal-width box again. Repeat with a two-line parse error whose lines differ in length. Exercise widths 1 through 4 and restore; require safe clipping and no orphan wide cells. Enter dismisses and restores the original drawing.

## Navigation and History

1. Keep a Unicode draft. Tab cycles Canvas, History, Shapes, Canvas; Shift-Tab reverses. Native KeyDown plus Char Tab advances once.
2. Click every header with Left; Right/Middle do nothing. History fills all rows below the header without a command box.
3. Submit enough mixed-case/Unicode commands to overflow History. Require exact submitted text, chronological order, initially newest content.
4. Vertical +120 scrolls three visual rows upward, -120 downward; two +60 equal one +120. Require end clamping; horizontal wheel does nothing.
5. Leave History while scrolled up, commit a new command, return and require the same top row. Resize and require current-width wrapping with a valid clamped position.
6. Canvas retains the draft. HELP, EXIT, errors and canceled previews never enter History.

## Shape Menu and Dragging

1. Open Shapes from Canvas and History. Require LINEV THIN/THICK/DOUBLE, LINEH THIN/THICK/DOUBLE, RECT THIN/THICK/DOUBLE/ROUND. Up/Down clamp at the first/last entry; highlighted entries remain reachable in clipped layouts.
2. Escape returns to the underlying page. Enter/click accepts once, switches to Canvas, retains draft with text 808080 and hides its cursor. The selecting mouse gesture must not start a drag.
3. For all ten styles, drag in all four directions. Require live preview, inclusive normalized coordinates and matching typed-command output on release. Lines retain their anchor column/row.
4. Overlap mixed lines and wide characters under BC CLEAR and BC 000000. Move preview away; require original content restored without ghosts. Release adds one canonical record and restores typing.
5. Release outside paper: clamp geometry. One-cell lines commit. One-row/one-column rectangles stay armed without committing.
6. Cancel with Escape, Tab, header selection, resize, and motion reporting Left released. Require no history or residual preview and restored typing.
7. Exercise DoubleClick replacing the second Down. Only Left begins/commits/cancels; Middle/Right/wheel never alter an armed shape.

## Replay, Resize and Restoration

1. Combine every grammar command/style, transparent/replacing backgrounds, intersections and clipped off-paper coordinates. Require matching replay after resizing.
2. Resize with menu, long draft, modal overlay and active drag. Require header retention, clipped layout, canceled drag and valid cells.
3. Windows active buffer must equal viewport dimensions at origin (0,0), without active scrollback/vertical scrollbar.
4. Submit EXIT from Canvas. Require exit zero and the original sentinel, scrollback, geometry, cursor, attributes and modes restored before shell/wrapper effects.
5. Start fresh and repeat typing/navigation/drag/exit. Require no stale held keys, parser bytes, preview, process or terminal modes.

## Verification Record

Record date, platform/terminal, builds/tests, actual live operations, restoration evidence and failures/fixes. Mark Linux/macOS pending when not executed. A passing parser or fake-backend test does not replace these production terminal checks.

### 2026-09-05 Follow-up: Windows host and Linux text styles

- The user reports only underline visibly working on Windows. Process inspection found the production playground launched by PowerShell with the built-in `C:\Windows\System32\conhost.exe`, file version 10.0.19041.1, on Windows 10 build 19045; no Windows Terminal process was found. This is consistent with the older host's rendering limitations documented in the specification. The prior ConPTY/headless result below verifies attribute transport, not this host's visual rendering. No new Windows Terminal visual run was performed in this follow-up.
- The user reports that the styles work correctly on Linux. Record this as successful user manual verification of the text effects; terminal/font/version details were not supplied. It does not establish a new automated Linux test run or additional coverage of modal layout, restoration or macOS.

### 2026-09-05 Windows text styles and information layout

- Windows 10 Pro 22H2, build 19045, production Windows ConPTY with xterm.js headless Unicode 11 cell decoding. Native UTF-16 console input/output crossed ConPTY as UTF-8. This run inspected terminal characters, widths, colors and style attributes; it did not assert a particular font's visual appearance. Auto selected RGB emission.
- Built Debug x64 through `copilotBuild.ps1`: zero warnings/errors. All 16 test files and 297 cases passed through the UnitTest wrapper, with no Debug leak report. The new minimal-width overlay regression first failed against the original implementation, then passed after the fix.
- Launched the production playground through the CLI wrapper and sent real ConPTY input. Verified `FS B`, `FS I`, `FS U`, `FS S`, `FS BIUS`, and `FS` with separate `TYPE` rows; checked exact decoded flags and RGB foreground 123456/background 234567. Checked styled wide text, unstyled geometric output, and preserved styles after resizing from 100 by 32 to 65 by 24 and back.
- Opened HELP over colored content. At 100 by 32, the rounded box shrank to its longest line and centered with left-aligned rows. At 30 by 24, it filled the available paper width and wrapped; restoring the size restored the minimal box. Inspected every border/interior background cell as black. Repeated modal layout checks with `FS BX` at 100 by 32 and 45 by 20, then dismissed and exited normally.
- EXIT returned zero in fresh repeated sessions. Native before/after snapshots around the CLI wrapper's executable invocation matched: 100 by 160 original buffer with a 100 by 32 viewport, window origin, cursor position/size/visibility, attributes, input/output modes, and hashes of every original character/attribute cell. The sentinel `TUI-STYLES-SENTINEL-20260905` returned. A no-op `cmd /C exit 0` established the shell's baseline first: cmd independently clears the mouse-input flag, so this shell effect was separated from TUI restoration.
- Both Windows and Linux/macOS renderers were updated and their Release artifacts regenerated. Linux/macOS builds and live checks for these new styles/layout changes remain pending manual verification; the earlier platform records below predate this change.

### 2026-09-05 Linux

- Ubuntu, VTE 0.76, `en_US.UTF-8`, DejaVu Sans Mono 10, `TERM=xterm-256color`, `COLORTERM=truecolor`; initial terminal 100 by 32 cells.
- Built UnitTest and TuiPlayground through their absolute `.github/Ubuntu/build.sh` entry points. All 14 unit-test files and 263 cases passed, including the production POSIX decoder and playground regressions.
- Exercised the production executable through VTE's PTY: bracket and mixed-width Unicode typing/Backspace, Tab/Shift-Tab, header clicks, modal HELP/errors, and all ten shape styles in all four drag directions. Each live preview and committed shape matched its equivalent typed command.
- Exercised overflowing history, both vertical wheel directions, ignored horizontal wheels, and Unicode draft retention across a resize to 30 by 12 cells and back to 100 by 32. Repeated fresh sessions exited with code zero.
- Also exercised lost-release cancellation, one-cell lines, degenerate rectangles, active-drag resize, and long Unicode drafts at widths 1 through 4. VTE/GTK refuses a one-column window allocation, so that case resized the actual child PTY to one column directly.
- A separate production-terminal run covered mixed-case FC/BC/CLEAR/TYPE/line/rectangle commands, both background modes, wide-character overlaps, off-paper clipping, Escape/Tab/header cancellation without ghosts, modal input isolation, and identical replay after resizing. Inspected the colored VTE output as well as the terminal text.
- The original `TUI-PORT-SENTINEL-20260905` returned after alternate-screen exit, and termios matched its saved value immediately after the executable returned, before wrapper output.
- Corrected stale wrapping wording in this SOP and the specification: the original task and existing implementation/tests require shape selection to clamp at the endpoints.
- Windows results belong to the earlier implementation run. macOS was not exercised in this Linux verification.

### 2026-09-05 macOS

- macOS 26.5.2 arm64, xterm.js with Unicode 11 cell widths in Playwright WebKit, Menlo 14, `en_US.UTF-8`, `TERM=xterm-256color`, `COLORTERM=truecolor`, initially 100 by 32 cells. The frontend was connected to a real macOS PTY running the production executable.
- Built UnitTest and TuiPlayground with their absolute `.github/Ubuntu/build.sh` entry points. All 14 files and 263 cases passed.
- Exercised bracket and mixed-width/supplementary Unicode typing, whole-scalar Backspace, Tab/Shift-Tab, header navigation, overflowing history and vertical/horizontal wheels, modal HELP/error isolation, Unicode draft retention, and all ten shape styles in all four drag directions. All 40 committed shapes matched the corresponding typed commands cell-for-cell, including colors.
- Additional live runs covered mixed-case grammar, transparent/replacing backgrounds, wide-character overlaps, off-paper clipping, identical replay after resizing, preview cancellation by Escape/Tab/header/lost release, one-cell lines, degenerate rectangles, outside-paper release, clipped shape menus, modal resizing, and active-drag resizing. Long Unicode drafts survived widths 1 through 4; xterm.js clamps its frontend to two columns, so the one-column case resized the actual child PTY directly.
- Original-screen sentinel and normal buffer were restored on exit zero, including a fresh typing/navigation/drag/exit repeat. Saved termios matched immediately after process return with only kernel-managed `PENDIN` masked. macOS sets this transient bit even in an isolated Python raw/restore round trip; all other flags, speeds, and control characters matched exactly.
- This is macOS production-terminal coverage; it does not replace the separately recorded Windows or Linux runs.
