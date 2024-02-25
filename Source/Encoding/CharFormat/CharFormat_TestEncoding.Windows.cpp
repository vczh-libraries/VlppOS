/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "CharFormat.h"
#include <windows.h>

#ifndef VCZH_MSVC
static_assert(false, "Do not build this file for non-Windows applications.");
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

		template<vint Count>
		bool GetEncodingResult(int(&tests)[Count], bool(&results)[Count], int test)
		{
			for (vint i = 0; i < Count; i++)
			{
				if (tests[i] & test)
				{
					if (results[i]) return true;
				}
			}
			return false;
		}
		
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
			int tests[] =
			{
				IS_TEXT_UNICODE_REVERSE_ASCII16,
				IS_TEXT_UNICODE_REVERSE_STATISTICS,
				IS_TEXT_UNICODE_REVERSE_CONTROLS,

				IS_TEXT_UNICODE_ASCII16,
				IS_TEXT_UNICODE_STATISTICS,
				IS_TEXT_UNICODE_CONTROLS,

				IS_TEXT_UNICODE_ILLEGAL_CHARS,
				IS_TEXT_UNICODE_ODD_LENGTH,
				IS_TEXT_UNICODE_NULL_BYTES,
			};

			const vint TestCount = sizeof(tests) / sizeof(*tests);
			bool results[TestCount];
			for (vint i = 0; i < TestCount; i++)
			{
				int test = tests[i];
				results[i] = IsTextUnicode(buffer, (int)size, &test) != 0;
			}

			if (size % 2 == 0
				&& !GetEncodingResult(tests, results, IS_TEXT_UNICODE_REVERSE_ASCII16)
				&& !GetEncodingResult(tests, results, IS_TEXT_UNICODE_REVERSE_STATISTICS)
				&& !GetEncodingResult(tests, results, IS_TEXT_UNICODE_REVERSE_CONTROLS)
				)
			{
				for (vint i = 0; i < size; i += 2)
				{
					unsigned char c = buffer[i];
					buffer[i] = buffer[i + 1];
					buffer[i + 1] = c;
				}
				// 3 = (count of reverse group) = (count of unicode group)
				for (vint i = 0; i < 3; i++)
				{
					int test = tests[i + 3];
					results[i] = IsTextUnicode(buffer, (int)size, &test) != 0;
				}
				for (vint i = 0; i < size; i += 2)
				{
					unsigned char c = buffer[i];
					buffer[i] = buffer[i + 1];
					buffer[i + 1] = c;
				}
			}

			if (GetEncodingResult(tests, results, IS_TEXT_UNICODE_NOT_UNICODE_MASK))
			{
				if (GetEncodingResult(tests, results, IS_TEXT_UNICODE_NOT_ASCII_MASK))
				{
					encoding = BomEncoder::Utf8;
				}
				else if (roughUtf8 || !roughMbcs)
				{
					encoding = BomEncoder::Utf8;
				}
			}
			else if (GetEncodingResult(tests, results, IS_TEXT_UNICODE_ASCII16))
			{
				encoding = BomEncoder::Utf16;
			}
			else if (GetEncodingResult(tests, results, IS_TEXT_UNICODE_REVERSE_ASCII16))
			{
				encoding = BomEncoder::Utf16BE;
			}
			else if (GetEncodingResult(tests, results, IS_TEXT_UNICODE_CONTROLS))
			{
				encoding = BomEncoder::Utf16;
			}
			else if (GetEncodingResult(tests, results, IS_TEXT_UNICODE_REVERSE_CONTROLS))
			{
				encoding = BomEncoder::Utf16BE;
			}
			else
			{
				if (!roughUtf8)
				{
					if (GetEncodingResult(tests, results, IS_TEXT_UNICODE_STATISTICS))
					{
						encoding = BomEncoder::Utf16;
					}
					else if (GetEncodingResult(tests, results, IS_TEXT_UNICODE_STATISTICS))
					{
						encoding = BomEncoder::Utf16BE;
					}
				}
				else if (GetEncodingResult(tests, results, IS_TEXT_UNICODE_NOT_UNICODE_MASK))
				{
					encoding = BomEncoder::Utf8;
				}
				else if (roughUtf8 || !roughMbcs)
				{
					encoding = BomEncoder::Utf8;
				}
			}
		}
	}
}
