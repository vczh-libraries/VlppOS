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

/***********************************************************************
Mbcs
***********************************************************************/

		vint MbcsEncoder::WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate)
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

		void MbcsToWChar(wchar_t* wideBuffer, vint wideChars, vint wideReaded, char* mbcsBuffer, vint mbcsChars)
		{
			AString a = AString::CopyFrom(mbcsBuffer, mbcsChars);
			WString w = atow(a);
			memcpy(wideBuffer, w.Buffer(), wideReaded * sizeof(wchar_t));
		}

/***********************************************************************
Utf8
***********************************************************************/

		vint Utf8Encoder::WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate)
		{
			UtfStringRangeToStringRangeReader<wchar_t, char8_t> reader(_buffer, chars);
			while (char8_t c = reader.Read())
			{
				vint written = stream->Write(&c, sizeof(c));
				if (written != sizeof(c))
				{
					Close();
					return 0;
				}
			}
			if (reader.HasIllegalChar())
			{
				Close();
				return 0;
			}
			return chars;
		}
	}
}
