You are going to improve the `TuiPlayground` project to make it a real playground for manual testing:
- The TUI playground becomes an ASCII art painting app by typing commands, put the usage in `Project.md`.
- commit and push all local changes after finishing.

Before actually implementing, fix a bug first. On Windows `TUI` does not expand to the whole window because the console window always display a vertical scroll bar. But `TUI` doesn't scroll as it uses the buffer for rendering. You need to find a way to disable the vrtical scroll bar, therefore it looks like the `TUI` is taking over the whole area.

Now implement the ASCII art painter like this:
- Automatic layout happens when the console window resized.
- At the ver bottom there will be a text box:
  - The background will be dark gray, so it looks differently with the painting area, indicating it is for typing.
  - There will be a cursor flashing every half seconds, at the end of the typed text, telling people if pressing any key text will appear there.
  - No actual cursor editing is supported, the only way to regret will be pressing backspace to delete the last character.
  - You need to be careful of character width as auto line wrapping must be implemented, but no need to support complex script, just treat it like simple left-to-right script to render each `char32_t` separately. When multiple lines are needed (if the last line is occupied completely the cursor should render at the next line), the text box should expand or shrink immedately.
- The rest of the area will be the ASCII art display area:
  - Its border is always a double lined rectangle, just like what it is now.
  - The app needs to remember all valid typed commands, so when repaint is needed all commands are replayed. But the border will clip anything that out of the area (including the border itself). The paper won't scroll, its (0,0) will always be (1,1) as the double lined border takes one pixel.
- Here are all commands to support, case insensitive:
  - `FC FFFFFF`: set foreground color using HTML color format without '#', default to white.
  - `BC CLEAR|000000`: background color, default to clear.
  - `LINEV FORMAT x y1 y2`, FORMAT would be `THIN|THICK|DOUBLE`.
  - `LINEH FORMAT x1 x2 y`
  - `RECT FORMAT x1 y1 x2 y2`, FORMAT would be `THIN|THICK|DOUBLE|ROUND`.
  - `CLEAR FFFFFF x1 y1 x2 y2`, clear rect using specificed background color
  - `TYPE x y:TEXT`, put text, no auto line wrap
- The parsing will be strict, and performed when user hits ENTER.
- When fail to parse, the error message will be displayed like:
  - `ERROR, original command:xxxx`
  - `REASON:xxxx`
  - A round rect occupying the whole width, aligned vertically centered will be displayed.
  - The text is in double line, but it should supports line wrap, and each virtual line will be horizontally centered.
  - When user hits ENTER again it disappeared, and user can continue to type.
- No matter success or fail, the text box receiving commands will be immediatelly cleared.

Data structure:

Here is the suggested data structure to store everything in the UI, so that when the console window resized, repaints could happen and render everything correctly, including:
- Rendering error message, or empty.
- Typing commands.
- All accumulated valid commands.

```C++
```