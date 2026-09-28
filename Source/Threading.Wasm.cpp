/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Threading.h"

#if defined VCZH_WASM
#include <emscripten/threading.h>

namespace vl
{

/***********************************************************************
Thread
***********************************************************************/

	void Thread::Sleep(vint ms)
	{
		emscripten_thread_sleep(ms);
	}

	vint Thread::GetCPUCount()
	{
		return emscripten_num_logical_cores();
	}
}

#endif
