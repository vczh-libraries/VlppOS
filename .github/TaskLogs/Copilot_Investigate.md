# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

GacUI/TODO_Task_TUI_Port.md is executed well, but the only issue is that ctrl+alt+super+q and ctrl+alt+super+shift+f8 does not work. This issue also happens on macOS as they shares the same piece of code, but macOS supports global shortcutkey and the later one goes through another route which works. Previous the windows one didn't work because VlppOS TUI implementation does not fill osSuper field at all. You are going to check:
- Has osSuper been filled in the VlppOS TUI implementation for Linux?
- Has Windows implementation filled every osSuper instead of just the keyboard IO struct?
Make sure all osSuper fields are filled in both Windows and Linux. Hopefully this will fix the linux issue. To speed up the development, when you release VlppOS into GacUI/wGac you don't have to run any GacUI/wGac test as no unit test is covering this part. commit and push all local changes. And then I will manually test CppTest_Tui myself in wGac.

# TEST [CONFIRMED]

- Audited every production assignment and default for the three input payloads in `Source/TUI/TUITypes.h`, both platform decoders, and GacUI's TUI forwarding. No uninitialized field or dropped observable Super flag was found.
- Built `Test/Linux/UnitTest` with the repository `build.sh`; ran `./Bin/UnitTest /C`. All 14 files and 270 cases passed. Existing POSIX tests cover `CSI 113;15u` for Ctrl+Alt+Super+Q and, after Kitty negotiation, `CSI 19;16~` for Ctrl+Alt+Super+Shift+F8, including every byte split. Windows-specific decoder tests were inspected but cannot execute on this Linux host.
- Installed GNOME Terminal is 3.52.0, using VTE 0.76.0. A temporary Python/GObject probe instantiated the installed VTE widget, emitted synthetic GTK key-press events, and captured its `commit` signal. Ctrl+Alt+Q and Ctrl+Alt+Super+Q both emitted hex `1b 11`; Ctrl+Alt+Shift+F8 and Ctrl+Alt+Super+Shift+F8 both emitted `ESC [19;8~`. This verifies VTE's input conversion, not physical desktop key delivery or a GacUI test. The temporary widget was destroyed. The user's terminal application has not been confirmed.
- The previous Linux application verification in `../wGac/TestMatrix_Tui.md` used Kitty 0.32.2 and explicitly excluded mouse Super and global shortcuts.
- Regenerated `Release` with the existing CodePack binary, copied the six root VlppOS release files to `../GacUI/Import`, and ran `../wGac/import.sh`. Checked the resulting snapshots against upstream. GacUI/wGac builds and tests were skipped as requested.

# PROPOSALS

- No.1 Preserve the existing event mappings and document terminal/global-shortcut limitations [CONFIRMED]

## No.1 Preserve the existing event mappings and document terminal/global-shortcut limitations

Windows already maps either Windows Terminal Win bit (`0x0200` or `0x0400`) into key-down and key-up payloads, copies that flag to every character payload, and initializes the common mouse payload before dispatching movement, down, up, double-click, and either wheel axis. This is not limited to the keyboard structure.

Linux and macOS share `PosixTuiInputDecoder`. Kitty keyboard reports carry independent Super state, which is decoded into key payloads and copied to any accompanying character payload. Legacy text and SGR mouse reports do not provide a Super state; their fields correctly retain the declared false default. SGR bit 8 is Alt, bit 32 is motion, and higher button bits cannot be repurposed as Super. Retaining the last keyboard Super state would be stale after an unreported modifier release. The current disambiguation mode does not report standalone modifiers or releases.

VTE 0.76 removes Super before producing terminal input. Its [key mapping source](https://github.com/GNOME/vte/blob/0.76.0/src/keymap.cc) restricts modifiers to Shift, Control, Alt and Num Lock. Changing VlppOS field assignments cannot recover a distinction absent from the input. Use a terminal implementing the [Kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/) for local Super shortcuts. No terminal change can add Super to standard SGR mouse reports; [Kitty's mouse encoder](https://github.com/kovidgoyal/kitty/blob/v0.32.2/kitty/mouse.c) also transmits only Shift, Alt and Control.

The F8 showcase command has an additional independent limitation: `../GacUI/Test/Resources/App/TuiControlTest/Resource.xml` declares `global:Ctrl+Shift+Alt+Command+F8`. `GuiToolstripCommand::GetShortcutManagerFromBuilder` selects the global manager exclusively for this declaration. `../wGac/WGac/Services/WGacInputService.cpp` only allocates a dummy registration id and never delivers global activation. This command has no local-key fallback, so improving terminal decoding alone cannot activate it in wGac. macOS native global delivery is a separate path.

### CODE CHANGE

No production source change is needed for an omitted observable `osSuper` assignment: none was found. Document the precise protocol boundaries in the owning VlppOS knowledge base and clarify the local Q/global F8 distinction in both wGac README languages. Preserve all field defaults, Alt/Meta semantics and public declarations.

### CONFIRMED

The full existing VlppOS suite passes, and the installed VTE encoder probe reproduces loss of Super before VlppOS receives input. Static inspection confirms all Windows event paths preserve their record's Super bits and the GacUI TUI adapter forwards them. Standard SGR and legacy text cannot supply every physical modifier; no claim of fixing those terminal limitations or implementing Wayland global shortcuts is made. Actual CppTest_Tui application testing remains with the user.
