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

		void Utf8Base64Encoder::WriteBytesToCharArray(uint8_t* fromBytes, char8_t(&toChars)[Base64CycleChars], vint bytes)
		{
			switch (bytes)
			{
			case 1:
				{
					toChars[0] = Utf8Base64Codes[fromBytes[0] >> 2];
					toChars[1] = Utf8Base64Codes[fromBytes[0] % (1 << 2)];
					toChars[2] = u8'=';
					toChars[3] = u8'=';
				}
				break;
			case 2:
				{
					toChars[0] = Utf8Base64Codes[fromBytes[0] >> 2];
					toChars[1] = Utf8Base64Codes[((fromBytes[0] % (1 << 2)) << 4) + (fromBytes[1] >> 4)];
					toChars[2] = Utf8Base64Codes[fromBytes[1] % (1 << 4)];
					toChars[3] = u8'=';
				}
				break;
			case 3:
				{
					toChars[0] = Utf8Base64Codes[fromBytes[0] >> 2];
					toChars[1] = Utf8Base64Codes[((fromBytes[0] % (1 << 2)) << 4) + (fromBytes[1] >> 4)];
					toChars[2] = Utf8Base64Codes[((fromBytes[1] % (1 << 4)) << 2) + (fromBytes[2] >> 6)];
					toChars[3] = Utf8Base64Codes[fromBytes[2] % (1 << 6)];
				}
				break;
			default:
				CHECK_FAIL(L"vl::stream::Utf8Base64Encoder::WriteBytesToCharArray(uint8_t*, char8_t(&)[Base64CycleChars], vint)#Parameter bytes should be 1, 2 or 3.");
			}
		}

		bool Utf8Base64Encoder::WriteCycle(uint8_t*& reading, vint& _size)
		{
			if (_size <= 0) return false;
			vint bytes = _size < Base64CycleBytes ? _size : Base64CycleBytes;

			char8_t chars[Base64CycleChars];
			WriteBytesToCharArray(reading, chars, bytes);
			vint writtenBytes = stream->Write(chars, Base64CycleChars);
			CHECK_ERROR(writtenBytes == Base64CycleChars, L"vl::stream::Utf8Base64Encoder::WriteCycle(uint8_t*&, vint&)#The underlying stream failed to accept enough base64 characters.");

			reading += bytes;
			_size -= bytes;
			return true;
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

			if (cacheSize < Base64CycleBytes) return false;
			uint8_t* cacheReading = cache;
			return WriteCycle(cacheReading, cacheSize);
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
				WriteBytesToCharArray(cache, chars, cacheSize);
				vint writtenBytes = stream->Write(chars, Base64CycleChars);
				CHECK_ERROR(writtenBytes == Base64CycleChars, L"vl::stream::Utf8Base64Encoder::Close()#The underlying stream failed to accept enough base64 characters.");

				cacheSize = 0;
			}
			EncoderBase::Close();
		}

/***********************************************************************
Utf8Base64Decoder
***********************************************************************/

		vint Utf8Base64Decoder::ReadBytesFromCharArray(char8_t(&fromChars)[Base64CycleChars], uint8_t* toBytes)
		{
			CHECK_FAIL(L"Not Implemented!");
		}

		vint Utf8Base64Decoder::ReadCycle(uint8_t*& writing, vint& _size)
		{
			char8_t chars[Base64CycleChars];
			vint readChars = stream->Read((void*)chars, Base64CycleChars);
			if (readChars == 0) return 0;
			CHECK_ERROR(readChars == Base64CycleChars, L"vl::stream::Utf8Base64Decoder::ReadCycle(uint8_t*&, vint&)#The underlying stream failed to provide enough base64 characters.");

			vint readBytes = ReadBytesFromCharArray(chars, writing);
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
