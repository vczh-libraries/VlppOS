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

		vint Utf8Base64Decoder::ReadBytes(char8_t(&fromChars)[4], uint8_t* toBytes)
		{
			CHECK_FAIL(L"Not Implemented!");
		}

		vint Utf8Base64Decoder::ReadCycle(uint8_t*& writing, vint& _size)
		{
			char8_t chars[4];
			vint readChars = stream->Read((void*)chars, 4);
			if (readChars == 0) return 0;
			CHECK_ERROR(readChars == 4, L"vl::stream::Utf8Base64EDecoder::ReadCycle(uint8_t*&, vint&)#The underlying stream failed to provide enough base64 characters.");

			vint readBytes = ReadBytes(chars, writing);
			writing += readBytes;
			_size -= readBytes;
			return readBytes;
		}

		void Utf8Base64Decoder::ReadCache(uint8_t*& writing, vint& _size)
		{
			if (cacheSize > 0)
			{
				vint copiedBytes = cacheSize;
				if (copiedBytes > _size) copiedBytes = _size;
				memcpy(writing, cache, copiedBytes);
				writing += copiedBytes;
				_size -= copiedBytes;
				cacheSize -= copiedBytes;
			}
		}

		vint Utf8Base64Decoder::Read(void* _buffer, vint _size)
		{
			uint8_t* writing = (uint8_t*)_buffer;

			// write cache to buffer if any
			ReadCache(writing, _size);

			// run Base64 decoding
			while (_size >= 3)
			{
				if (ReadCycle(writing, _size) == 0) goto FINISHED_READING;
			}

			// run the last Base64 decoding cycle and write a prefix to buffer
			if (_size > 0)
			{
				uint8_t* cacheWriting = cache;
				vint temp = 0;
				if ((cacheSize = ReadCycle(cacheWriting, temp)) == 0) goto FINISHED_READING;
				ReadCache(writing, _size);
			}
		FINISHED_READING:
			return writing - (uint8_t*)_buffer;
		}
	}
}
