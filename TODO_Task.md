The analysis is in `../GacUI/ToDo/Task_TUI.md`.
You are going to implement a cross-platform TUI utilities, by share as many common code as possible meanwhile implement non-sharable part for Windows, Linux and macOS.

## Definition of class vl::console::TUI

Just like `vl::console::Console` being a static class, you are going to put everything in a static class `vl::console::TUI`. By calling `TUI::Start`, the console buffer is taken over by `TUI`, and you can assume no one is going to use `vl::console::Console`, by checking `TUI::IsInUse` in `Console::(SetColor|Read|Write)`, using `CHECK_ERROR` for assertion.

`TUI::Stop` is used to return the buffer to normal console application. When calling it, everything is turning back to the state just before calling `TUI::Start`. And `Start`-`Stop` could happen multiple times. `Start` and `Stop` should check `TUI::IsInUse` and ignore the call if nothing is going to switch.
- The above requirement only valid if such `TUI::Stop` could be implemented in Windows, Linux and macOS.

`TUI` is going to maintain a buffer, which is a conceptually two dimentional array (but implemented by one dimentional array), of struct `TuiPixel`. The buffer should change according to the console window size. Changing the buffer size should fill new pixels with "empty" and set back/fore color to black/white. After preparing the buffer, a callback `BufferSizeChanged` is called. Such buffer could be obtained by `TUI::GetBuffer()`, returning a pointer to `TuiPixel`. This is a row-based buffer, row y and column x could be accessed using `GetBuffer()[y * GetBufferWidth() + x]`. Use could modify it and call `TUI::RenderBuffer` to submit it.

`TUI::InstallListener` and `TUI::UninstallListener` maintain a list of `ITuiCallback` pointer, which includes `BufferSizeChanged` as well as mouse, keyboard and a `Char(wchar_t)` event when use is typing. The argument should look like, or if possible, just copy from `INativeWindowListener`'s data structure from `../GacUI` repo. But for today, keyboard events will be just placeholder and do not implement it yet.

`TuiPixel` is going to look like this:

```C++
struct TuiColor
{
  vuint8_t r, g, b;
};

enum class TuiMergableGlyph
{
  None = 0,
  ThinLine = 1,
  ThickLine = 2,
};

struct TuiMergablePixel
{
  TuiMergableGlyph up, down, left, right; 
}

enum class TuiUnmergableGlyph
{
  RoundRect,
  DoubleRect,
};

enum class TuiUnmergableDirection
{
  LeftTop,
  RightTop,
  LeftBottom,
  RightBottom,
}

struct TuiUnmergableGlyph
{
  TuiUnmergableGlyph glyph;
  TuiUnmergableDirection direction;
};

enum class TuiPixelGlyph
{
  Char,
  Mergable,
  Unmergable,
  DoubleLineH,
  DoubleLineV,
};

struct TuiPixel
{
  TuiPixelGlyph glyph;
  union
  {
    char32_t c;
    TuiMergablePixel mergable;
    TuiUnmergablePixel unmergable;
  };
  TuiColor foregroundColor, backgroundColor;
};
```

`TuiPixel` defines a buffer that we can draw or print. What we can draw is:
- Thin vertical line, thin horizontal line, thin rectanble, thin round rect
- Thick vertical line, thick horizontal line, thick rectangle
- Double vertical line, double horizontal line, double rectanble
- Put a char32_t, when it is 0, it means "empty"
  - Technically a mergable pixel with 4 Nones is also empty, but when we set a pixel to empty, we always use Char.

`TuiMergablePixel` could describe if a pixel is covered by multiple thin or thick elements. It renders a cross with four components configurable.

But if the existing element in the pixel is being covered by a different one in different `TuiPixelGlyph`, it is totally replaced.

So that we are also going to have a lot of:
- `TUI::PrintChar(options, x, y)`. If it takes multiple pixels, for example 2, (x+1,y) will becomes a `Char` with 0, other properties will copy
- `TUI::MeasureChar(char32_t)` returns its width in pixels.
- `TUI::DrawLineV(options, x, y1, y2)`
- `TUI::DrawLineH(options, x1, x2, y)`
- `TUI::DrawRect(options, x1, y1, x2, y2)`
- `TUI::Clear(TuiColor, x1, y1, x2, y2)`
LineV and LineH share the same option type meanwhile rect use its own.
When line's or rect's `Nullable<TuiColor> backgroundColor` in options is not empty, it will always override affected pixels, regardless of what was in the range. By the way, other properties should not use `Nullable`. `foregroundColor` is always going to override.

## Files Layout

You are going to put source code in `Source/TUI`:
- `TUI.h`
- `TUI.cpp`
- `TUI.Windows.cpp`
- `TUI.Linux.cpp`
- `TUI.macOS.cpp` (if it can't share with linux)

You are going to implement all 3 platforms, but you are only going to verify the Windows version for now, Linux and macOS verification will be done in the future.

`TUI::Start` could have different signature for different platforms. For example, if Windows can choose between 16 colors and 256 colors, options of a struct should be used. If Windows can't and there will be no configuration to setup, its `Start` will have no argument. So `TUI::Start` will actually be implemented in different dependent files. Most of members should be in `TUI.cpp`.

`TUI::Start` is going to call `Starting` and `TUI::Stop` is going to call `Stopping`. And `Start` will block until `TUI::Stop` is called.

## UnitTest Project

In the `UnitTest` project they are going to use a `TUI` solution explorer folder.
Make a `TestTui.cpp` to test some functionalities, including:
- Convert `TuiPixel` to `wchar_t`, and `char32_t`. When a Char cannot be represented in single wchar_t, converting to wchar_t is always result in 0. These functions will be `TuiPixel::GetWChar` and `TuiPixel::GetChar32`.
- `PrintChar`, `Drawxxx`, `Clear` should also have their representing helper function, with extra e arguments from the first `TuiPixel* buffer, vint width, vint height`, for testing purpose. So testing draw function don't need to activate `TUI`.

## TuiPlayground Project

Create a new `TuiPlayground` project, but currently it does nothing, just draw a white double line rectangle on a black background to fill the console window, which means, the border of the console window will be double lined, dragging the console window will see the double line rectanble followed as a border.
