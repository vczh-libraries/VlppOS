# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

Add bold/italic/underline/strikeline to TUI's cell text style, defaults to false, only use them when rendering text. In order to doing this, we have to replace TuiPixel::c to a struct like other, we could name it TuiCharPixel.

Add `FS [B][I][U][S]` to TUI playground as a command, `TEXT` command would use it. When any of BIUS appears it means true for the appearing component and false for the rest.

Update [DebugTuiPlaygroundSOP.md](.github/Jobs/DebugTuiPlaygroundSOP.md) to add the new command, run the text to make sure it works.

Update knowledge base pages if necessary.

One more modification to TUI playground, there is a UI controlling how to render the error message, it will be triggered by invalid commands or the `HELP` command. Currently every line is centered. I would like you to make a better layout:

- When any line does not exceed the current window width excluding the message box border, no line wrap happens (which is already implemented)
- When no line wrap happens, the width of the text area is defined as the longest line. In this case, text should be left-aligned, and the message box shrink to the minimum width, and center the whole message box.
- When line wrap happens, the message box expands to a maximum width according to the current window width, text should still be left-aligned, and horizontally center the whole message box
- When TUI window resize, the message box should be re-layouted.
- Add a black background when rendering the message box, as currently everything under it is visible because the message box background is transparent.

Also verify if color setting on Windows is modern or not. We are going to support Windows 10 or newer OS so no concern about compatibility, hopefully we can use true color, or as many color as we can, if possible on Windows TUI implementation. Add in your own word "we are only support Windows 10 or newer OS when developing TUI implementation on Windows" to [Project.md](Project.md) in the proper position.

commit and push once finishing.

# UPDATES

## UPDATE

you need to also update the Linux implementation as well, I will test them manually later

## UPDATE

by the way, `TEXT` looks like my mistake, if the current commmand is `TYPE`, then keep `TYPE`

# TEST [CONFIRMED]

The new minimal-width information-box regression compiled in Debug x64 with zero warnings/errors and failed against the original implementation at the expected top-left corner assertion. Existing tests preceding it passed. This confirms the full-width layout defect before the fix.

- Reproduce the existing overlay's full-width, individually centered, transparent layout with a deterministic buffer test. Require a centered minimal-width box, left-aligned text and an opaque black interior, then verify wrapping, Unicode widths, narrow dimensions and resize relayout.
- Verify default-disabled styles, styled printing and wide-character repair, transitions between styled text and geometric/empty cells, FS combinations/reset/invalid grammar, TYPE replay, history persistence, and style preservation after resize.
- Build the Windows Debug x64 solution with copilotBuild.ps1 and run the required unit tests with copilotExecute.ps1. Inspect the completed logs and Debug leak tail.
- Run the production playground through the CLI wrapper and real console input/output. Check the four styles individually and together, style reset, RGB output, opaque help/errors and resize relayout, then EXIT and terminal restoration.
- Update the POSIX renderer alongside Windows. Linux/macOS live terminal verification is deferred to the user's manual testing as requested.

# PROPOSALS

- No.1 Store character styles in the cell and emit VT attributes; derive opaque information layout from current dimensions [CONFIRMED]

## No.1 Store character styles in the cell and emit VT attributes; derive opaque information layout from current dimensions

Introduce TuiTextStyle with four false-by-default flags and TuiCharPixel containing a scalar and its style. Replace the character union member with TuiPixel::character; add style to TuiPrintOptions. Only character glyphs with nonzero scalars supply render styles; geometric glyphs and empty cells reset attributes. Preserve scalar measurement and wide-cell color invariants.

Add FS with an optional compact case-insensitive BIUS combination; FS alone clears every flag. Each FS replaces the entire current style. Retain TYPE as the sole text command, following the user's correction. Replay style commands in submission order and apply styles only to text, preserving existing shape/color behavior.

Wrap information at the maximum available interior width, choose the longest line's width when nothing wraps, otherwise use the full available width. Center the complete rectangle, clear its entire bounds to black, and left-align every row. Recompute this on each frame, including resize callbacks.

Windows already enables virtual-terminal output and selects TrueColor in Auto mode. Keep RGB SGR output and explicit indexed color choices, while updating the supported Windows baseline to Windows 10 or newer. Verify actual emitted RGB and style attributes at the production terminal boundary.

### CODE CHANGE

Added TuiTextStyle/TuiCharPixel and migrated scalar accesses to character.c. PrintChar replaces complete pixels with initialized aggregates so switching union payloads from geometric glyphs starts the character payload lifetime and cannot retain stale style bits. Shared style selection and SGR encoding serve both Windows and POSIX, and each VT renderer tracks style transitions independently of colors using a nullable previous style. Added FS semantic command replay and help grammar, retaining TYPE only. The information overlay measures wrapped rows, centers the complete minimal/full-width box, clears it to black and left-aligns text. Updated Project.md, the TUI specification/index, playground SOP and relevant learning notes; regenerated Release artifacts with CodePack.

### CONFIRMED

- Windows Debug x64 solution build succeeded with zero warnings/errors. The final full UnitTest wrapper run passed all 16 files and 297 cases; the completed Execute.log has no leak report. Tests cover all 16 FS combinations, false defaults, style replacement/reset, wide-cell repair, geometry/empty-cell exclusion, exact command history and resize replay, invalid grammar, opaque minimal/full-width information boxes, Unicode and tiny dimensions. The initial full-width modal regression was reproduced before the implementation changed.
- Production CLI-wrapper runs on Windows 10 build 19045 through ConPTY verified each style and their combination, plain reset, exact RGB foreground/background, styled Unicode, unstyled shapes, replay after resize, and black left-aligned HELP/error boxes that shrink or wrap as the window changes. Exact terminal cell attributes and frames were inspected with xterm.js headless. Font appearance remains terminal-dependent.
- After EXIT zero, native snapshots matched original buffer/viewport geometry, cursor, attributes, modes, and complete character/attribute hashes, including a sentinel and scrollback taller than the viewport. A no-op cmd baseline independently demonstrated the shell's mouse-input flag change; starting from that baseline made the TUI before/after comparison exact. The SOP records the operations and dimensions.
- Windows already used modern virtual-terminal output and TrueColor in Auto mode. No color quantization change was needed; the live run confirmed exact RGB values on Windows 10. Text attributes now use SGR 1/3/4/9 and explicit 22/23/24/29 resets in both platform renderers.
- GacUI's shared `Source/TUI/TUITypes.h` declarations/defaults/key values/event semantics are unchanged. Searches found no `TuiPixel` or `TuiPrintOptions` uses in GacUI Source/Test, so no downstream consumer edits or import refresh are required for shared input compatibility. Generated VlppOS Release pairs contain the new character payload and both renderer changes.
- Linux/macOS code is updated, but its build and live terminal verification are pending the user's manual testing; the Windows common tests do not claim POSIX runtime coverage.
