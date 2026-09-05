# TUI Playground End-to-End Operation SOP

This document owns production feature operations and observable results. [Project.md](../../Project.md) owns project locations; [the specification](../KnowledgeBase/KB_VlppOS_TerminalUserInterface.md) owns API/input/rendering contracts.

## Session Setup

- Windows: from `REPO-ROOT/Test/UnitTest`, build with `& REPO-ROOT/.github/Scripts/copilotBuild.ps1` and launch `& REPO-ROOT/.github/Scripts/copilotExecute.ps1 -Mode CLI -Executable TuiPlayground` in a real interactive console.
- Linux/macOS: from `REPO-ROOT/Test/Linux/TuiPlayground`, build through the absolute `REPO-ROOT/.github/Ubuntu/build.sh` and run `./Bin/TuiPlayground` in an interactive UTF-8 xterm-compatible terminal.
- Record OS, terminal, locale/font, selected emission mode, dimensions and process result.
- Begin with existing content and a unique sentinel. Windows must have scrollback taller than its viewport. Save buffer size, window rectangle/origin, cursor position/visibility/size, attributes and input/output modes.
- Capture the same state immediately after the application returns, before wrapper/prompt output touches the restored console.

## Rules for Every Operation

1. Inspect the current frame, send input through the production console/terminal boundary, then require the exact visible result. Fake backends and direct callback calls do not satisfy this SOP.
2. After navigation/resize, locate the header, paper and command box again.
3. All operations must clip on small screens without scrolling/crashing.
4. Record unavailable Linux/macOS hosts as pending; common-parser tests on Windows do not verify a POSIX terminal.

## Command Grammar

- `FC RRGGBB` changes the foreground color; the initial foreground is `FFFFFF`.
- `BC CLEAR` preserves destination backgrounds for later lines and rectangles; `BC RRGGBB` replaces them. The initial background mode is `CLEAR`.
- `LINEV THIN|THICK|DOUBLE x y1 y2` draws a vertical line.
- `LINEH THIN|THICK|DOUBLE x1 x2 y` draws a horizontal line.
- `RECT THIN|THICK|DOUBLE|ROUND x1 y1 x2 y2` draws a rectangle. `ROUND` means a thin line with rounded corners.
- `CLEAR RRGGBB x1 y1 x2 y2` clears a rectangle to the specified background.
- `TYPE x y:TEXT` draws the payload without wrapping and preserves it exactly, including case, spaces, additional colons, and supplementary Unicode characters.
- `HELP` displays this concise list of accepted command shapes.
- `EXIT` quits the application.

Command names, formats, `CLEAR`, and hexadecimal digits are case-insensitive. Parsing is otherwise strict: use exactly one ASCII space at each displayed separator, six hexadecimal digits, signed decimal coordinates in the platform `vint` range, and ordered ranges. Logical paper `(0,0)` appears at terminal `(1,2)` inside the double-line border; signed off-paper coordinates are accepted and clipped.

The command box wraps complete Unicode scalars by display width, grows upward as needed, keeps its blinking cursor visible, and supports only Backspace editing. Enter submits and clears the box. `HELP` and parse errors display information in a centered rounded overlay; all typing, including Escape, is ignored until Enter dismisses it. `q` and `Q` are ordinary command text. `EXIT` is the only in-application way to quit.


## Initial Frame and Typing

1. Require row 0 exactly ` Canvas  History  Shapes `. Canvas is selected; selected background 000080, unselected 808080, all text FFFFFF.
2. Require the double border on row 1, paper origin (1,2), bottom command box background 404040.
3. Type `TYPE 0 0:Ab [brackets]` and Enter. Require the exact paper text and one history record.
4. Type mixed width-one/width-two/supplementary Unicode in a draft. Backspace removes one complete scalar; held text/Backspace keys repeat in order without duplicate control actions.
5. Grow the draft through wrapped rows. The box grows to at most height-1 and keeps its cursor visible. Shrink to widths 1..4 and restore; retain all scalars/draft without orphan wide cells.
6. Submit malformed commands and HELP. Require a rounded modal overlay; only Enter dismisses, once, without empty submission. Escape, Tab, arrows, mouse and ordinary text do nothing. q/Q are ordinary command text.

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
