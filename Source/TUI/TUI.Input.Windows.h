/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_TUI_INPUT_WINDOWS
#define VCZH_TUI_INPUT_WINDOWS

#include "TUI.Internal.h"

#ifdef VCZH_MSVC
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <Windows.h>

namespace vl
{
	namespace console
	{
		namespace tui_internal
		{
			// The production record decoder is also used directly by deterministic tests.
			class WindowsTuiInputDecoder
			{
			private:
				bool							keys[256] = {};
				DWORD							mouseButtons = 0;
				void							QueueMouseButton(unittest::TuiBackendEventType type, presentation::NativeMouseButton button, const presentation::WindowMouseInfo& info);
			public:
				collections::List<unittest::TuiBackendEvent> pendingEvents;

				void							DecodeKey(const KEY_EVENT_RECORD& record);
				void							DecodeMouse(const MOUSE_EVENT_RECORD& record, COORD viewportOrigin);
			};
		}
	}
}
#endif

#endif
