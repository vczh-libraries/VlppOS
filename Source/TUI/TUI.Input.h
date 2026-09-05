/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_TUI_INPUT
#define VCZH_TUI_INPUT

#include "TUI.Internal.h"

namespace vl
{
	namespace console
	{
		namespace tui_internal
		{
			// Incremental xterm input conversion, shared by the POSIX backend and tests.
			class PosixTuiInputDecoder
			{
			private:
				Nullable<vuint64_t>			lastClickTime;
				vint							lastClickX = -1;
				vint							lastClickY = -1;
				presentation::NativeMouseButton lastClickButton = presentation::NativeMouseButton::Left;
				bool							left = false;
				bool							middle = false;
				bool							right = false;
				bool							discardSequence = false;
				vuint8_t						controlString = 0;

				void							QueueKey(presentation::NativeWindowKeyInfo info, Nullable<char32_t> text);
				void							DecodeSequence(vint end, vuint64_t now);
				bool							DecodeText(bool alt);
			public:
				collections::List<vuint8_t>	inputBytes;
				collections::List<unittest::TuiBackendEvent> pendingEvents;
				Nullable<vuint64_t>			escapeDeadline;

				void							ParseInput(vuint64_t now);
			};
		}
	}
}

#endif
