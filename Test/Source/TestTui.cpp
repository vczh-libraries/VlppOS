/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "../../Source/TUI/TUI.h"

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
		bool								failStart = false;

		TuiColorMode Start(const TuiStartOptions& options) override
		{
			startCount++;
			if (failStart)
			{
				throw Exception(L"Fake backend startup failure.");
			}
			return options.colorMode == TuiColorMode::Auto ? TuiColorMode::TrueColor : options.colorMode;
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
				time += milliseconds;
			}
			return false;
		}

		void Render(const TuiPixel*, vint, vint, TuiColorMode) override
		{
			renderCount++;
		}

		void PushChar(char32_t code)
		{
			tui_test::TuiBackendEvent event;
			event.type = tui_test::TuiBackendEventType::Char;
			event.charInfo.code = code;
			events.Add(event);
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
		Func<void()>		onStarting;
		Func<void()>		onStopping;
		Func<void()>		onResize;
		Func<void()>		onTimer;
		Func<void(char32_t)>	onChar;

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
			events.Add(L"Char:" + u32tow(U32String::CopyFrom(&info.code, 1)));
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
		vint							stoppingCount = 0;
		vint							eventCount = 0;

		void Starting() override
		{
			if (stopOnStarting) TUI::Stop();
		}

		void Stopping() override
		{
			stoppingCount++;
		}

		void BufferSizeChanged() override
		{
			if (stopOnResize) TUI::Stop();
		}

		void MouseMove(const TuiMouseInfo&) override
		{
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseMove) TUI::Stop();
		}

		void MouseDown(TuiMouseButton, const TuiMouseInfo&) override
		{
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseDown) TUI::Stop();
		}

		void MouseUp(TuiMouseButton, const TuiMouseInfo&) override
		{
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseUp) TUI::Stop();
		}

		void MouseDoubleClick(TuiMouseButton, const TuiMouseInfo&) override
		{
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseDoubleClick) TUI::Stop();
		}

		void MouseVerticalWheel(const TuiMouseInfo&) override
		{
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseVerticalWheel) TUI::Stop();
		}

		void MouseHorizontalWheel(const TuiMouseInfo&) override
		{
			eventCount++;
			if (type == tui_test::TuiBackendEventType::MouseHorizontalWheel) TUI::Stop();
		}

		void KeyDown(const TuiKeyInfo&) override
		{
			eventCount++;
			if (type == tui_test::TuiBackendEventType::KeyDown) TUI::Stop();
		}

		void KeyUp(const TuiKeyInfo&) override
		{
			eventCount++;
			if (type == tui_test::TuiBackendEventType::KeyUp) TUI::Stop();
		}

		void Char(const TuiCharInfo&) override
		{
			eventCount++;
			if (type == tui_test::TuiBackendEventType::Char) TUI::Stop();
		}

		void Timer() override
		{
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
				auto code = pixel.GetChar32();
				auto empty = up == 0 && down == 0 && left == 0 && right == 0;
				TEST_ASSERT(empty ? code == 0 : code >= U'\u2500' && code <= U'\u257F');
			}
		});

		TEST_CASE(L"Supported and unsupported double states are distinguished")
		{
			TuiPixel pixel;
			pixel.glyph = TuiPixelGlyph::Mergeable;

			pixel.mergeable = { TuiMergeableGlyph::None, TuiMergeableGlyph::None, TuiMergeableGlyph::DoubleLine, TuiMergeableGlyph::DoubleLine };
			TEST_ASSERT(pixel.GetChar32() == U'\u2550');
			pixel.mergeable = { TuiMergeableGlyph::DoubleLine, TuiMergeableGlyph::DoubleLine, TuiMergeableGlyph::None, TuiMergeableGlyph::None };
			TEST_ASSERT(pixel.GetChar32() == U'\u2551');
			pixel.mergeable = { TuiMergeableGlyph::None, TuiMergeableGlyph::DoubleLine, TuiMergeableGlyph::None, TuiMergeableGlyph::DoubleLine };
			TEST_ASSERT(pixel.GetChar32() == U'\u2554');
			pixel.mergeable = { TuiMergeableGlyph::None, TuiMergeableGlyph::None, TuiMergeableGlyph::DoubleLine, TuiMergeableGlyph::None };
			TEST_ASSERT(pixel.GetChar32() == 0);
			pixel.mergeable = { TuiMergeableGlyph::ThickLine, TuiMergeableGlyph::DoubleLine, TuiMergeableGlyph::None, TuiMergeableGlyph::None };
			TEST_ASSERT(pixel.GetChar32() == 0);
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

		TEST_CASE(L"MeasureChar uses the shared scalar-width policy")
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
		});
	});

	TEST_CATEGORY(L"Lifecycle and callbacks")
	{
		TEST_CASE(L"Start, initial resize, character stop and cleanup ordering")
		{
			auto backend = Ptr(new FakeTuiBackend);
			backend->PushChar(U'Q');
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			callback.onChar = [](char32_t) { TUI::Stop(); };
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
			TEST_ASSERT(callback.events[2] == L"Char:Q");
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
			backend->PushChar(U'A');
			backend->PushChar(U'B');
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback callback;
			callback.onStarting = []() { TEST_ASSERT(TUI::RunOneCycle()); };
			callback.onChar = [](char32_t code)
			{
				if (code == U'A')
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
			TEST_ASSERT(callback.events[1] == L"Char:A");
			TEST_ASSERT(callback.events[2] == L"Char:B");
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
			backend->PushChar(U'X');
			tui_test::ScopedTuiBackend backendScope(backend);
			Callback first;
			Callback second;
			first.onStarting = [&]()
			{
				TEST_ASSERT(TUI::UninstallListener(&second));
				TEST_ASSERT(TUI::InstallListener(&second));
			};
			first.onChar = [](char32_t) { TUI::Stop(); };
			TUI::InstallListener(&first);
			TUI::InstallListener(&second);
			TUI::Start({});
			TEST_ASSERT(second.events.Count() == 2);
			TEST_ASSERT(second.events[0] == L"BufferSizeChanged");
			TEST_ASSERT(second.events[1] == L"Char:X");
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
			TEST_ASSERT(backend->startCount == 2);
			TEST_ASSERT(backend->stopCount == 1);
			TUI::UninstallListener(&callback);
		});

		TEST_CASE(L"Every input callback can stop later dispatch")
		{
			auto types = Array<tui_test::TuiBackendEventType>({
				tui_test::TuiBackendEventType::MouseMove,
				tui_test::TuiBackendEventType::MouseDown,
				tui_test::TuiBackendEventType::MouseUp,
				tui_test::TuiBackendEventType::MouseDoubleClick,
				tui_test::TuiBackendEventType::MouseVerticalWheel,
				tui_test::TuiBackendEventType::MouseHorizontalWheel,
				tui_test::TuiBackendEventType::KeyDown,
				tui_test::TuiBackendEventType::KeyUp,
				tui_test::TuiBackendEventType::Char,
			});
			for (auto type : types)
			{
				auto backend = Ptr(new FakeTuiBackend);
				tui_test::TuiBackendEvent event;
				event.type = type;
				backend->events.Add(event);
				backend->PushChar(U'Z');
				tui_test::ScopedTuiBackend backendScope(backend);
				StopOnEventCallback callback;
				callback.type = type;
				TUI::InstallListener(&callback);
				TUI::Start({});
				TEST_ASSERT(callback.eventCount == 1);
				TEST_ASSERT(callback.stoppingCount == 1);
				TUI::UninstallListener(&callback);
			}
		});
	});
}
