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
