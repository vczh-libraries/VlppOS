/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "CharFormat.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
MbcsDecoder::ReadString
***********************************************************************/

		extern bool IsMbcsLeadByte(char c);
		extern void MbcsToWChar(wchar_t* wideBuffer, vint wideChars, vint wideReaded, char* mbcsBuffer, vint mbcsChars);

		vint MbcsDecoder::ReadString(wchar_t* _buffer, vint chars)
		{
			char* source = new char[chars * 2];
			char* reading = source;
			vint readed = 0;
			while (readed < chars)
			{
				if (stream->Read(reading, 1) != 1)
				{
					break;
				}
				if (IsMbcsLeadByte(*reading))
				{
					if (stream->Read(reading + 1, 1) != 1)
					{
						break;
					}
					reading += 2;
				}
				else
				{
					reading++;
				}
				readed++;
			}

			MbcsToWChar(_buffer, chars, readed, source, (vint)(reading - source));
			delete[] source;
			return readed;
		}

/***********************************************************************
MbcsEncoder
***********************************************************************/

		vint MbcsEncoder::Write(void* _buffer, vint _size)
		{
			// prepare a buffer for input
			vint availableChars = (cacheSize + _size) / sizeof(wchar_t);
			vint availableBytes = availableChars * sizeof(wchar_t);
			bool needToFree = false;
			vuint8_t* unicode = nullptr;
			if (cacheSize > 0)
			{
				unicode = new vuint8_t[cacheSize + _size];
				memcpy(unicode, cacheBuffer, cacheSize);
				memcpy((vuint8_t*)unicode + cacheSize, _buffer, _size);
				needToFree = true;
			}
			else
			{
				unicode = (vuint8_t*)_buffer;
			}

#if defined VCZH_WCHAR_UTF16
			if (availableChars > 0)
			{
				// a surrogate pair must be written as a whole thing
				vuint16_t c = (vuint16_t)((wchar_t*)unicode)[availableChars - 1];
				if ((c & 0xFC00U) == 0xD800U)
				{
					availableChars -= 1;
					availableBytes -= sizeof(wchar_t);
				}
			}
#endif

			// write the buffer
			if (availableChars > 0)
			{
				vint written = WriteString((wchar_t*)unicode, availableChars) * sizeof(wchar_t);
				CHECK_ERROR(written == availableBytes, L"MbcsEncoder::Write(void*, vint)#Failed to write a complete string.");
			}

			// cache the remaining
			cacheSize = cacheSize + _size - availableBytes;
			if (cacheSize > 0)
			{
				CHECK_ERROR(cacheSize <= sizeof(char32_t), L"MbcsEncoder::Write(void*, vint)#Unwritten text is too large to cache.");
				memcpy(cacheBuffer, unicode + availableBytes, cacheSize);
			}

			if (needToFree) delete[] unicode;
			return _size;
		}

/***********************************************************************
MbcsDecoder::WriteString
***********************************************************************/

		// implemented in platform dependent files

/***********************************************************************
MbcsDecoder
***********************************************************************/

		vint MbcsDecoder::Read(void* _buffer, vint _size)
		{
			vuint8_t* writing = (vuint8_t*)_buffer;
			vint filledBytes = 0;

			// feed the cache first
			if (cacheSize > 0)
			{
				filledBytes = cacheSize < _size ? cacheSize : _size;
				memcpy(writing, cacheBuffer, cacheSize);
				_size -= filledBytes;
				writing += filledBytes;

				// adjust the cache if it is not fully consumed
				cacheSize -= filledBytes;
				if (cacheSize > 0)
				{
					memcpy(cacheBuffer, cacheBuffer + filledBytes, cacheSize);
				}

				if (_size == 0)
				{
					return filledBytes;
				}
			}

			// fill the buffer as many as possible
			while (_size >= sizeof(wchar_t))
			{
				vint availableChars = _size / sizeof(wchar_t);
				vint readBytes = ReadString((wchar_t*)writing, availableChars) * sizeof(wchar_t);
				if (readBytes == 0) break;
				filledBytes += readBytes;
				_size -= readBytes;
				writing += readBytes;
			}

			// cache the remaining wchar_t
			if (_size < sizeof(wchar_t))
			{
				wchar_t c;
				vint readChars = ReadString(&c, 1) * sizeof(wchar_t);
				if (readChars == sizeof(wchar_t))
				{
					vuint8_t* reading = (vuint8_t*)&c;
					memcpy(writing, reading, _size);
					filledBytes += _size;
					cacheSize = sizeof(wchar_t) - _size;
					memcpy(cacheBuffer, reading + _size, cacheSize);
				}
			}

			return filledBytes;
		}
	}
}
