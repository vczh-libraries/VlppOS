/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "../../../Source/TUI/TUI.h"

using namespace vl;
using namespace vl::console;

namespace
{
	class PlaygroundCallback : public ITuiCallback
	{
	private:
		void Redraw()
		{
			auto width = TUI::GetBufferWidth();
			auto height = TUI::GetBufferHeight();
			TUI::Clear({ 0, 0, 0 }, 0, 0, width - 1, height - 1);
			if (width >= 2 && height >= 2)
			{
				TuiRectOptions options;
				options.glyph = TuiMergeableGlyph::DoubleLine;
				options.foregroundColor = { 255, 255, 255 };
				TUI::DrawRect(options, 0, 0, width - 1, height - 1);
			}
			TUI::RenderBuffer();
		}

	public:
		void BufferSizeChanged() override
		{
			Redraw();
		}

		void Char(const TuiCharInfo& info) override
		{
			if (info.code == L'q' || info.code == L'Q' || info.code == (wchar_t)0x1B)
			{
				TUI::Stop();
			}
		}
	};

#if !defined VCZH_TUI_PLAYGROUND_TEST
	vint RunPlayground()
	{
		PlaygroundCallback callback;
		CHECK_ERROR(TUI::InstallListener(&callback), L"TuiPlayground failed to install its callback.");
		try
		{
			TUI::Start({});
		}
		catch (...)
		{
			TUI::UninstallListener(&callback);
			throw;
		}
		CHECK_ERROR(TUI::UninstallListener(&callback), L"TuiPlayground failed to uninstall its callback.");
		return 0;
	}
#endif
}

#if !defined VCZH_TUI_PLAYGROUND_TEST
#if defined VCZH_MSVC
int wmain()
#elif defined VCZH_GCC
int main()
#endif
{
	vint result = 1;
	try
	{
		result = RunPlayground();
	}
	catch (const Exception& exception)
	{
		Console::WriteLine(L"Error: " + exception.Message());
	}
	catch (const Error& error)
	{
		Console::WriteLine(L"Error: " + WString(error.Description()));
	}
	catch (...)
	{
		Console::WriteLine(L"Error: Unknown application failure.");
	}

	ThreadLocalStorage::DisposeStorages();
	FinalizeGlobalStorage();
#if defined VCZH_MSVC && defined VCZH_CHECK_MEMORY_LEAKS
	_CrtDumpMemoryLeaks();
#endif
	return (int)result;
}
#endif
