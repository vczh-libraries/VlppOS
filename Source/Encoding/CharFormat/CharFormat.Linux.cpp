/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "CharFormat.h"
#include <string.h>

#ifndef VCZH_GCC
static_assert(false, "Do not build this file for Windows applications.");
#endif

namespace vl
{
	namespace stream
	{
		using namespace vl::encoding;

		bool IsMbcsLeadByte(char c)
		{
			return (vint8_t)c < 0;
		}

		void MbcsToWChar(wchar_t* wideBuffer, vint wideChars, vint wideReaded, char* mbcsBuffer, vint mbcsChars)
		{
			AString a = AString::CopyFrom(mbcsBuffer, mbcsChars);
			WString w = atow(a);
			memcpy(wideBuffer, w.Buffer(), wideReaded * sizeof(wchar_t));
		}

/***********************************************************************
Mbcs
***********************************************************************/

		vint MbcsEncoder::WriteString(wchar_t* _buffer, vint chars)
		{
			WString w = WString::CopyFrom(_buffer, chars);
			AString a = wtoa(w);
			vint length = a.Length();
			vint result = stream->Write((void*)a.Buffer(), length);

			if (result != length)
			{
				Close();
				return 0;
			}
			return chars;
		}
	}
}
