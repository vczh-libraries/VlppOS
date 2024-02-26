/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "UtfEncoding.h"

namespace vl
{
	namespace stream
	{
		using namespace vl::encoding;

/***********************************************************************
UtfGeneralEncoder
***********************************************************************/

		template<typename T>
		vint UtfGeneralEncoder<T>::WriteString(wchar_t* _buffer, vint chars)
		{
			UtfStringRangeToStringRangeReader<wchar_t, T> reader(_buffer, chars);
			while (T c = reader.Read())
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

		template<typename T>
		vint UtfGeneralEncoder<T>::Write(void* _buffer, vint _size)
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
				CHECK_ERROR(written == availableBytes, L"UtfGeneralEncoder<T>::Write(void*, vint)#Failed to write a complete string.");
			}

			// cache the remaining
			cacheSize = cacheSize + _size - availableBytes;
			if (cacheSize > 0)
			{
				CHECK_ERROR(cacheSize <= sizeof(char32_t), L"UtfGeneralEncoder<T>::Write(void*, vint)#Unwritten text is too large to cache.");
				memcpy(cacheBuffer, unicode + availableBytes, cacheSize);
			}

			if (needToFree) delete[] unicode;
			return _size;
		}

		template class UtfGeneralEncoder<char8_t>;
		template class UtfGeneralEncoder<char16_t>;
		template class UtfGeneralEncoder<char16be_t>;
		template class UtfGeneralEncoder<char32_t>;

/***********************************************************************
UtfGeneralDecoder
***********************************************************************/

		template<typename T>
		vint UtfGeneralDecoder<T>::ReadString(wchar_t* _buffer, vint chars)
		{
			vint counter = 0;
			for (vint i = 0; i < chars; i++)
			{
				wchar_t c = reader.Read();
				if (!c) break;
				_buffer[i] = c;
				counter++;
			}
			return counter;
		}

		template<typename T>
		void UtfGeneralDecoder<T>::Setup(IStream* _stream)
		{
			CharDecoderBase::Setup(_stream);
			reader.Setup(_stream);
		}

		template<typename T>
		vint UtfGeneralDecoder<T>::Read(void* _buffer, vint _size)
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

		template class UtfGeneralDecoder<char8_t>;
		template class UtfGeneralDecoder<char16_t>;
		template class UtfGeneralDecoder<char16be_t>;
		template class UtfGeneralDecoder<char32_t>;

/***********************************************************************
UtfGeneralEncoder<wchar_t>
***********************************************************************/

		vint UtfGeneralEncoder<wchar_t>::Write(void* _buffer, vint _size)
		{
			return stream->Write(_buffer, _size);
		}

/***********************************************************************
UtfGeneralDecoder<wchar_t>
***********************************************************************/

		vint UtfGeneralDecoder<wchar_t>::Read(void* _buffer, vint _size)
		{
			return stream->Read(_buffer, _size);
		}
	}
}
