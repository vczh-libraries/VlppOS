/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "../../Source/TUI/TUI.h"

#define VCZH_TUI_PLAYGROUND_TEST
#include "../UnitTest/TuiPlayground/Main.cpp"
#undef VCZH_TUI_PLAYGROUND_TEST

using namespace vl;
using namespace vl::collections;
using namespace vl::console;
namespace tui_test = vl::console::unittest;

namespace
{
	class FakeTuiBackend : public tui_test::ITuiBackend
	{
	public:
		List<tui_test::TuiBackendEvent>		events;
		vint								width = 8;
		vint								height = 4;
		vuint64_t							time = 0;
		vint								startCount = 0;
		vint								stopCount = 0;
		vint								renderCount = 0;
		vint								renderWidth = 0;
		vint								renderHeight = 0;
		TuiColorMode						renderColorMode = TuiColorMode::Auto;
		Array<TuiPixel>						renderedBuffer;
		List<U32String>						renderedTexts;
		List<vint>							waitTimeouts;
		vint								escapeAfterTimeouts = -1;
		TuiColorMode						startColorMode = TuiColorMode::TrueColor;
		bool								honorRequestedColorMode = true;
		bool								failStart = false;

		TuiColorMode Start(const TuiStartOptions& options) override
		{
			startCount++;
			if (failStart)
			{
				throw Exception(L"Fake backend startup failure.");
			}
			return honorRequestedColorMode && options.colorMode != TuiColorMode::Auto
				? options.colorMode
				: startColorMode;
		}

		void Stop() override
		{
			stopCount++;
		}

		bool TryGetConsoleSize(vint& outputWidth, vint& outputHeight) override
		{
			outputWidth = width;
			outputHeight = height;
			return true;
		}

		vuint64_t GetMonotonicTime() override
		{
			return time;
		}

		bool ReadEvent(vint milliseconds, tui_test::TuiBackendEvent& event) override
		{
			if (events.Count() > 0)
			{
				event = events[0];
				events.RemoveAt(0);
				return true;
			}
			if (milliseconds > 0)
			{
				waitTimeouts.Add(milliseconds);
				if (escapeAfterTimeouts == 0)
				{
					event.type = tui_test::TuiBackendEventType::Char;
					event.charInfo.code = (wchar_t)0x1B;
					return true;
				}
				if (escapeAfterTimeouts > 0) escapeAfterTimeouts--;
				time += milliseconds;
			}
			return false;
		}

		void Render(const TuiPixel* buffer, vint width, vint height, TuiColorMode colorMode) override
		{
			renderCount++;
			renderWidth = width;
			renderHeight = height;
			renderColorMode = colorMode;
			Array<TuiPixel> copiedBuffer(width * height);
			for (vint i = 0; i < width * height; i++)
			{
				copiedBuffer[i] = buffer[i];
			}
			renderedBuffer = std::move(copiedBuffer);
			List<char32_t> text;
			for (vint i = 0; i < width * height; i++)
			{
				text.Add(buffer[i].GetChar32());
			}
			renderedTexts.Add(U32String::CopyFrom(&text[0], text.Count()));
		}

		void PushChar(const TuiCharInfo& info)
		{
			tui_test::TuiBackendEvent event;
			event.type = tui_test::TuiBackendEventType::Char;
			event.charInfo = info;
			events.Add(event);
		}

		void PushChar(wchar_t code)
		{
			PushChar({ .code = code });
		}

		void PushScalar(char32_t code)
		{
			wchar_t units[encoding::UtfConversion<wchar_t>::BufferLength];
			auto count = encoding::UtfConversion<wchar_t>::From32(code, units);
			TEST_ASSERT(count > 0);
			for (vint i = 0; i < count; i++)
			{
				PushChar(units[i]);
			}
		}

		void PushText(const U32String& text)
		{
			for (vint i = 0; i < text.Length(); i++)
			{
				PushScalar(text[i]);
			}
		}

		void PushCommand(const U32String& text)
		{
			PushText(text);
			PushChar(L'\r');
		}

		void PushResize(vint newWidth, vint newHeight)
		{
			tui_test::TuiBackendEvent event;
			event.type = tui_test::TuiBackendEventType::Resize;
			event.width = newWidth;
			event.height = newHeight;
			events.Add(event);
		}
	};

	struct Callback : ITuiCallback
	{
		List<WString>		events;
		List<TuiCharInfo>	charInfos;
		Func<void()>		onStarting;
		Func<void()>		onStopping;
		Func<void()>		onResize;
		Func<void()>		onTimer;
		Func<void(wchar_t)>	onChar;

		void Starting() override
		{
			events.Add(L"Starting");
			if (onStarting) onStarting();
		}

		void Stopping() override
		{
			events.Add(L"Stopping");
			if (onStopping) onStopping();
		}

		void BufferSizeChanged() override
		{
			events.Add(L"BufferSizeChanged");
			if (onResize) onResize();
		}

		void Char(const TuiCharInfo& info) override
		{
			events.Add(L"Char");
			charInfos.Add(info);
			if (onChar) onChar(info.code);
		}

		void Timer() override
		{
			events.Add(L"Timer");
			if (onTimer) onTimer();
		}
	};

	struct StopOnEventCallback : ITuiCallback
	{
		tui_test::TuiBackendEventType	type = tui_test::TuiBackendEventType::None;
		bool							stopOnStarting = false;
		bool							stopOnResize = false;
		bool							stopOnTimer = false;
		vint							expectedThreadId = -1;
		vint							stoppingCount = 0;
		vint							eventCount = 0;

		void CheckThread()
		{
			if (expectedThreadId != -1)
			{
				TEST_ASSERT(Thread::GetCurrentThreadId() == expectedThreadId);
			}
		}

		void Starting() override
		{
			CheckThread();
			if (stopOnStarting) TUI::Stop();
		}

		void Stopping() override
		{
			CheckThread();
			stoppingCount++;
		}

		void BufferSizeChanged() override
		{
			CheckThread();
			if (stopOnResize) TUI::Stop();
		}

		void MouseMove(const TuiMouseInfo&) override
		{
			CheckThread();
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseMove) TUI::Stop();
		}

		void MouseDown(TuiMouseButton, const TuiMouseInfo&) override
		{
			CheckThread();
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseDown) TUI::Stop();
		}

		void MouseUp(TuiMouseButton, const TuiMouseInfo&) override
		{
			CheckThread();
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseUp) TUI::Stop();
		}

		void MouseDoubleClick(TuiMouseButton, const TuiMouseInfo&) override
		{
			CheckThread();
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseDoubleClick) TUI::Stop();
		}

		void MouseVerticalWheel(const TuiMouseInfo&) override
		{
			CheckThread();
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseVerticalWheel) TUI::Stop();
		}

		void MouseHorizontalWheel(const TuiMouseInfo&) override
		{
			CheckThread();
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseHorizontalWheel) TUI::Stop();
		}

		void KeyDown(const TuiKeyInfo&) override
		{
			CheckThread();
			eventCount++;
			if (type == tui_test::TuiBackendEventType::KeyDown) TUI::Stop();
		}

		void KeyUp(const TuiKeyInfo&) override
		{
			CheckThread();
			eventCount++;
			if (type == tui_test::TuiBackendEventType::KeyUp) TUI::Stop();
		}

		void Char(const TuiCharInfo&) override
		{
			CheckThread();
			eventCount++;
			if (type == tui_test::TuiBackendEventType::Char) TUI::Stop();
		}

		void Timer() override
		{
			CheckThread();
			eventCount++;
			if (stopOnTimer) TUI::Stop();
		}
	};

	void ClearBuffer(TuiPixel* buffer, vint width, vint height)
	{
		TUI::Clear(buffer, width, height, TuiColor{ 0, 0, 0 }, 0, 0, width - 1, height - 1);
	}

	U32String BufferText(TuiPixel* buffer, vint width, vint height)
	{
		Array<char32_t> text(width * height);
		for (vint i = 0; i < width * height; i++)
		{
			auto code = buffer[i].GetChar32();
			text[i] = code == 0 ? U' ' : code;
		}
		return U32String::CopyFrom(&text[0], text.Count());
	}
}

TEST_FILE
{
	TEST_CATEGORY(L"Pixels and character widths")
	{
		TEST_CASE(L"Every none, thin and thick arm state has an exact mapping")
		{
			const char32_t expected[] =
			{
				0, U'\u2576', U'\u257A', U'\u2574', U'\u2500', U'\u257C', U'\u2578', U'\u257E', U'\u2501',
				U'\u2577', U'\u250C', U'\u250D', U'\u2510', U'\u252C', U'\u252E', U'\u2511', U'\u252D', U'\u252F',
				U'\u257B', U'\u250E', U'\u250F', U'\u2512', U'\u2530', U'\u2532', U'\u2513', U'\u2531', U'\u2533',
				U'\u2575', U'\u2514', U'\u2515', U'\u2518', U'\u2534', U'\u2536', U'\u2519', U'\u2535', U'\u2537',
				U'\u2502', U'\u251C', U'\u251D', U'\u2524', U'\u253C', U'\u253E', U'\u2525', U'\u253D', U'\u253F',
				U'\u257D', U'\u251F', U'\u2522', U'\u2527', U'\u2541', U'\u2546', U'\u252A', U'\u2545', U'\u2548',
				U'\u2579', U'\u2516', U'\u2517', U'\u251A', U'\u2538', U'\u253A', U'\u251B', U'\u2539', U'\u253B',
				U'\u257F', U'\u251E', U'\u2521', U'\u2526', U'\u2540', U'\u2544', U'\u2529', U'\u2543', U'\u2547',
				U'\u2503', U'\u2520', U'\u2523', U'\u2528', U'\u2542', U'\u254A', U'\u252B', U'\u2549', U'\u254B',
			};
			vint index = 0;
			for (vint up = 0; up < 3; up++)
			for (vint down = 0; down < 3; down++)
			for (vint left = 0; left < 3; left++)
			for (vint right = 0; right < 3; right++)
			{
				TuiPixel pixel;
				pixel.glyph = TuiPixelGlyph::Mergeable;
				pixel.mergeable =
				{
					(TuiMergeableGlyph)up,
					(TuiMergeableGlyph)down,
					(TuiMergeableGlyph)left,
					(TuiMergeableGlyph)right,
				};
				TEST_ASSERT(pixel.GetChar32() == expected[index++]);
			}
			TEST_ASSERT(index == sizeof(expected) / sizeof(*expected));
		});

		TEST_CASE(L"Supported and unsupported double states are distinguished")
		{
			using G = TuiMergeableGlyph;
			const TuiMergeablePixel supported[] =
			{
				{ G::None, G::None, G::DoubleLine, G::DoubleLine },
				{ G::DoubleLine, G::DoubleLine, G::None, G::None },
				{ G::None, G::ThinLine, G::None, G::DoubleLine },
				{ G::None, G::DoubleLine, G::None, G::ThinLine },
				{ G::None, G::DoubleLine, G::None, G::DoubleLine },
				{ G::None, G::ThinLine, G::DoubleLine, G::None },
				{ G::None, G::DoubleLine, G::ThinLine, G::None },
				{ G::None, G::DoubleLine, G::DoubleLine, G::None },
				{ G::ThinLine, G::None, G::None, G::DoubleLine },
				{ G::DoubleLine, G::None, G::None, G::ThinLine },
				{ G::DoubleLine, G::None, G::None, G::DoubleLine },
				{ G::ThinLine, G::None, G::DoubleLine, G::None },
				{ G::DoubleLine, G::None, G::ThinLine, G::None },
				{ G::DoubleLine, G::None, G::DoubleLine, G::None },
				{ G::ThinLine, G::ThinLine, G::None, G::DoubleLine },
				{ G::DoubleLine, G::DoubleLine, G::None, G::ThinLine },
				{ G::DoubleLine, G::DoubleLine, G::None, G::DoubleLine },
				{ G::ThinLine, G::ThinLine, G::DoubleLine, G::None },
				{ G::DoubleLine, G::DoubleLine, G::ThinLine, G::None },
				{ G::DoubleLine, G::DoubleLine, G::DoubleLine, G::None },
				{ G::None, G::ThinLine, G::DoubleLine, G::DoubleLine },
				{ G::None, G::DoubleLine, G::ThinLine, G::ThinLine },
				{ G::None, G::DoubleLine, G::DoubleLine, G::DoubleLine },
				{ G::ThinLine, G::None, G::DoubleLine, G::DoubleLine },
				{ G::DoubleLine, G::None, G::ThinLine, G::ThinLine },
				{ G::DoubleLine, G::None, G::DoubleLine, G::DoubleLine },
				{ G::ThinLine, G::ThinLine, G::DoubleLine, G::DoubleLine },
				{ G::DoubleLine, G::DoubleLine, G::ThinLine, G::ThinLine },
				{ G::DoubleLine, G::DoubleLine, G::DoubleLine, G::DoubleLine },
			};
			for (vint up = 0; up < 4; up++)
			for (vint down = 0; down < 4; down++)
			for (vint left = 0; left < 4; left++)
			for (vint right = 0; right < 4; right++)
			{
				if (up != 3 && down != 3 && left != 3 && right != 3) continue;
				TuiPixel pixel;
				pixel.glyph = TuiPixelGlyph::Mergeable;
				pixel.mergeable = { (G)up, (G)down, (G)left, (G)right };
				char32_t expected = 0;
				for (vint i = 0; i < sizeof(supported) / sizeof(*supported); i++)
				{
					auto& state = supported[i];
					if (
						state.up == pixel.mergeable.up &&
						state.down == pixel.mergeable.down &&
						state.left == pixel.mergeable.left &&
						state.right == pixel.mergeable.right)
					{
						expected = (char32_t)(U'\u2550' + i);
						break;
					}
				}
				TEST_ASSERT(pixel.GetChar32() == expected);
			}

			TuiPixel invalid;
			invalid.glyph = TuiPixelGlyph::Mergeable;
			invalid.mergeable = { (G)4, G::None, G::None, G::None };
			TEST_ASSERT(invalid.GetChar32() == 0);
		});

		TEST_CASE(L"GetChar32 and GetWChar follow scalar and platform rules")
		{
			TuiPixel pixel;
			TEST_ASSERT(pixel.GetChar32() == 0);
			TEST_ASSERT(pixel.GetWChar() == 0);

			pixel.c = U'\u2500';
			TEST_ASSERT(pixel.GetChar32() == U'\u2500');
			TEST_ASSERT(pixel.GetWChar() == L'\u2500');

			pixel.c = U'\U0001F600';
			TEST_ASSERT(pixel.GetChar32() == U'\U0001F600');
			if constexpr (sizeof(wchar_t) == 2)
			{
				TEST_ASSERT(pixel.GetWChar() == 0);
			}
			else
			{
				TEST_ASSERT(pixel.GetWChar() == (wchar_t)U'\U0001F600');
			}

			pixel.glyph = TuiPixelGlyph::WideCharContinuation;
			TEST_ASSERT(pixel.GetChar32() == 0);
			TEST_ASSERT(pixel.GetWChar() == 0);
		});

		TEST_CASE(L"MeasureChar uses the platform scalar-width policy")
		{
			TEST_ASSERT(TUI::MeasureChar(U'\0') == 0);
			TEST_ASSERT(TUI::MeasureChar(U'\x1B') == 0);
			TEST_ASSERT(TUI::MeasureChar(U'\u0301') == 0);
			TEST_ASSERT(TUI::MeasureChar(U'A') == 1);
			TEST_ASSERT(TUI::MeasureChar(U'\u2500') == 1);
			TEST_ASSERT(TUI::MeasureChar(U'\u4E00') == 2);
			TEST_ASSERT(TUI::MeasureChar(U'\U0001F600') == 2);
			TEST_ASSERT(TUI::MeasureChar((char32_t)0xD800) == 0);
			TEST_ASSERT(TUI::MeasureChar((char32_t)0x110000) == 0);
		});
	});

	TEST_CATEGORY(L"Drawing")
	{
		TEST_CASE(L"PrintChar validates scalars and maintains wide pairs")
		{
			TuiPixel buffer[5];
			ClearBuffer(buffer, 5, 1);
			TuiPrintOptions options;
			options.foregroundColor = { 1, 2, 3 };
			options.backgroundColor = { 4, 5, 6 };

			TEST_ERROR(TUI::PrintChar(buffer, 5, 1, options, (char32_t)0xD800, 0, 0));
			TUI::PrintChar(buffer, 5, 1, options, U'\u0301', 0, 0);
			TEST_ASSERT(buffer[0].c == 0);

			TUI::PrintChar(buffer, 5, 1, options, U'\u4E00', 1, 0);
			TEST_ASSERT(buffer[1].glyph == TuiPixelGlyph::Char);
			TEST_ASSERT(buffer[1].c == U'\u4E00');
			TEST_ASSERT(buffer[2].glyph == TuiPixelGlyph::WideCharContinuation);
			TEST_ASSERT(buffer[2].foregroundColor == options.foregroundColor);
			TEST_ASSERT(buffer[2].backgroundColor == options.backgroundColor);

			TUI::PrintChar(buffer, 5, 1, options, U'X', 2, 0);
			TEST_ASSERT(buffer[1].glyph == TuiPixelGlyph::Char && buffer[1].c == 0);
			TEST_ASSERT(buffer[2].glyph == TuiPixelGlyph::Char && buffer[2].c == U'X');

			TUI::PrintChar(buffer, 5, 1, options, U'\u4E00', 4, 0);
			TEST_ASSERT(buffer[4].c == 0);
			TUI::PrintChar(buffer, 5, 1, options, U'Y', -1, 0);
			TEST_ASSERT(BufferText(buffer, 5, 1) == U32String::Unmanaged(U"  X  "));
		});

		TEST_CASE(L"Lines, sharp rectangles and rounded rectangles are exact")
		{
			TuiPixel buffer[25];
			ClearBuffer(buffer, 5, 5);
			TuiLineOptions line;
			line.glyph = TuiMergeableGlyph::ThinLine;
			TUI::DrawLineH(buffer, 5, 5, line, 1, 3, 2);
			TEST_ASSERT(BufferText(buffer, 5, 5) == U32String::Unmanaged(
				U"     "
				U"     "
				U" \u2500\u2500\u2500 "
				U"     "
				U"     "));
			TUI::DrawLineV(buffer, 5, 5, line, 2, 1, 3);
			TEST_ASSERT(buffer[2 * 5 + 2].GetChar32() == U'\u253C');

			ClearBuffer(buffer, 5, 5);
			TuiRectOptions rect;
			TUI::DrawRect(buffer, 5, 5, rect, 1, 1, 3, 3);
			TEST_ASSERT(BufferText(buffer, 5, 5) == U32String::Unmanaged(
				U"     "
				U" \u250C\u2500\u2510 "
				U" \u2502 \u2502 "
				U" \u2514\u2500\u2518 "
				U"     "));

			ClearBuffer(buffer, 5, 5);
			rect.corner = TuiRectCorner::Round;
			TUI::DrawRect(buffer, 5, 5, rect, 1, 1, 3, 3);
			TEST_ASSERT(buffer[1 * 5 + 1].GetChar32() == U'\u256D');
			TEST_ASSERT(buffer[1 * 5 + 3].GetChar32() == U'\u256E');
			TEST_ASSERT(buffer[3 * 5 + 1].GetChar32() == U'\u2570');
			TEST_ASSERT(buffer[3 * 5 + 3].GetChar32() == U'\u256F');
		});

		TEST_CASE(L"Merge fallback, latest colors, backgrounds, clipping and clear")
		{
			TuiPixel buffer[9];
			ClearBuffer(buffer, 3, 3);
			TuiLineOptions horizontal;
			horizontal.glyph = TuiMergeableGlyph::DoubleLine;
			horizontal.foregroundColor = { 10, 20, 30 };
			TUI::DrawLineH(buffer, 3, 3, horizontal, -10, 10, 1);

			TuiLineOptions vertical;
			vertical.glyph = TuiMergeableGlyph::ThickLine;
			vertical.foregroundColor = { 40, 50, 60 };
			vertical.backgroundColor = TuiColor{ 70, 80, 90 };
			TUI::DrawLineV(buffer, 3, 3, vertical, 1, -10, 10);
			TEST_ASSERT(buffer[4].GetChar32() == U'\u2503');
			TEST_ASSERT(buffer[4].foregroundColor == vertical.foregroundColor);
			TEST_ASSERT(buffer[4].backgroundColor == vertical.backgroundColor.Value());

			TEST_ERROR(TUI::DrawLineH(buffer, 3, 3, horizontal, 2, 1, 0));
			TEST_ERROR(TUI::DrawLineV(buffer, 3, 3, vertical, 0, 2, 1));
			TEST_ERROR(TUI::Clear(buffer, 3, 3, TuiColor{ 0, 0, 0 }, 2, 0, 1, 1));

			TUI::Clear(buffer, 3, 3, TuiColor{ 9, 8, 7 }, 1, 1, 2, 2);
			TEST_ASSERT(buffer[4].glyph == TuiPixelGlyph::Char && buffer[4].c == 0);
			TEST_ASSERT(buffer[4].foregroundColor == TuiColor({ 255, 255, 255 }));
			TEST_ASSERT(buffer[4].backgroundColor == TuiColor({ 9, 8, 7 }));

			horizontal.glyph = TuiMergeableGlyph::None;
			TEST_ERROR(TUI::DrawLineH(buffer, 3, 3, horizontal, 0, 1, 0));
			horizontal.glyph = (TuiMergeableGlyph)4;
			TEST_ERROR(TUI::DrawLineH(buffer, 3, 3, horizontal, 0, 1, 0));

			TuiRectOptions invalidRect;
			invalidRect.corner = (TuiRectCorner)2;
			TEST_ERROR(TUI::DrawRect(buffer, 3, 3, invalidRect, 0, 0, 2, 2));
			invalidRect.corner = TuiRectCorner::Round;
			invalidRect.glyph = TuiMergeableGlyph::DoubleLine;
			TEST_ERROR(TUI::DrawRect(buffer, 3, 3, invalidRect, 0, 0, 2, 2));
		});
	});

	TEST_CATEGORY(L"Lifecycle and callbacks")
	{
		TEST_CASE(L"Character events use native wchar_t units")
		{
			TEST_ASSERT((std::is_same_v<decltype(TuiCharInfo::code), wchar_t>));
		});

#if defined VCZH_WCHAR_UTF16
		TEST_CASE(L"UTF-16 character events preserve native units, order and modifiers")
		{
			auto backend = Ptr(new FakeTuiBackend);
			const wchar_t high = (wchar_t)0xD83D;
			const wchar_t low = (wchar_t)0xDE00;
			const TuiCharInfo inputs[] =
			{
				{ .code = L'A', .ctrl = true, .shift = true, .alt = true, .capslock = true },
				{ .code = (wchar_t)0xD800, .ctrl = true },
				{ .code = L'B' },
				{ .code = (wchar_t)0xDC00, .shift = true },
				{ .code = high, .alt = true },
				{ .code = low, .capslock = true },
			};
			for (auto&& input : inputs)
			{
				backend->PushChar(input);
			}
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			callback.onChar = [&](wchar_t)
			{
				if (callback.charInfos.Count() == sizeof(inputs) / sizeof(*inputs))
				{
					TUI::Stop();
				}
			};
			TUI::InstallListener(&callback);
			TUI::Start({});
			TEST_ASSERT(callback.charInfos.Count() == sizeof(inputs) / sizeof(*inputs));
			for (vint i = 0; i < callback.charInfos.Count(); i++)
			{
				auto&& expected = inputs[i];
				auto&& actual = callback.charInfos[i];
				TEST_ASSERT(actual.code == expected.code);
				TEST_ASSERT(actual.ctrl == expected.ctrl);
				TEST_ASSERT(actual.shift == expected.shift);
				TEST_ASSERT(actual.alt == expected.alt);
				TEST_ASSERT(actual.capslock == expected.capslock);
			}
			TEST_ASSERT(callback.charInfos[4].code == high);
			TEST_ASSERT(callback.charInfos[5].code == low);
			TUI::UninstallListener(&callback);
		});

		TEST_CASE(L"Stopping on a UTF-16 high surrogate suppresses the queued low surrogate")
		{
			auto backend = Ptr(new FakeTuiBackend);
			const wchar_t high = (wchar_t)0xD83D;
			const wchar_t low = (wchar_t)0xDE00;
			backend->PushChar(high);
			backend->PushChar(low);
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			callback.onChar = [&](wchar_t code)
			{
				if (code == high)
				{
					TUI::Stop();
				}
			};
			TUI::InstallListener(&callback);
			TUI::Start({});
			TEST_ASSERT(callback.charInfos.Count() == 1);
			TEST_ASSERT(callback.charInfos[0].code == high);
			TUI::UninstallListener(&callback);
		});
#elif defined VCZH_WCHAR_UTF32
		TEST_CASE(L"UTF-32 character events preserve one native supplementary unit")
		{
			auto backend = Ptr(new FakeTuiBackend);
			TuiCharInfo input =
			{
				.code = (wchar_t)0x1F600,
				.ctrl = true,
				.shift = true,
				.alt = true,
				.capslock = true,
			};
			backend->PushChar(input);
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			callback.onChar = [](wchar_t) { TUI::Stop(); };
			TUI::InstallListener(&callback);
			TUI::Start({});
			TEST_ASSERT(callback.charInfos.Count() == 1);
			auto&& actual = callback.charInfos[0];
			TEST_ASSERT(actual.code == input.code);
			TEST_ASSERT(actual.ctrl == input.ctrl);
			TEST_ASSERT(actual.shift == input.shift);
			TEST_ASSERT(actual.alt == input.alt);
			TEST_ASSERT(actual.capslock == input.capslock);
			TUI::UninstallListener(&callback);
		});
#endif

		TEST_CASE(L"Start, initial resize, character stop and cleanup ordering")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->PushChar(L'Q');
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			callback.onChar = [](wchar_t) { TUI::Stop(); };
			TEST_ASSERT(TUI::InstallListener(&callback));
			TEST_ASSERT(!TUI::InstallListener(nullptr));
			TEST_ASSERT(!TUI::InstallListener(&callback));
			TEST_ASSERT(!TUI::IsInUse());
			TEST_ASSERT(!TUI::IsStopRequested());
			TUI::Start({});
			TEST_ASSERT(!TUI::IsInUse());
			TEST_ASSERT(!TUI::IsStopRequested());
			TEST_ASSERT(Console::IsEnabled());
			TEST_ASSERT(backend->startCount == 1);
			TEST_ASSERT(backend->stopCount == 1);
			TEST_ASSERT(callback.events.Count() == 4);
			TEST_ASSERT(callback.events[0] == L"Starting");
			TEST_ASSERT(callback.events[1] == L"BufferSizeChanged");
			TEST_ASSERT(callback.events[2] == L"Char");
			TEST_ASSERT(callback.charInfos.Count() == 1);
			TEST_ASSERT(callback.charInfos[0].code == L'Q');
			TEST_ASSERT(callback.events[3] == L"Stopping");
			TEST_ASSERT(TUI::UninstallListener(&callback));
			TEST_ASSERT(!TUI::UninstallListener(&callback));
		});

		TEST_CASE(L"Stopping from Starting skips initial resize")
		{
			auto backend = Ptr(new FakeTuiBackend);
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			callback.onStarting = []() { TUI::Stop(); };
			TUI::InstallListener(&callback);
			TUI::Start({});
			TEST_ASSERT(callback.events.Count() == 2);
			TEST_ASSERT(callback.events[0] == L"Starting");
			TEST_ASSERT(callback.events[1] == L"Stopping");
			TUI::UninstallListener(&callback);
		});

		TEST_CASE(L"Timer is deadline-driven on the Start thread")
		{
			auto backend = Ptr(new FakeTuiBackend);
			tui_test::ScopedTuiBackend backendScope(backend);
			auto ownerThread = Thread::GetCurrentThreadId();
			Callback callback;
			callback.onStarting = [&]()
			{
				TEST_ASSERT(Thread::GetCurrentThreadId() == ownerThread);
				TEST_ERROR(TUI::StartTimer(0));
				TUI::StartTimer(10);
			};
			callback.onTimer = [&]()
			{
				TEST_ASSERT(Thread::GetCurrentThreadId() == ownerThread);
				TUI::StopTimer();
				TUI::Stop();
			};
			TUI::InstallListener(&callback);
			TUI::Start({});
			TEST_ASSERT(backend->time == 10);
			TEST_ASSERT(callback.events.Contains(L"Timer"));
			TUI::UninstallListener(&callback);
		});

		TEST_CASE(L"RunOneCycle is reentrant and consumes queued events")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->PushChar(L'A');
			backend->PushChar(L'B');
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			callback.onStarting = []() { TEST_ASSERT(!TUI::RunOneCycle()); };
			callback.onChar = [](wchar_t code)
			{
				if (code == L'A')
				{
					TEST_ASSERT(!TUI::IsStopRequested());
					TEST_ASSERT(!TUI::RunOneCycle());
				}
				else
				{
					TUI::Stop();
					TEST_ASSERT(TUI::IsStopRequested());
				}
			};
			TUI::InstallListener(&callback);
			TUI::Start({});
			TEST_ASSERT(callback.events[1] == L"Char");
			TEST_ASSERT(callback.events[2] == L"Char");
			TEST_ASSERT(callback.charInfos.Count() == 2);
			TEST_ASSERT(callback.charInfos[0].code == L'A');
			TEST_ASSERT(callback.charInfos[1].code == L'B');
			TUI::UninstallListener(&callback);
		});

		TEST_CASE(L"Resize replaces the buffer and repairs a cut wide character")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->width = 3;
			backend->height = 1;
			backend->PushResize(2, 1);
			tui_test::ScopedTuiBackend backendScope(backend);
			vint resizeCount = 0;
			Callback callback;
			callback.onResize = [&]()
			{
				resizeCount++;
				if (resizeCount == 1)
				{
					TUI::PrintChar({}, U'\u4E00', 1, 0);
				}
				else
				{
					TEST_ASSERT(TUI::GetBufferWidth() == 2);
					TEST_ASSERT(TUI::GetBuffer()[1].glyph == TuiPixelGlyph::Char);
					TEST_ASSERT(TUI::GetBuffer()[1].c == 0);
					TUI::Stop();
				}
			};
			TUI::InstallListener(&callback);
			TUI::Start({});
			TEST_ASSERT(resizeCount == 2);
			TUI::UninstallListener(&callback);
		});

		TEST_CASE(L"Listener generations protect mutation during dispatch")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->PushChar(L'X');
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback first;
			Callback second;
			first.onStarting = [&]()
			{
				TEST_ASSERT(TUI::UninstallListener(&second));
				TEST_ASSERT(TUI::InstallListener(&second));
			};
			second.onChar = [](wchar_t) { TUI::Stop(); };
			TUI::InstallListener(&first);
			TUI::InstallListener(&second);
			TUI::Start({});
			TEST_ASSERT(second.events.Count() == 3);
			TEST_ASSERT(second.events[0] == L"BufferSizeChanged");
			TEST_ASSERT(second.events[1] == L"Char");
			TEST_ASSERT(second.charInfos.Count() == 1);
			TEST_ASSERT(second.charInfos[0].code == L'X');
			TEST_ASSERT(second.events[2] == L"Stopping");
			TUI::UninstallListener(&first);
			TUI::UninstallListener(&second);
		});

		TEST_CASE(L"Nested dispatch snapshots the newly installed listener generation")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->PushChar(L'A');
			backend->PushChar(L'B');
			backend->PushChar(L'C');
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback first;
			Callback second;
			first.onChar = [&](wchar_t code)
			{
				if (code == L'A')
				{
					TEST_ASSERT(TUI::UninstallListener(&second));
					TEST_ASSERT(TUI::InstallListener(&second));
					TEST_ASSERT(TUI::RunOneCycle());
				}
			};
			second.onChar = [](wchar_t code)
			{
				if (code == L'C') TUI::Stop();
			};
			TUI::InstallListener(&first);
			TUI::InstallListener(&second);
			TUI::Start({});
			TEST_ASSERT(second.events.Count() == 5);
			TEST_ASSERT(second.events[0] == L"Starting");
			TEST_ASSERT(second.events[1] == L"BufferSizeChanged");
			TEST_ASSERT(second.events[2] == L"Char");
			TEST_ASSERT(second.events[3] == L"Char");
			TEST_ASSERT(second.charInfos.Count() == 2);
			TEST_ASSERT(second.charInfos[0].code == L'B');
			TEST_ASSERT(second.charInfos[1].code == L'C');
			TEST_ASSERT(second.events[4] == L"Stopping");
			TUI::UninstallListener(&first);
			TUI::UninstallListener(&second);
		});

		TEST_CASE(L"Startup and stopping exceptions still restore backend and Console")
		{
			{
				auto backend = Ptr(new FakeTuiBackend);
				tui_test::ScopedTuiBackend backendScope(backend);
				Callback callback;
				callback.onStarting = []() { throw Exception(L"Starting failed."); };
				TUI::InstallListener(&callback);
				TEST_EXCEPTION(
					TUI::Start({}),
					Exception,
					([](const Exception& e) { TEST_ASSERT(e.Message() == L"Starting failed."); }));
				TEST_ASSERT(callback.events.Count() == 1);
				TEST_ASSERT(backend->stopCount == 1);
				TEST_ASSERT(Console::IsEnabled());
				TEST_ASSERT(!TUI::IsInUse());
				TUI::UninstallListener(&callback);
			}
			{
				auto backend = Ptr(new FakeTuiBackend);
				tui_test::ScopedTuiBackend backendScope(backend);
				Callback callback;
				callback.onStarting = []() { TUI::Stop(); };
				callback.onStopping = []() { throw Exception(L"Stopping failed."); };
				TUI::InstallListener(&callback);
				TEST_EXCEPTION(
					TUI::Start({}),
					Exception,
					([](const Exception& e) { TEST_ASSERT(e.Message() == L"Stopping failed."); }));
				TEST_ASSERT(backend->stopCount == 1);
				TEST_ASSERT(Console::IsEnabled());
				TEST_ASSERT(!TUI::IsInUse());
				TUI::UninstallListener(&callback);
			}
		});

		TEST_CASE(L"Partial initialization failure and repeated starts leave clean state")
		{
			auto backend = Ptr(new FakeTuiBackend);
			tui_test::ScopedTuiBackend backendScope(backend);
			backend->failStart = true;
			TEST_EXCEPTION(
				TUI::Start({}),
				Exception,
				([](const Exception&) {}));
			TEST_ASSERT(!TUI::IsInUse());
			TEST_ASSERT(Console::IsEnabled());

			backend->failStart = false;
			Callback callback;
			callback.onStarting = []()
			{
				TUI::Start({});
				TUI::Stop();
			};
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::Start({});
			TEST_ASSERT(backend->startCount == 3);
			TEST_ASSERT(backend->stopCount == 2);
			TUI::UninstallListener(&callback);
		});

		TEST_CASE(L"Invalid requested and selected color modes fail cleanly")
		{
			auto backend = Ptr(new FakeTuiBackend);
			tui_test::ScopedTuiBackend backendScope(backend);
			TuiStartOptions options;
			options.colorMode = (TuiColorMode)4;
			TEST_ERROR(TUI::Start(options));
			TEST_ASSERT(backend->startCount == 0);

			backend->honorRequestedColorMode = false;
			backend->startColorMode = (TuiColorMode)4;
			TEST_ERROR(TUI::Start({}));
			TEST_ASSERT(backend->startCount == 1);
			TEST_ASSERT(backend->stopCount == 1);
			TEST_ASSERT(Console::IsEnabled());
			TEST_ASSERT(!TUI::IsInUse());
		});

		TEST_CASE(L"Every input callback can stop later dispatch")
		{
			tui_test::TuiBackendEventType types[] = {
				tui_test::TuiBackendEventType::MouseMove,
				tui_test::TuiBackendEventType::MouseDown,
				tui_test::TuiBackendEventType::MouseUp,
				tui_test::TuiBackendEventType::MouseDoubleClick,
				tui_test::TuiBackendEventType::MouseVerticalWheel,
				tui_test::TuiBackendEventType::MouseHorizontalWheel,
				tui_test::TuiBackendEventType::KeyDown,
				tui_test::TuiBackendEventType::KeyUp,
				tui_test::TuiBackendEventType::Char,
			};
			for (auto type : types)
			{
				auto backend = Ptr(new FakeTuiBackend);
				tui_test::TuiBackendEvent event;
				event.type = type;
				backend->events.Add(event);
				backend->PushChar(L'Z');
				tui_test::ScopedTuiBackend backendScope(backend);
				StopOnEventCallback callback;
				callback.type = type;
				callback.expectedThreadId = Thread::GetCurrentThreadId();
				TUI::InstallListener(&callback);
				TUI::Start({});
				TEST_ASSERT(callback.eventCount == 1);
				TEST_ASSERT(callback.stoppingCount == 1);
				TUI::UninstallListener(&callback);
			}
		});

		TEST_CASE(L"IsStopRequested supports bridge-style suppression")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->PushChar(L'X');
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback bridge;
			Callback later;
			vint higherLevelCallbacks = 0;
			bridge.onChar = [&](wchar_t)
			{
				TUI::Stop();
				if (!TUI::IsStopRequested())
				{
					higherLevelCallbacks++;
				}
			};
			TUI::InstallListener(&bridge);
			TUI::InstallListener(&later);
			TUI::Start({});
			TEST_ASSERT(higherLevelCallbacks == 0);
			TEST_ASSERT(later.events.Count() == 3);
			TEST_ASSERT(later.events[0] == L"Starting");
			TEST_ASSERT(later.events[1] == L"BufferSizeChanged");
			TEST_ASSERT(later.events[2] == L"Stopping");
			TEST_ASSERT(!TUI::IsStopRequested());
			TUI::UninstallListener(&bridge);
			TUI::UninstallListener(&later);
		});

		TEST_CASE(L"Active operations reject a non-owner thread")
		{
			auto backend = Ptr(new FakeTuiBackend);
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			callback.onStarting = []()
			{
				vint rejected = 0;
				auto thread = Thread::CreateAndStart([&]()
				{
					try
					{
						TUI::IsInUse();
					}
					catch (const Error&)
					{
						rejected++;
					}
					try
					{
						TUI::Start({});
					}
					catch (const Error&)
					{
						rejected++;
					}
					try
					{
						TUI::GetBuffer();
					}
					catch (const Error&)
					{
						rejected++;
					}
				}, false);
				TEST_ASSERT(thread->Wait());
				delete thread;
				TEST_ASSERT(rejected == 3);
				TUI::Stop();
			};
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);
		});

		TEST_CASE(L"Console size, selected color mode and rendering use the active backend")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->width = 5;
			backend->height = 3;
			backend->honorRequestedColorMode = false;
			backend->startColorMode = TuiColorMode::Color256;
			tui_test::ScopedTuiBackend backendScope(backend);
			vint width = 0;
			vint height = 0;
			TEST_ASSERT(TUI::TryGetConsoleSize(width, height));
			TEST_ASSERT(width == 5 && height == 3);
			Callback callback;
			callback.onStarting = []()
			{
				TEST_ASSERT(TUI::GetColorMode() == TuiColorMode::Color256);
				TEST_ASSERT(TUI::GetBufferWidth() == 5);
				TEST_ASSERT(TUI::GetBufferHeight() == 3);
				TUI::Clear({ 0, 0, 0 }, 0, 0, 4, 2);
				TuiRectOptions options;
				options.glyph = TuiMergeableGlyph::DoubleLine;
				TUI::DrawRect(options, 0, 0, 4, 2);
				TUI::RenderBuffer();
				TUI::Stop();
			};
			TUI::InstallListener(&callback);
			TuiStartOptions options;
			options.colorMode = TuiColorMode::TrueColor;
			TUI::Start(options);
			TEST_ASSERT(backend->renderCount == 1);
			TEST_ASSERT(backend->renderWidth == 5 && backend->renderHeight == 3);
			TEST_ASSERT(backend->renderColorMode == TuiColorMode::Color256);
			TUI::UninstallListener(&callback);
		});

		TEST_CASE(L"RenderBuffer rejects malformed raw cells")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->width = 2;
			backend->height = 1;
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			callback.onStarting = []()
			{
				auto buffer = TUI::GetBuffer();
				auto reset = [&]()
				{
					buffer[0] = TuiPixel();
					buffer[1] = TuiPixel();
				};

				reset();
				buffer[0].glyph = TuiPixelGlyph::Mergeable;
				buffer[0].mergeable = { (TuiMergeableGlyph)4, TuiMergeableGlyph::None, TuiMergeableGlyph::None, TuiMergeableGlyph::None };
				TEST_ERROR(TUI::RenderBuffer());

				reset();
				buffer[0].glyph = TuiPixelGlyph::Mergeable;
				buffer[0].mergeable = { TuiMergeableGlyph::None, TuiMergeableGlyph::None, TuiMergeableGlyph::DoubleLine, TuiMergeableGlyph::None };
				TEST_ERROR(TUI::RenderBuffer());

				reset();
				buffer[0].glyph = TuiPixelGlyph::Unmergeable;
				buffer[0].unmergeable = { TuiUnmergeableGlyph::RoundCorner, (TuiUnmergeableDirection)4 };
				TEST_ERROR(TUI::RenderBuffer());

				reset();
				buffer[0].glyph = (TuiPixelGlyph)4;
				TEST_ERROR(TUI::RenderBuffer());

				reset();
				buffer[0].c = (char32_t)0xD800;
				TEST_ERROR(TUI::RenderBuffer());

				reset();
				buffer[0].c = U'\u0301';
				TEST_ERROR(TUI::RenderBuffer());

				reset();
				buffer[0].c = U'\u4E00';
				TEST_ERROR(TUI::RenderBuffer());

				reset();
				buffer[0].glyph = TuiPixelGlyph::WideCharContinuation;
				TEST_ERROR(TUI::RenderBuffer());

				reset();
				TUI::PrintChar({}, U'\u4E00', 0, 0);
				buffer[1].foregroundColor = { 1, 2, 3 };
				TEST_ERROR(TUI::RenderBuffer());

				reset();
				buffer[0].glyph = TuiPixelGlyph::Mergeable;
				buffer[0].mergeable = {};
				TUI::RenderBuffer();

				reset();
				TUI::PrintChar({}, U'\u4E00', 0, 0);
				TUI::RenderBuffer();
				TUI::Stop();
			};
			TUI::InstallListener(&callback);
			TUI::Start({});
			TEST_ASSERT(backend->renderCount == 2);
			TUI::UninstallListener(&callback);
		});

		TEST_CASE(L"Nested callback exceptions stay latched until Start cleanup")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->PushChar(L'A');
			backend->PushChar(L'B');
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			bool caughtNested = false;
			callback.onStarting = []() { TUI::RunOneCycle(); };
			callback.onChar = [&](wchar_t code)
			{
				if (code == L'A')
				{
					try
					{
						TUI::RunOneCycle();
					}
					catch (const Exception&)
					{
						caughtNested = true;
					}
				}
				else
				{
					throw Exception(L"Nested callback failed.");
				}
			};
			TUI::InstallListener(&callback);
			TEST_EXCEPTION(
				TUI::Start({}),
				Exception,
				([](const Exception& e) { TEST_ASSERT(e.Message() == L"Nested callback failed."); }));
			TEST_ASSERT(caughtNested);
			TEST_ASSERT(callback.events.Count() == 3);
			TEST_ASSERT(callback.events[0] == L"Starting");
			TEST_ASSERT(callback.events[1] == L"Char");
			TEST_ASSERT(callback.events[2] == L"Char");
			TEST_ASSERT(callback.charInfos.Count() == 2);
			TEST_ASSERT(callback.charInfos[0].code == L'A');
			TEST_ASSERT(callback.charInfos[1].code == L'B');
			TEST_ASSERT(backend->stopCount == 1);
			TEST_ASSERT(Console::IsEnabled());
			TEST_ASSERT(!TUI::IsInUse());
			TUI::UninstallListener(&callback);
		});
	});

	TEST_CATEGORY(L"TuiPlayground regression")
	{
		TEST_CASE(L"Strict parser accepts every command shape and preserves payloads")
		{
			PaintingCommand command;
			U32String reason;

			TEST_ASSERT(TryParseCommand(U"fC fFaA00", command, reason));
			TEST_ASSERT(command.Get<SetForegroundColorCommand>().color == TuiColor({ 255, 170, 0 }));

			TEST_ASSERT(TryParseCommand(U"Bc ClEaR", command, reason));
			TEST_ASSERT(!command.Get<SetBackgroundColorCommand>().color);
			TEST_ASSERT(TryParseCommand(U"BC 000000", command, reason));
			TEST_ASSERT(command.Get<SetBackgroundColorCommand>().color.Value() == TuiColor({ 0, 0, 0 }));

			const char32_t* lineFormats[] = { U"thin", U"THICK", U"DoUbLe" };
			const TuiMergeableGlyph lineGlyphs[] =
			{
				TuiMergeableGlyph::ThinLine,
				TuiMergeableGlyph::ThickLine,
				TuiMergeableGlyph::DoubleLine,
			};
			for (vint i = 0; i < 3; i++)
			{
				TEST_ASSERT(TryParseCommand(U32String(U"LINEV ") + lineFormats[i] + U" -1 -2 3", command, reason));
				auto&& vertical = command.Get<DrawLineVCommand>();
				TEST_ASSERT(vertical.glyph == lineGlyphs[i] && vertical.x == -1 && vertical.y1 == -2 && vertical.y2 == 3);

				TEST_ASSERT(TryParseCommand(U32String(U"lineh ") + lineFormats[i] + U" -3 4 5", command, reason));
				auto&& horizontal = command.Get<DrawLineHCommand>();
				TEST_ASSERT(horizontal.glyph == lineGlyphs[i] && horizontal.x1 == -3 && horizontal.x2 == 4 && horizontal.y == 5);

				TEST_ASSERT(TryParseCommand(U32String(U"RECT ") + lineFormats[i] + U" -3 -2 4 5", command, reason));
				auto&& rectangle = command.Get<DrawRectCommand>();
				TEST_ASSERT(rectangle.glyph == lineGlyphs[i] && rectangle.corner == TuiRectCorner::Sharp);
			}

			TEST_ASSERT(TryParseCommand(U"rEcT rOuNd -3 -2 4 5", command, reason));
			auto&& rounded = command.Get<DrawRectCommand>();
			TEST_ASSERT(rounded.glyph == TuiMergeableGlyph::ThinLine && rounded.corner == TuiRectCorner::Round);

			TEST_ASSERT(TryParseCommand(U"cLeAr aBcDeF -4 -3 2 1", command, reason));
			auto&& clear = command.Get<ClearRectCommand>();
			TEST_ASSERT(clear.backgroundColor == TuiColor({ 0xAB, 0xCD, 0xEF }));
			TEST_ASSERT(clear.x1 == -4 && clear.y1 == -3 && clear.x2 == 2 && clear.y2 == 1);

			TEST_ASSERT(TryParseCommand(U"TyPe -1 2:Ab :\U0001F600", command, reason));
			auto&& type = command.Get<TypeCommand>();
			TEST_ASSERT(type.x == -1 && type.y == 2);
			TEST_ASSERT(type.text == U"Ab :\U0001F600");

			vint parsed = 0;
			TEST_ASSERT(TryParseVint(wtou32(itow(std::numeric_limits<vint>::min())), parsed));
			TEST_ASSERT(parsed == std::numeric_limits<vint>::min());
			TEST_ASSERT(TryParseVint(wtou32(itow(std::numeric_limits<vint>::max())), parsed));
			TEST_ASSERT(parsed == std::numeric_limits<vint>::max());
		});

		TEST_CASE(L"Strict parser rejects malformed commands, whitespace, overflow and ranges")
		{
			List<U32String> invalid;
			invalid.Add(U32String(U""));
			invalid.Add(U32String(U" FC FFFFFF"));
			invalid.Add(U32String(U"FC  FFFFFF"));
			invalid.Add(U32String(U"FC FFFFFF "));
			invalid.Add(U32String(U"FC FFFFF"));
			invalid.Add(U32String(U"FC GFFFFF"));
			invalid.Add(U32String(U"BC clear "));
			invalid.Add(U32String(U"LINEV ROUND 0 0 1"));
			invalid.Add(U32String(U"LINEV THIN 0 2 1"));
			invalid.Add(U32String(U"LINEH THIN 2 1 0"));
			invalid.Add(U32String(U"RECT ROUND 0 0 0 1"));
			invalid.Add(U32String(U"RECT CURVE 0 0 1 1"));
			invalid.Add(U32String(U"CLEAR FFFFFF 0 2 1 1"));
			invalid.Add(U32String(U"TYPE 0 0:"));
			invalid.Add(U32String(U"TYPE +0 0:text"));
			invalid.Add(U32String(U"TYPE 0  0:text"));
			invalid.Add(U32String(U"TYPE 0 0 :text"));
			invalid.Add(U32String(U"UNKNOWN"));
			invalid.Add(U32String(U"LINEV THIN ") + wtou32(itow(std::numeric_limits<vint>::max())) + U"0 0 1");
			invalid.Add(U32String(U"LINEH THIN -") + wtou32(itow(std::numeric_limits<vint>::max())) + U"0 1 0");

			for (auto&& text : invalid)
			{
				PaintingCommand command;
				U32String reason;
				TEST_ASSERT(!TryParseCommand(text, command, reason));
				TEST_ASSERT(reason.Length() > 0);
			}
		});

		TEST_CASE(L"q and Q are command text and only Escape exits")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->PushChar(L'q');
			backend->PushChar(L'Q');
			backend->PushChar((wchar_t)0x1B);
			tui_test::ScopedTuiBackend backendScope(backend);
			PlaygroundCallback callback;
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);
			TEST_ASSERT(backend->events.Count() == 0);
			TEST_ASSERT(callback.state.typingCommand == U"qQ");
		});

		TEST_CASE(L"The bottom command row has a distinct dark gray background")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->PushChar((wchar_t)0x1B);
			tui_test::ScopedTuiBackend backendScope(backend);
			PlaygroundCallback callback;
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);
			TEST_ASSERT(backend->renderedBuffer.Count() == backend->width * backend->height);
			for (vint x = 0; x < backend->width; x++)
			{
				TEST_ASSERT(backend->renderedBuffer[(backend->height - 1) * backend->width + x].backgroundColor == TuiColor({ 64, 64, 64 }));
			}
		});

		TEST_CASE(L"Native units resynchronize without swallowing scalars or Backspace")
		{
			auto backend = Ptr(new FakeTuiBackend);
#if defined VCZH_WCHAR_UTF16
			auto high = (wchar_t)0xD83D;
			auto low = (wchar_t)0xDE00;
			backend->PushChar(low);
			backend->PushChar(high);
			backend->PushChar(high);
			backend->PushChar(low);
			backend->PushChar(high);
			backend->PushChar(L'B');
			backend->PushChar(high);
			backend->PushChar(L'\b');
			backend->PushChar((wchar_t)0x7F);
#else
			backend->PushChar((wchar_t)U'\U0001F600');
			backend->PushChar(L'B');
			backend->PushChar(L'\b');
			backend->PushChar((wchar_t)0x7F);
#endif
			backend->PushChar(L'q');
			backend->PushChar(L'Q');
			backend->PushChar((wchar_t)0x1B);
			tui_test::ScopedTuiBackend backendScope(backend);
			PlaygroundCallback callback;
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);
			TEST_ASSERT(callback.state.typingCommand == U"qQ");
#if defined VCZH_WCHAR_UTF16
			TEST_ASSERT(callback.state.pendingHighSurrogate == 0);
#endif
		});

		TEST_CASE(L"Malformed native units do not swallow Enter or Escape and errors retain no pending input")
		{
			auto backend = Ptr(new FakeTuiBackend);
#if defined VCZH_WCHAR_UTF16
			auto high = (wchar_t)0xD83D;
			auto low = (wchar_t)0xDE00;
			backend->PushChar(high);
#endif
			backend->PushChar(L'\r');
#if defined VCZH_WCHAR_UTF16
			backend->PushChar(high);
			backend->PushChar(low);
#endif
			backend->PushChar(L'Z');
			backend->PushChar(L'\r');
#if defined VCZH_WCHAR_UTF16
			backend->PushChar(high);
#endif
			backend->PushChar(L'\n');
			backend->PushChar(L'\r');
#if defined VCZH_WCHAR_UTF16
			backend->PushChar(high);
#endif
			backend->PushChar((wchar_t)0x1B);
			tui_test::ScopedTuiBackend backendScope(backend);
			PlaygroundCallback callback;
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);
			TEST_ASSERT(!callback.state.error);
			TEST_ASSERT(callback.state.typingCommand.Length() == 0);
#if defined VCZH_WCHAR_UTF16
			TEST_ASSERT(callback.state.pendingHighSurrogate == 0);
#endif
		});

		TEST_CASE(L"Wrapping grows the command box and moves a full-row cursor to the next line")
		{
			auto wrapped = WrapText(U"abcd", 4, true);
			TEST_ASSERT(wrapped.rowWidths.Count() == 2);
			TEST_ASSERT(wrapped.rowWidths[0] == 4 && wrapped.rowWidths[1] == 0);
			TEST_ASSERT(wrapped.cursorX == 0 && wrapped.cursorRow == 1);

			auto backend = Ptr(new FakeTuiBackend);
			backend->width = 4;
			backend->height = 5;
			backend->PushText(U"abcd");
			backend->PushChar((wchar_t)0x1B);
			tui_test::ScopedTuiBackend backendScope(backend);
			PlaygroundCallback callback;
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);

			TEST_ASSERT(backend->renderedBuffer[0].GetChar32() == U'\u2554');
			TEST_ASSERT(backend->renderedBuffer[2 * 4].GetChar32() == U'\u255A');
			for (vint x = 0; x < 4; x++)
			{
				TEST_ASSERT(backend->renderedBuffer[3 * 4 + x].GetChar32() == U'a' + x);
				TEST_ASSERT(backend->renderedBuffer[3 * 4 + x].backgroundColor == TuiColor({ 64, 64, 64 }));
			}
			TEST_ASSERT(backend->renderedBuffer[4 * 4].GetChar32() == U'\u2588');
		});

		TEST_CASE(L"A width-two scalar is retained in one column and appears after resize")
		{
			auto narrow = WrapText(U"\U0001F600", 1, true);
			TEST_ASSERT(narrow.scalars.Count() == 0);
			TEST_ASSERT(narrow.rowWidths.Count() == 1);

			auto backend = Ptr(new FakeTuiBackend);
			backend->width = 1;
			backend->height = 3;
			backend->PushScalar(U'\U0001F600');
			backend->PushResize(2, 3);
			backend->PushChar((wchar_t)0x1B);
			tui_test::ScopedTuiBackend backendScope(backend);
			PlaygroundCallback callback;
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);

			TEST_ASSERT(callback.state.typingCommand == U"\U0001F600");
			TEST_ASSERT(backend->renderWidth == 2 && backend->renderHeight == 3);
			TEST_ASSERT(backend->renderedBuffer[2].GetChar32() == U'\U0001F600');
			TEST_ASSERT(backend->renderedBuffer[3].glyph == TuiPixelGlyph::WideCharContinuation);
			TEST_ASSERT(backend->renderedBuffer[4].GetChar32() == U'\u2588');
		});

		TEST_CASE(L"The 500 millisecond timer alternates hidden and visible cursor frames")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->escapeAfterTimeouts = 2;
			tui_test::ScopedTuiBackend backendScope(backend);
			PlaygroundCallback callback;
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);

			TEST_ASSERT(backend->waitTimeouts.Count() == 3);
			for (auto timeout : backend->waitTimeouts)
			{
				TEST_ASSERT(timeout == 500);
			}
			TEST_ASSERT(backend->renderedTexts.Count() == 4);
			auto cursor = (backend->height - 1) * backend->width;
			TEST_ASSERT(backend->renderedTexts[0][cursor] == U'\u2588');
			TEST_ASSERT(backend->renderedTexts[1][cursor] == U'\u2588');
			TEST_ASSERT(backend->renderedTexts[2][cursor] == 0);
			TEST_ASSERT(backend->renderedTexts[3][cursor] == U'\u2588');
		});

		TEST_CASE(L"Commands replay in logical paper coordinates with clipping and background preservation")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->width = 12;
			backend->height = 8;
			backend->PushCommand(U"FC FF0000");
			backend->PushCommand(U"BC 112233");
			backend->PushCommand(U"CLEAR 445566 0 0 9 4");
			backend->PushCommand(U"LINEH THIN -2 9 1");
			backend->PushCommand(U"LINEV DOUBLE 2 -2 4");
			backend->PushCommand(U"RECT ROUND 4 0 8 4");
			backend->PushCommand(U"BC CLEAR");
			backend->PushCommand(U"TYPE 0 2:\u754CA");
			backend->PushCommand(U"LINEH THICK 0 9 4");
			backend->PushResize(14, 9);
			backend->PushChar((wchar_t)0x1B);
			tui_test::ScopedTuiBackend backendScope(backend);
			PlaygroundCallback callback;
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);

			TEST_ASSERT(callback.state.commands.Count() == 9);
			TEST_ASSERT(backend->renderWidth == 14 && backend->renderHeight == 9);
			TEST_ASSERT(backend->renderedBuffer[0].GetChar32() == U'\u2554');
			TEST_ASSERT(backend->renderedBuffer[13].GetChar32() == U'\u2557');
			TEST_ASSERT(backend->renderedBuffer[7 * 14].GetChar32() == U'\u255A');
			TEST_ASSERT(backend->renderedBuffer[7 * 14 + 13].GetChar32() == U'\u255D');

			auto&& wide = backend->renderedBuffer[3 * 14 + 1];
			auto&& continuation = backend->renderedBuffer[3 * 14 + 2];
			auto&& ascii = backend->renderedBuffer[3 * 14 + 3];
			TEST_ASSERT(wide.GetChar32() == U'\u754C');
			TEST_ASSERT(continuation.glyph == TuiPixelGlyph::WideCharContinuation);
			TEST_ASSERT(wide.foregroundColor == TuiColor({ 255, 0, 0 }));
			TEST_ASSERT(wide.backgroundColor == TuiColor({ 0x44, 0x55, 0x66 }));
			TEST_ASSERT(continuation.backgroundColor == wide.backgroundColor);
			TEST_ASSERT(ascii.GetChar32() == U'A');
			TEST_ASSERT(ascii.foregroundColor == TuiColor({ 255, 0, 0 }));
			TEST_ASSERT(ascii.backgroundColor == TuiColor({ 0x11, 0x22, 0x33 }));

			TEST_ASSERT(backend->renderedBuffer[1 * 14 + 5].GetChar32() == U'\u256D');
			TEST_ASSERT(backend->renderedBuffer[5 * 14 + 1].backgroundColor == TuiColor({ 0x44, 0x55, 0x66 }));
			TEST_ASSERT(backend->renderedBuffer[1 * 14 + 11].backgroundColor == TuiColor({ 0, 0, 0 }));
			for (vint x = 0; x < 14; x++)
			{
				TEST_ASSERT(backend->renderedBuffer[8 * 14 + x].backgroundColor == TuiColor({ 64, 64, 64 }));
			}
		});

		TEST_CASE(L"Parse errors preserve Unicode, hide the cursor and stay inside both borders")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->width = 14;
			backend->height = 8;
			backend->PushCommand(U"BAD \U0001F600");
			backend->PushChar(L'X');
#if defined VCZH_WCHAR_UTF16
			backend->PushChar((wchar_t)0xD83D);
			backend->PushChar((wchar_t)0xDE00);
#endif
			backend->PushChar((wchar_t)0x1B);
			tui_test::ScopedTuiBackend backendScope(backend);
			PlaygroundCallback callback;
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);

			TEST_ASSERT(callback.state.error);
			TEST_ASSERT(callback.state.error.Value().originalCommand == U"BAD \U0001F600");
			TEST_ASSERT(callback.state.typingCommand.Length() == 0);
#if defined VCZH_WCHAR_UTF16
			TEST_ASSERT(callback.state.pendingHighSurrogate == 0);
#endif
			TEST_ASSERT(backend->renderedBuffer[0].GetChar32() == U'\u2554');
			TEST_ASSERT(backend->renderedBuffer[6 * 14].GetChar32() == U'\u255A');
			TEST_ASSERT(backend->renderedBuffer[1 * 14 + 1].GetChar32() == U'\u256D');
			TEST_ASSERT(backend->renderedBuffer[1 * 14 + 12].GetChar32() == U'\u256E');
			TEST_ASSERT(backend->renderedBuffer[5 * 14 + 1].GetChar32() == U'\u2570');
			TEST_ASSERT(backend->renderedBuffer[5 * 14 + 12].GetChar32() == U'\u256F');
			for (vint x = 0; x < 14; x++)
			{
				TEST_ASSERT(backend->renderedBuffer[7 * 14 + x].backgroundColor == TuiColor({ 64, 64, 64 }));
				TEST_ASSERT(backend->renderedBuffer[7 * 14 + x].GetChar32() != U'\u2588');
			}
		});

		TEST_CASE(L"Enter dismisses an error without submitting an empty command")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->PushCommand(U"INVALID");
			backend->PushChar(L'Z');
			backend->PushChar(L'\r');
			backend->PushChar(L'q');
			backend->PushChar((wchar_t)0x1B);
			tui_test::ScopedTuiBackend backendScope(backend);
			PlaygroundCallback callback;
			TUI::InstallListener(&callback);
			TUI::Start({});
			TUI::UninstallListener(&callback);
			TEST_ASSERT(!callback.state.error);
			TEST_ASSERT(callback.state.commands.Count() == 0);
			TEST_ASSERT(callback.state.typingCommand == U"q");
		});
	});
}
