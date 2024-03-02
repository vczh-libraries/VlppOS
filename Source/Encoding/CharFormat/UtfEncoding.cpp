/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "UtfEncoding.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
UtfGeneralEncoder
***********************************************************************/

		template<typename TNative, typename TExpect>
		vint UtfGeneralEncoder<TNative, TExpect>::Write(void* _buffer, vint _size)
		{
			// prepare a buffer for input
			vint availableChars = (cacheSize + _size) / sizeof(TExpect);
			vint availableBytes = availableChars * sizeof(TExpect);
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

			// write the buffer
			if (availableChars > 0)
			{
				TStringRangeReader reader((TExpect*)unicode, availableChars);
				while (TNative c = reader.Read())
				{
					vint written = stream->Write(&c, sizeof(c));
					if (written != sizeof(c))
					{
						Close();
						CHECK_FAIL(L"UtfGeneralEncoder<T>::Write(void*, vint)#Failed to write a complete string.");
					}
				}
				auto cluster = reader.SourceCluster();
				availableChars = cluster.index + cluster.size;
				availableBytes = availableChars * sizeof(TExpect);
			}

			// cache the remaining
			cacheSize = cacheSize + _size - availableBytes;
			if (cacheSize > 0)
			{
				CHECK_ERROR(cacheSize <= sizeof(cacheBuffer), L"UtfGeneralEncoder<T>::Write(void*, vint)#Unwritten text is too large to cache.");
				memcpy(cacheBuffer, unicode + availableBytes, cacheSize);
			}

			if (needToFree) delete[] unicode;
			return _size;
		}

/***********************************************************************
UtfGeneralDecoder
***********************************************************************/

		template<typename TNative, typename TExpect>
		void UtfGeneralDecoder<TNative, TExpect>::Setup(IStream* _stream)
		{
			DecoderBase::Setup(_stream);
			reader.Setup(_stream);
		}

		template<typename TNative, typename TExpect>
		vint UtfGeneralDecoder<TNative, TExpect>::Read(void* _buffer, vint _size)
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
			while (_size >= sizeof(TExpect))
			{
				vint availableChars = _size / sizeof(TExpect);
				vint readBytes = 0;
				for (vint i = 0; i < availableChars; i++)
				{
					TExpect c = reader.Read();
					if (!c) break;
					*((TExpect*)writing) = c;
					writing += sizeof(TExpect);
					readBytes += sizeof(TExpect);
				}
				if (readBytes == 0) break;
				filledBytes += readBytes;
				_size -= readBytes;
			}

			// cache the remaining TExpect
			if (_size < sizeof(TExpect))
			{
				if (TExpect c = reader.Read())
				{
					vuint8_t* reading = (vuint8_t*)&c;
					memcpy(writing, reading, _size);
					filledBytes += _size;
					cacheSize = sizeof(TExpect) - _size;
					memcpy(cacheBuffer, reading + _size, cacheSize);
				}
			}

			return filledBytes;
		}

/***********************************************************************
UtfGeneralEncoder<T, T>
***********************************************************************/

		template<typename T>
		vint UtfGeneralEncoder<T, T>::Write(void* _buffer, vint _size)
		{
			return stream->Write(_buffer, _size);
		}

/***********************************************************************
UtfGeneralDecoder<T, T>
***********************************************************************/

		template<typename T>
		vint UtfGeneralDecoder<T, T>::Read(void* _buffer, vint _size)
		{
			return stream->Read(_buffer, _size);
		}

/***********************************************************************
Unicode General (extern templates)
***********************************************************************/

		template class UtfGeneralEncoder<wchar_t, wchar_t>;
		template class UtfGeneralEncoder<wchar_t, char8_t>;
		template class UtfGeneralEncoder<wchar_t, char16_t>;
		template class UtfGeneralEncoder<wchar_t, char16be_t>;
		template class UtfGeneralEncoder<wchar_t, char32_t>;

		template class UtfGeneralEncoder<char8_t, wchar_t>;
		template class UtfGeneralEncoder<char8_t, char8_t>;
		template class UtfGeneralEncoder<char8_t, char16_t>;
		template class UtfGeneralEncoder<char8_t, char16be_t>;
		template class UtfGeneralEncoder<char8_t, char32_t>;

		template class UtfGeneralEncoder<char16_t, wchar_t>;
		template class UtfGeneralEncoder<char16_t, char8_t>;
		template class UtfGeneralEncoder<char16_t, char16_t>;
		template class UtfGeneralEncoder<char16_t, char16be_t>;
		template class UtfGeneralEncoder<char16_t, char32_t>;

		template class UtfGeneralEncoder<char16be_t, wchar_t>;
		template class UtfGeneralEncoder<char16be_t, char8_t>;
		template class UtfGeneralEncoder<char16be_t, char16_t>;
		template class UtfGeneralEncoder<char16be_t, char16be_t>;
		template class UtfGeneralEncoder<char16be_t, char32_t>;

		template class UtfGeneralEncoder<char32_t, wchar_t>;
		template class UtfGeneralEncoder<char32_t, char8_t>;
		template class UtfGeneralEncoder<char32_t, char16_t>;
		template class UtfGeneralEncoder<char32_t, char16be_t>;
		template class UtfGeneralEncoder<char32_t, char32_t>;

		template class UtfGeneralDecoder<wchar_t, wchar_t>;
		template class UtfGeneralDecoder<wchar_t, char8_t>;
		template class UtfGeneralDecoder<wchar_t, char16_t>;
		template class UtfGeneralDecoder<wchar_t, char16be_t>;
		template class UtfGeneralDecoder<wchar_t, char32_t>;

		template class UtfGeneralDecoder<char8_t, wchar_t>;
		template class UtfGeneralDecoder<char8_t, char8_t>;
		template class UtfGeneralDecoder<char8_t, char16_t>;
		template class UtfGeneralDecoder<char8_t, char16be_t>;
		template class UtfGeneralDecoder<char8_t, char32_t>;

		template class UtfGeneralDecoder<char16_t, wchar_t>;
		template class UtfGeneralDecoder<char16_t, char8_t>;
		template class UtfGeneralDecoder<char16_t, char16_t>;
		template class UtfGeneralDecoder<char16_t, char16be_t>;
		template class UtfGeneralDecoder<char16_t, char32_t>;

		template class UtfGeneralDecoder<char16be_t, wchar_t>;
		template class UtfGeneralDecoder<char16be_t, char8_t>;
		template class UtfGeneralDecoder<char16be_t, char16_t>;
		template class UtfGeneralDecoder<char16be_t, char16be_t>;
		template class UtfGeneralDecoder<char16be_t, char32_t>;

		template class UtfGeneralDecoder<char32_t, wchar_t>;
		template class UtfGeneralDecoder<char32_t, char8_t>;
		template class UtfGeneralDecoder<char32_t, char16_t>;
		template class UtfGeneralDecoder<char32_t, char16be_t>;
		template class UtfGeneralDecoder<char32_t, char32_t>;
	}
}
