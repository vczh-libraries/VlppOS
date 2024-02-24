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
		bool IsMbcsLeadByte(char c)
		{
			return IsDBCSLeadByte(c);
		}

		void MbcsToWChar(wchar_t* wideBuffer, vint wideChars, vint wideReaded, char* mbcsBuffer, vint mbcsChars)
		{
			MultiByteToWideChar(CP_THREAD_ACP, 0, mbcsBuffer, (int)mbcsChars, wideBuffer, (int)wideChars);
		}

/***********************************************************************
Mbcs
***********************************************************************/

		vint MbcsEncoder::WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate)
		{
			vint length = WideCharToMultiByte(CP_THREAD_ACP, 0, _buffer, (int)chars, NULL, NULL, NULL, NULL);
			char* mbcs = new char[length];
			WideCharToMultiByte(CP_THREAD_ACP, 0, _buffer, (int)chars, mbcs, (int)length, NULL, NULL);
			vint result = stream->Write(mbcs, length);
			delete[] mbcs;

			if (result != length)
			{
				Close();
				return 0;
			}
			return chars;
		}

/***********************************************************************
Utf8
***********************************************************************/

		vint Utf8Encoder::WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate)
		{
			vint length = WideCharToMultiByte(CP_UTF8, 0, _buffer, (int)chars, NULL, NULL, NULL, NULL);
			char* mbcs = new char[length];
			WideCharToMultiByte(CP_UTF8, 0, _buffer, (int)chars, mbcs, (int)length, NULL, NULL);
			vint result = stream->Write(mbcs, length);
			delete[] mbcs;
			if (result != length)
			{
				Close();
				return 0;
			}
			return chars;
		}
	}
}
