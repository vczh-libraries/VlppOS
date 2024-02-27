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

		void Utf8Base64Encoder::WriteBytes(uint8_t* fromBytes, char8_t(&toChars)[Base64CycleChars], vint bytes)
		{
			CHECK_FAIL(L"Not Implemented!");
		}

		bool Utf8Base64Encoder::WriteCycle(uint8_t*& reading, vint& _size)
		{
		}

		bool Utf8Base64Encoder::WriteCache(uint8_t*& reading, vint& _size)
		{
			if (cacheSize > 0 || _size < Base64CycleBytes)
			{
				vint copiedBytes = Base64CycleBytes - cacheSize;
				if (copiedBytes > _size) copiedBytes = _size;
				if (copiedBytes > 0)
				{
					memcpy(cache + cacheSize, reading, copiedBytes);
					reading += copiedBytes;
					_size -= copiedBytes;
					cacheSize += copiedBytes;
				}
			}

			if (cacheSize == Base64CycleBytes)
			{
				uint8_t* cacheReading = cache;
				return WriteCycle(cacheReading, cacheSize);
			}
			return true;
		}

		vint Utf8Base64Encoder::Write(void* _buffer, vint _size)
		{
			uint8_t* reading = (uint8_t*)_buffer;

			// flush cache if any
			if (!WriteCache(reading, _size)) goto FINISHED_WRITING;

			// run Base64 encoding
			while (_size >= Base64CycleBytes)
			{
				if (!WriteCycle(reading, _size)) goto FINISHED_WRITING;
			}

			// run the last Base64 encoding cycle and wrote a postfix to cache
			WriteCache(reading, _size);

		FINISHED_WRITING:
			return reading - (uint8_t*)_buffer;
		}

		void Utf8Base64Encoder::Close()
		{
			if (cacheSize > 0)
			{
				char8_t chars[Base64CycleChars];
				WriteBytes(cache, chars, cacheSize);
				vint writtenBytes = stream->Write(chars, Base64CycleChars);
				CHECK_ERROR(writtenBytes == Base64CycleChars, L"vl::stream::Utf8Base64Encoder::Close()#The underlying stream failed to accept enough base64 characters.");

				cacheSize = 0;
			}
			EncoderBase::Close();
		}

/***********************************************************************
Utf8Base64Decoder
***********************************************************************/

		vint Utf8Base64Decoder::ReadBytes(char8_t(&fromChars)[Base64CycleChars], uint8_t* toBytes)
		{
			CHECK_FAIL(L"Not Implemented!");
		}

		vint Utf8Base64Decoder::ReadCycle(uint8_t*& writing, vint& _size)
		{
			char8_t chars[Base64CycleChars];
			vint readChars = stream->Read((void*)chars, Base64CycleChars);
			if (readChars == 0) return 0;
			CHECK_ERROR(readChars == Base64CycleChars, L"vl::stream::Utf8Base64Decoder::ReadCycle(uint8_t*&, vint&)#The underlying stream failed to provide enough base64 characters.");

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
				if (copiedBytes > 0)
				{
					memcpy(writing, cache, copiedBytes);
					writing += copiedBytes;
					_size -= copiedBytes;
					cacheSize -= copiedBytes;
				}
			}
		}

		vint Utf8Base64Decoder::Read(void* _buffer, vint _size)
		{
			uint8_t* writing = (uint8_t*)_buffer;

			// write cache to buffer if any
			ReadCache(writing, _size);

			// run Base64 decoding
			while (_size >= Base64CycleBytes)
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
