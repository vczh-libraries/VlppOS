/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_TUI_INTERNAL
#define VCZH_TUI_INTERNAL

#include "TUI.h"

namespace vl
{
	namespace console
	{
		namespace tui_internal
		{
			extern vint QuantizeColor(TuiColor color, TuiColorMode colorMode, const TuiColor* customColor16 = nullptr);
			extern TuiColor GetCanonicalColor(vint index);
			extern bool IsZeroWidthCodePoint(char32_t code);
			extern bool IsTwoWidthCodePoint(char32_t code);
			extern Ptr<unittest::ITuiBackend> CreateTuiBackend();
		}
	}
}

#endif
