/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Locale.h"

#if defined VCZH_GCC || defined VCZH_WASM


namespace vl
{
	ILocaleImpl* GetOSLocaleImpl()
	{
		static EnUsLocaleImpl linuxLocaleImpl;
		return &linuxLocaleImpl;
	}
}

#endif
