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
MbcsEncoder
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

/***********************************************************************
Helper Functions
***********************************************************************/

		extern bool CanBeMbcs(unsigned char* buffer, vint size);
		extern bool CanBeUtf8(unsigned char* buffer, vint size);
		extern bool CanBeUtf16(unsigned char* buffer, vint size, bool& hitSurrogatePairs);
		extern bool CanBeUtf16BE(unsigned char* buffer, vint size, bool& hitSurrogatePairs);
		
/***********************************************************************
TestEncoding
***********************************************************************/

		extern void TestEncodingInternal(
			unsigned char* buffer,
			vint size,
			BomEncoder::Encoding& encoding,
			bool containsBom,
			bool utf16HitSurrogatePairs,
			bool utf16BEHitSurrogatePairs,
			bool roughMbcs,
			bool roughUtf8,
			bool roughUtf16,
			bool roughUtf16BE
			)
		{
			if (roughUtf16 && roughUtf16BE && !roughUtf8)
			{
				if (utf16BEHitSurrogatePairs && !utf16HitSurrogatePairs)
				{
					encoding = BomEncoder::Utf16BE;
				}
				else
				{
					encoding = BomEncoder::Utf16;
				}
			}
			else
			{
				encoding = BomEncoder::Utf8;
			}
		}
	}
}
