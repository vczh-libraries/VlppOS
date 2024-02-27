/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Base64Encoding.h"

namespace vl
{
	namespace stream
	{
		const char8_t Utf8Base64Codes[] = u8"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/***********************************************************************
Utf8Base64Encoder
***********************************************************************/

		void Utf8Base64Encoder::WriteBytes(uint8_t* fromBytes, char8_t(&toChars)[4], vint bytes)
		{
			CHECK_FAIL(L"Not Implemented!");
		}

		vint Utf8Base64Encoder::Write(void* _buffer, vint _size)
		{
			CHECK_FAIL(L"Not Implemented!");
		}

		void Utf8Base64Encoder::Close()
		{
			CHECK_FAIL(L"Not Implemented!");
		}

/***********************************************************************
Utf8Base64EDecoder
***********************************************************************/

		vint Utf8Base64EDecoder::ReadBytes(char8_t(&fromChars)[4], uint8_t* toBytes)
		{
			CHECK_FAIL(L"Not Implemented!");
		}

		vint Utf8Base64EDecoder::Read(void* _buffer, vint _size)
		{
			CHECK_FAIL(L"Not Implemented!");
		}

		void Utf8Base64EDecoder::Close()
		{
			CHECK_FAIL(L"Not Implemented!");
		}
	}
}
