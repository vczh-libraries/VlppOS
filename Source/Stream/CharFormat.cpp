/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "CharFormat.h"
#if defined VCZH_MSVC
#include <windows.h>
#elif defined VCZH_GCC
#include <string.h>
#endif

namespace vl
{
	namespace stream
	{

/***********************************************************************
CharEncoder
***********************************************************************/

		void CharEncoder::Setup(IStream* _stream)
		{
			stream = _stream;
		}

		void CharEncoder::Close()
		{
		}

		vint CharEncoder::Write(void* _buffer, vint _size)
		{
			const vint all = cacheSize + _size;
			const vint chars = all / sizeof(wchar_t);
			const vint bytes = chars * sizeof(wchar_t);
			wchar_t* unicode = 0;
			bool needToFree = false;
			vint result = 0;

			if (chars)
			{
				if (cacheSize > 0)
				{
					unicode = new wchar_t[chars];
					memcpy(unicode, cacheBuffer, cacheSize);
					memcpy(((vuint8_t*)unicode) + cacheSize, _buffer, bytes - cacheSize);
					needToFree = true;
				}
				else
				{
					unicode = (wchar_t*)_buffer;
				}
				result = WriteString(unicode, chars, needToFree) * sizeof(wchar_t) - cacheSize;
				cacheSize = 0;
			}

			if (needToFree)
			{
				delete[] unicode;
			}
			if (all - bytes > 0)
			{
				cacheSize = all - bytes;
				memcpy(cacheBuffer, (vuint8_t*)_buffer + _size - cacheSize, cacheSize);
				result += cacheSize;
			}
			return result;
		}

/***********************************************************************
CharDecoder
***********************************************************************/

		void CharDecoder::Setup(IStream* _stream)
		{
			stream=_stream;
		}

		void CharDecoder::Close()
		{
		}

		vint CharDecoder::Read(void* _buffer, vint _size)
		{
			vuint8_t* unicode = (vuint8_t*)_buffer;
			vint result = 0;
			{
				vint index = 0;
				while (cacheSize > 0 && _size > 0)
				{
					*unicode++ = cacheBuffer[index]++;
					cacheSize--;
					_size--;
					result++;
				}
			}

			const vint chars = _size / sizeof(wchar_t);
			vint bytes = ReadString((wchar_t*)unicode, chars) * sizeof(wchar_t);
			result += bytes;
			_size -= bytes;
			unicode += bytes;

			if (_size > 0)
			{
				wchar_t c;
				if (ReadString(&c, 1) == 1)
				{
					cacheSize = sizeof(wchar_t) - _size;
					memcpy(unicode, &c, _size);
					memcpy(cacheBuffer, (vuint8_t*)&c + _size, cacheSize);
					result += _size;
				}
			}
			return result;
		}

/***********************************************************************
Mbcs
***********************************************************************/

		vint MbcsEncoder::WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate)
		{
#if defined VCZH_MSVC
			vint length = WideCharToMultiByte(CP_THREAD_ACP, 0, _buffer, (int)chars, NULL, NULL, NULL, NULL);
			char* mbcs = new char[length];
			WideCharToMultiByte(CP_THREAD_ACP, 0, _buffer, (int)chars, mbcs, (int)length, NULL, NULL);
			vint result = stream->Write(mbcs, length);
			delete[] mbcs;
#elif defined VCZH_GCC
			WString w(_buffer, chars);
			AString a = wtoa(w);
			vint length = a.Length();
			vint result = stream->Write((void*)a.Buffer(), length);
#endif
			if (result == length)
			{
				return chars;
			}
			else
			{
				Close();
				return 0;
			}
		}

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
#if defined VCZH_MSVC
				if (IsDBCSLeadByte(*reading))
#elif defined VCZH_GCC
				if ((vint8_t)*reading < 0)
#endif
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
#if defined VCZH_MSVC
			MultiByteToWideChar(CP_THREAD_ACP, 0, source, (int)(reading - source), _buffer, (int)chars);
#elif defined VCZH_GCC
			AString a(source, (vint)(reading - source));
			WString w = atow(a);
			memcpy(_buffer, w.Buffer(), readed * sizeof(wchar_t));
#endif
			delete[] source;
			return readed;
		}

/***********************************************************************
Utf-16
***********************************************************************/

		vint Utf16Encoder::WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate)
		{
#if defined VCZH_WCHAR_UTF16
			return stream->Write(_buffer, chars * sizeof(wchar_t));
#elif defined VCZH_WCHAR_UTF32
			WCharToUtfReader<char16_t> reader(_buffer, chars);
			vint counter = 0;
			while (char16_t c = reader.Read())
			{
				counter += stream->Write(&c, sizeof(c));
			}
			if (reader.HasIllegalChar())
			{
				Close();
				return 0;
			}
			return counter;
#endif
		}

		vint Utf16Decoder::ReadString(wchar_t* _buffer, vint chars)
		{
#if defined VCZH_WCHAR_UTF16
			return stream->Read(_buffer, chars * sizeof(wchar_t));
#elif defined VCZH_WCHAR_UTF32
			reader.Setup(stream);
			vint counter = 0;
			for (vint i = 0; i < chars; i++)
			{
				wchar_t c = reader.Read();
				if (!c) break;
				_buffer[i] = c;
				counter++;
			}
			return counter * sizeof(wchar_t);
#endif
		}

/***********************************************************************
Utf-16-be
***********************************************************************/

		vint Utf16BEEncoder::WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate)
		{
#if defined VCZH_WCHAR_UTF16
			if (freeToUpdate)
			{
				SwapBytesForUtf16BE(_buffer, chars);
				vint counter = stream->Write(_buffer, sizeof(wchar_t) * chars);
				SwapBytesForUtf16BE(_buffer, chars);
				return counter;
			}
			else
			{
				vint counter = 0;
				for (vint i = 0; i < chars; i++)
				{
					wchar_t c = _buffer[i];
					SwapByteForUtf16BE(c);
					counter += stream->Write(&c, sizeof(c));
				}
				return counter;
			}
#elif defined VCZH_WCHAR_UTF32
			WCharToUtfReader<char16_t> reader(_buffer, chars);
			vint counter = 0;
			while (char16_t c = reader.Read())
			{
				SwapByteForUtf16BE(c);
				counter += stream->Write(&c, sizeof(c));
			}
			if (reader.HasIllegalChar())
			{
				Close();
				return 0;
			}
			return counter;
#endif
		}

		vint Utf16BEDecoder::ReadString(wchar_t* _buffer, vint chars)
		{
#if defined VCZH_WCHAR_UTF16
			vint size = stream->Read(_buffer, chars * sizeof(wchar_t));
			SwapBytesForUtf16BE(_buffer, size / sizeof(wchar_t));
			return size;
#elif defined VCZH_WCHAR_UTF32
			reader.Setup(stream);
			vint counter = 0;
			for (vint i = 0; i < chars; i++)
			{
				wchar_t c = reader.Read();
				if (!c) break;
				_buffer[i] = c;
				counter++;
			}
			return counter * sizeof(wchar_t);
#endif
		}

/***********************************************************************
Utf8
***********************************************************************/

		vint Utf8Encoder::WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate)
		{
#if defined VCZH_MSVC
			vint length = WideCharToMultiByte(CP_UTF8, 0, _buffer, (int)chars, NULL, NULL, NULL, NULL);
			char* mbcs = new char[length];
			WideCharToMultiByte(CP_UTF8, 0, _buffer, (int)chars, mbcs, (int)length, NULL, NULL);
			vint result = stream->Write(mbcs, length);
			delete[] mbcs;
			if (result == length)
			{
				return result;
			}
			else
			{
				Close();
				return 0;
			}
#elif defined VCZH_GCC
			WCharToUtfReader<char8_t> reader(_buffer, chars);
			vint counter = 0;
			while (char8_t c = reader.Read())
			{
				counter += stream->Write(&c, sizeof(c));
			}
			if (reader.HasIllegalChar())
			{
				Close();
				return 0;
			}
			return counter;
#endif
		}

		vint Utf8Decoder::ReadString(wchar_t* _buffer, vint chars)
		{
			reader.Setup(stream);
			vint counter = 0;
			for (vint i = 0; i < chars; i++)
			{
				wchar_t c = reader.Read();
				if (!c) break;
				_buffer[i] = c;
				counter++;
			}
			return counter * sizeof(wchar_t);
		}
	}
}
