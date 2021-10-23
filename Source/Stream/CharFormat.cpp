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
				result = WriteString(unicode, chars) * sizeof(wchar_t) - cacheSize;
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

		vint MbcsEncoder::WriteString(wchar_t* _buffer, vint chars)
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
Consumer
***********************************************************************/

		template<typename TTo>
		class WCharToUtfReader : public encoding::UtfFrom32ReaderBase<TTo, WCharToUtfReader<TTo>>
		{
			template<typename T, typename TBase>
			friend class encoding::UtfFrom32ReaderBase;

			class InternalReader : public encoding::UtfTo32ReaderBase<wchar_t, InternalReader>
			{
			public:
				const wchar_t* starting = nullptr;
				const wchar_t* ending = nullptr;
				const wchar_t* consuming = nullptr;

				wchar_t Consume()
				{
					if (consuming == ending) return 0;
					return *consuming++;
				}
			};
		protected:
			InternalReader internalReader;

			char32_t Consume()
			{
				return internalReader.Read();
			}
		public:
			WCharToUtfReader(const wchar_t* _starting, vint count)
			{
				internalReader.starting = _starting;
				internalReader.ending = _starting + count;
				internalReader.consuming = _starting;
			}

			bool HasIllegalChar() const
			{
				return encoding::UtfFrom32ReaderBase<TTo, WCharToUtfReader<TTo>>::HasIllegalChar() || internalReader.HasIllegalChar();
			}
		};

/***********************************************************************
Utf-16
***********************************************************************/

		vint Utf16Encoder::WriteString(wchar_t* _buffer, vint chars)
		{
//#if defined VCZH_WCHAR_UTF16
//			return stream->Write(_buffer, chars * sizeof(wchar_t)) / sizeof(wchar_t);
//#elif defined VCZH_WCHAR_UTF32
			WCharToUtfReader<char16_t> reader(_buffer, chars);
			vint counter = 0;
			while (char16_t c = reader.Read())
			{
				counter++;
				stream->Write(&c, sizeof(c));
			}
			return counter;
//#endif
		}

		vint Utf16Decoder::ReadString(wchar_t* _buffer, vint chars)
		{
#if defined VCZH_MSVC
			return stream->Read(_buffer, chars * sizeof(wchar_t)) / sizeof(wchar_t);
#elif defined VCZH_GCC
			wchar_t* writing = _buffer;
			while (writing - _buffer < chars)
			{
				vuint16_t utf16_1 = 0;
				vuint16_t utf16_2 = 0;

				if (stream->Read(&utf16_1, 2) != 2) break;
				if (utf16_1 < 0xD800 || utf16_1 > 0xDFFF)
				{
					*writing++ = (wchar_t)utf16_1;
				}
				else if (utf16_1 < 0xDC00)
				{
					if (stream->Read(&utf16_2, 2) != 2) break;
					if (0xDC00 <= utf16_2 && utf16_2 <= 0xDFFF)
					{
						*writing++ = (wchar_t)(utf16_1 - 0xD800) * 0x400 + (wchar_t)(utf16_2 - 0xDC00) + 0x10000;
					}
					else
					{
						break;
					}
				}
				else
				{
					break;
				}
			}
			return writing - _buffer;
#endif
		}

/***********************************************************************
Utf-16-be
***********************************************************************/

		vint Utf16BEEncoder::WriteString(wchar_t* _buffer, vint chars)
		{
#if defined VCZH_MSVC
			vint writed = 0;
			while (writed < chars)
			{
				if (stream->Write(((unsigned char*)_buffer) + 1, 1) != 1)
				{
					break;
				}
				if (stream->Write(_buffer, 1) != 1)
				{
					break;
				}
				_buffer++;
				writed++;
			}
			if (writed != chars)
			{
				Close();
			}
			return writed;
#elif defined VCZH_GCC
			vint writed = 0;
			vuint16_t utf16 = 0;
			vuint8_t* utf16buf = (vuint8_t*)&utf16;
			while (writed < chars)
			{
				wchar_t w = *_buffer++;
				if (w < 0x10000)
				{
					utf16 = (vuint16_t)w;
					if (stream->Write(&utf16buf[1], 1) != 1) break;
					if (stream->Write(&utf16buf[0], 1) != 1) break;
				}
				else if (w < 0x110000)
				{
					wchar_t inc = w - 0x10000;

					utf16 = (vuint16_t)(inc / 0x400) + 0xD800;
					if (stream->Write(&utf16buf[1], 1) != 1) break;
					if (stream->Write(&utf16buf[0], 1) != 1) break;

					utf16 = (vuint16_t)(inc % 0x400) + 0xDC00;
					if (stream->Write(&utf16buf[1], 1) != 1) break;
					if (stream->Write(&utf16buf[0], 1) != 1) break;
				}
				else
				{
					break;
				}
				writed++;
			}
			if (writed != chars)
			{
				Close();
			}
			return writed;
#endif
		}

		vint Utf16BEDecoder::ReadString(wchar_t* _buffer, vint chars)
		{
#if defined VCZH_MSVC
			chars = stream->Read(_buffer, chars * sizeof(wchar_t)) / sizeof(wchar_t);
			unsigned char* unicode = (unsigned char*)_buffer;
			for (vint i = 0; i < chars; i++)
			{
				unsigned char t = unicode[0];
				unicode[0] = unicode[1];
				unicode[1] = t;
				// +=2?
				unicode++;
			}
			return chars;
#elif defined VCZH_GCC
			wchar_t* writing = _buffer;
			while (writing - _buffer < chars)
			{
				vuint16_t utf16_1 = 0;
				vuint16_t utf16_2 = 0;
				vuint8_t* utf16buf = 0;
				vuint8_t utf16buf_temp = 0;

				if (stream->Read(&utf16_1, 2) != 2) break;

				utf16buf = (vuint8_t*)&utf16_1;
				utf16buf_temp = utf16buf[0];
				utf16buf[0] = utf16buf[1];
				utf16buf[1] = utf16buf_temp;

				if (utf16_1 < 0xD800 || utf16_1 > 0xDFFF)
				{
					*writing++ = (wchar_t)utf16_1;
				}
				else if (utf16_1 < 0xDC00)
				{
					if (stream->Read(&utf16_2, 2) != 2) break;

					utf16buf = (vuint8_t*)&utf16_2;
					utf16buf_temp = utf16buf[0];
					utf16buf[0] = utf16buf[1];
					utf16buf[1] = utf16buf_temp;

					if (0xDC00 <= utf16_2 && utf16_2 <= 0xDFFF)
					{
						*writing++ = (wchar_t)(utf16_1 - 0xD800) * 0x400 + (wchar_t)(utf16_2 - 0xDC00) + 0x10000;
					}
					else
					{
						break;
					}
				}
				else
				{
					break;
				}
			}
			return writing - _buffer;
#endif
		}

/***********************************************************************
Utf8
***********************************************************************/

		vint Utf8Encoder::WriteString(wchar_t* _buffer, vint chars)
		{
#if defined VCZH_MSVC
			vint length = WideCharToMultiByte(CP_UTF8, 0, _buffer, (int)chars, NULL, NULL, NULL, NULL);
			char* mbcs = new char[length];
			WideCharToMultiByte(CP_UTF8, 0, _buffer, (int)chars, mbcs, (int)length, NULL, NULL);
			vint result = stream->Write(mbcs, length);
			delete[] mbcs;
			if (result == length)
			{
				return chars;
			}
			else
			{
				Close();
				return 0;
			}
#elif defined VCZH_GCC
			vint writed = 0;
			while (writed < chars)
			{
				wchar_t w = *_buffer++;
				vuint8_t utf8[4];
				if (w < 0x80)
				{
					utf8[0] = (vuint8_t)w;
					if (stream->Write(utf8, 1) != 1) break;
				}
				else if (w < 0x800)
				{
					utf8[0] = 0xC0 + ((w & 0x7C0) >> 6);
					utf8[1] = 0x80 + (w & 0x3F);
					if (stream->Write(utf8, 2) != 2) break;
				}
				else if (w < 0x10000)
				{
					utf8[0] = 0xE0 + ((w & 0xF000) >> 12);
					utf8[1] = 0x80 + ((w & 0xFC0) >> 6);
					utf8[2] = 0x80 + (w & 0x3F);
					if (stream->Write(utf8, 3) != 3) break;
				}
				else if (w < 0x110000) // only accept UTF-16 range
				{
					utf8[0] = 0xF0 + ((w & 0x1C0000) >> 18);
					utf8[1] = 0x80 + ((w & 0x3F000) >> 12);
					utf8[2] = 0x80 + ((w & 0xFC0) >> 6);
					utf8[3] = 0x80 + (w & 0x3F);
					if (stream->Write(utf8, 4) != 4) break;
				}
				else
				{
					break;
				}
				writed++;
			}
			if (writed != chars)
			{
				Close();
			}
			return writed;
#endif
		}

		vint Utf8Decoder::ReadString(wchar_t* _buffer, vint chars)
		{
			vuint8_t source[4];
#if defined VCZH_MSVC
			wchar_t target[2];
#endif
			wchar_t* writing = _buffer;
			vint readed = 0;
			vint sourceCount = 0;

			while (readed < chars)
			{
#if defined VCZH_MSVC
				if (cacheAvailable)
				{
					*writing++ = cache;
					cache = 0;
					cacheAvailable = false;
				}
				else
				{
#endif
					if (stream->Read(source, 1) != 1)
					{
						break;
					}
					if ((*source & 0xF0) == 0xF0)
					{
						if (stream->Read(source + 1, 3) != 3)
						{
							break;
						}
						sourceCount = 4;
					}
					else if ((*source & 0xE0) == 0xE0)
					{
						if (stream->Read(source + 1, 2) != 2)
						{
							break;
						}
						sourceCount = 3;
					}
					else if ((*source & 0xC0) == 0xC0)
					{
						if (stream->Read(source + 1, 1) != 1)
						{
							break;
						}
						sourceCount = 2;
					}
					else
					{
						sourceCount = 1;
					}
#if defined VCZH_MSVC	
					int targetCount = MultiByteToWideChar(CP_UTF8, 0, (char*)source, (int)sourceCount, target, 2);
					if (targetCount == 1)
					{
						*writing++ = target[0];
					}
					else if (targetCount == 2)
					{
						*writing++ = target[0];
						cache = target[1];
						cacheAvailable = true;
					}
					else
					{
						break;
					}
				}
#elif defined VCZH_GCC
					if (sourceCount == 1)
					{
						*writing++ = (wchar_t)source[0];
					}
					else if (sourceCount == 2)
					{
						*writing++ = (((wchar_t)source[0] & 0x1F) << 6) + ((wchar_t)source[1] & 0x3F);
					}
					else if (sourceCount == 3)
					{
						*writing++ = (((wchar_t)source[0] & 0xF) << 12) + (((wchar_t)source[1] & 0x3F) << 6) + ((wchar_t)source[2] & 0x3F);
					}
					else if (sourceCount == 4)
					{
						*writing++ = (((wchar_t)source[0] & 0x7) << 18) + (((wchar_t)source[1] & 0x3F) << 12) + (((wchar_t)source[2] & 0x3F) << 6) + ((wchar_t)source[3] & 0x3F);
					}
					else
					{
						break;
					}
#endif
					readed++;
			}
				return readed;
		}
	}
}
