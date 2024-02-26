/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include <string.h>
#include "Accessor.h"

namespace vl
{
	namespace stream
	{
		using namespace collections;

		template<typename T>
		struct VCRLF_ {};

		template<> struct VCRLF_<wchar_t> { static constexpr const wchar_t* Value = L"\r\n"; };
		template<> struct VCRLF_<char8_t> { static constexpr const char8_t* Value = u8"\r\n"; };
		template<> struct VCRLF_<char16_t> { static constexpr const char16_t* Value = u"\r\n"; };
		template<> struct VCRLF_<char32_t> { static constexpr const char32_t* Value = U"\r\n"; };

		template<typename T>
		constexpr const T* VCRLF = VCRLF_<T>::Value;

		template<typename T>
		struct VEMPTYSTR_ {};

		template<> struct VEMPTYSTR_<wchar_t> { static constexpr const wchar_t* Value = L""; };
		template<> struct VEMPTYSTR_<char8_t> { static constexpr const char8_t* Value = u8""; };
		template<> struct VEMPTYSTR_<char16_t> { static constexpr const char16_t* Value = u""; };
		template<> struct VEMPTYSTR_<char32_t> { static constexpr const char32_t* Value = U""; };

		template<typename T>
		constexpr const T* VEMPTYSTR = VEMPTYSTR_<T>::Value;

/***********************************************************************
TextReader_<T>
***********************************************************************/

		template<typename T>
		ObjectString<T> TextReader_<T>::ReadString(vint length)
		{
			T* buffer = new T[length + 1];
			vint i = 0;
			for (; i < length; i++)
			{
				if ((buffer[i] = ReadChar()) == 0)
				{
					break;
				}
			}
			buffer[i] = 0;
			ObjectString<T> result(buffer);
			delete[] buffer;
			return result;
		}

		template<typename T>
		ObjectString<T> TextReader_<T>::ReadLine()
		{
			ObjectString<T> result;
			auto buffer = new T[65537];
			buffer[0] = 0;
			vint i = 0;
			while (true)
			{
				T c = ReadChar();
				if (c == L'\n' || c == 0)
				{
					buffer[i] = 0;
					result += buffer;
					buffer[0] = 0;
					i = 0;
					break;
				}
				else
				{
					if (i == 65536)
					{
						buffer[i] = 0;
						result += buffer;
						buffer[0] = 0;
						i = 0;
					}
					buffer[i++] = c;
				}
			}
			result += buffer;
			delete[] buffer;
			if (result.Length() > 0 && result[result.Length() - 1] == L'\r')
			{
				return result.Left(result.Length() - 1);
			}
			else
			{
				return result;
			}
		}

		template<typename T>
		ObjectString<T> TextReader_<T>::ReadToEnd()
		{
			ObjectString<T> result;
			auto buffer = new T[65537];
			buffer[0] = 0;
			vint i = 0;
			while (true)
			{
				T c = ReadChar();
				if (c == 0)
				{
					buffer[i] = 0;
					result += buffer;
					buffer[0] = 0;
					i = 0;
					break;
				}
				else
				{
					if (i == 65536)
					{
						buffer[i] = 0;
						result += buffer;
						buffer[0] = 0;
						i = 0;
					}
					buffer[i++] = c;
				}
			}
			result += buffer;
			delete[] buffer;
			return result;
		}

/***********************************************************************
TextWriter_<T>
***********************************************************************/

		template<typename T>
		void TextWriter_<T>::WriteString(const T* string, vint charCount)
		{
			while (*string)
			{
				WriteChar(*string++);
			}
		}

		template<typename T>
		void TextWriter_<T>::WriteString(const T* string)
		{
			vint len = 0;
			if constexpr (std::is_same_v<T, char>)
			{
				len = strlen(string);
			}
			else if constexpr (std::is_same_v<T, char8_t>)
			{
				len = strlen((const char*)string);
			}
			else if constexpr (std::is_same_v<T, wchar_t>)
			{
				len = wcslen(string);
			}
#if defined VCZH_WCHAR_UTF16
			else if constexpr (std::is_same_v<T, char16_t>)
			{
				len = wcslen((const wchar_t*)string);
			}
#elif defined VCZH_WCHAR_UTF32
			else if constexpr (std::is_same_v<T, char32_t>)
			{
				len = wcslen((const wchar_t*)string);
			}
#endif
			else
			{
				len = ObjectString<T>::Unmanaged(string).Length();
			}
			WriteString(string, len);
		}

		template<typename T>
		void TextWriter_<T>::WriteString(const ObjectString<T>& string)
		{
			if (string.Length())
			{
				WriteString(string.Buffer(), string.Length());
			}
		}

		template<typename T>
		void TextWriter_<T>::WriteLine(const T* string, vint charCount)
		{
			WriteString(string, charCount);
			WriteString(VCRLF<T>, 2);
		}

		template<typename T>
		void TextWriter_<T>::WriteLine(const T* string)
		{
			WriteString(string);
			WriteString(VCRLF<T>, 2);
		}

		template<typename T>
		void TextWriter_<T>::WriteLine(const ObjectString<T>& string)
		{
			WriteString(string);
			WriteString(VCRLF<T>, 2);
		}

/***********************************************************************
StringReader_<T>
***********************************************************************/

		template<typename T>
		void StringReader_<T>::PrepareIfLastCallIsReadLine()
		{
			if (lastCallIsReadLine)
			{
				lastCallIsReadLine = false;
				if (current < string.Length() && string[current] == L'\r') current++;
				if (current < string.Length() && string[current] == L'\n') current++;
			}
		}

		template<typename T>
		StringReader_<T>::StringReader_(const ObjectString<T>& _string)
			: string(_string)
			, current(0)
			, lastCallIsReadLine(false)
		{
		}

		template<typename T>
		bool StringReader_<T>::IsEnd()
		{
			return current == string.Length();
		}

		template<typename T>
		T StringReader_<T>::ReadChar()
		{
			PrepareIfLastCallIsReadLine();
			if (IsEnd())
			{
				return 0;
			}
			else
			{
				return string[current++];
			}
		}

		template<typename T>
		ObjectString<T> StringReader_<T>::ReadString(vint length)
		{
			PrepareIfLastCallIsReadLine();
			if (IsEnd())
			{
				return VEMPTYSTR<T>;
			}
			else
			{
				vint remain = string.Length() - current;
				if (length > remain) length = remain;
				ObjectString<T> result = string.Sub(current, length);
				current += length;
				return result;
			}
		}

		template<typename T>
		ObjectString<T> StringReader_<T>::ReadLine()
		{
			PrepareIfLastCallIsReadLine();
			if (IsEnd())
			{
				return VEMPTYSTR<T>;
			}
			else
			{
				vint lineEnd = current;
				while (lineEnd < string.Length())
				{
					T c = string[lineEnd];
					if (c == L'\r' || c == L'\n') break;
					lineEnd++;
				}
				ObjectString<T> result = string.Sub(current, lineEnd - current);
				current = lineEnd;
				lastCallIsReadLine = true;
				return result;
			}
		}

		template<typename T>
		ObjectString<T> StringReader_<T>::ReadToEnd()
		{
			return ReadString(string.Length() - current);
		}

/***********************************************************************
StreamReader_<T>
***********************************************************************/

		template<typename T>
		StreamReader_<T>::StreamReader_(IStream& _stream)
			: stream(&_stream)
		{
		}

		template<typename T>
		bool StreamReader_<T>::IsEnd()
		{
			return stream == nullptr;
		}

		template<typename T>
		T StreamReader_<T>::ReadChar()
		{
			if (stream)
			{
				T buffer = 0;
				if (stream->Read(&buffer, sizeof(buffer)) == 0)
				{
					stream = nullptr;
					return 0;
				}
				else
				{
					return buffer;
				}
			}
			else
			{
				return 0;
			}
		}

/***********************************************************************
StreamWriter_<T>
***********************************************************************/

		template<typename T>
		StreamWriter_<T>::StreamWriter_(IStream& _stream)
			:stream(&_stream)
		{
		}

		template<typename T>
		void StreamWriter_<T>::WriteChar(T c)
		{
			stream->Write(&c, sizeof(c));
		}

		template<typename T>
		void StreamWriter_<T>::WriteString(const T* string, vint charCount)
		{
			stream->Write((void*)string, charCount * sizeof(*string));
		}

/***********************************************************************
Extern Templates
***********************************************************************/

		template class TextReader_<wchar_t>;
		template class TextReader_<char8_t>;
		template class TextReader_<char16_t>;
		template class TextReader_<char32_t>;

		template class TextWriter_<wchar_t>;
		template class TextWriter_<char8_t>;
		template class TextWriter_<char16_t>;
		template class TextWriter_<char32_t>;

		template class StringReader_<wchar_t>;
		template class StringReader_<char8_t>;
		template class StringReader_<char16_t>;
		template class StringReader_<char32_t>;

		template class StreamReader_<wchar_t>;
		template class StreamReader_<char8_t>;
		template class StreamReader_<char16_t>;
		template class StreamReader_<char32_t>;

		template class StreamWriter_<wchar_t>;
		template class StreamWriter_<char8_t>;
		template class StreamWriter_<char16_t>;
		template class StreamWriter_<char32_t>;

/***********************************************************************
Extern Templates
***********************************************************************/

		namespace monospace_tabling
		{
			void WriteBorderLine(TextWriter& writer, Array<vint>& columnWidths, vint columns)
			{
				writer.WriteChar(L'+');
				for (vint i = 0; i < columns; i++)
				{
					vint c = columnWidths[i];
					for (vint j = 0; j < c; j++)
					{
						writer.WriteChar(L'-');
					}
					writer.WriteChar(L'+');
				}
				writer.WriteLine(L"");
			}

			void WriteContentLine(TextWriter& writer, Array<vint>& columnWidths, vint rowHeight, vint columns, Array<WString>& tableByRow, vint startRow)
			{
				vint cellStart = startRow * columns;
				for (vint r = 0; r < rowHeight; r++)
				{
					writer.WriteChar(L'|');
					for (vint c = 0; c < columns; c++)
					{
						const wchar_t* cell = tableByRow[cellStart + c].Buffer();
						for (vint i = 0; i < r; i++)
						{
							if (cell) cell = ::wcsstr(cell, L"\r\n");
							if (cell) cell += 2;
						}

						writer.WriteChar(L' ');
						vint length = 0;
						if (cell)
						{
							const wchar_t* end = ::wcsstr(cell, L"\r\n");
							length = end ? end - cell : (vint)wcslen(cell);
							writer.WriteString(cell, length);
						}

						for (vint i = columnWidths[c] - 2; i >= length; i--)
						{
							writer.WriteChar(L' ');
						}
						writer.WriteChar(L'|');
					}
					writer.WriteLine(L"");
				}
			}
		}

		void WriteMonospacedEnglishTable(TextWriter& writer, collections::Array<WString>& tableByRow, vint rows, vint columns)
		{
			Array<vint> rowHeights(rows);
			Array<vint> columnWidths(columns);
			for (vint i = 0; i < rows; i++) rowHeights[i] = 0;
			for (vint j = 0; j < columns; j++) columnWidths[j] = 0;

			for (vint i = 0; i < rows; i++)
			{
				for (vint j = 0; j < columns; j++)
				{
					WString text = tableByRow[i * columns + j];
					const wchar_t* reading = text.Buffer();
					vint width = 0;
					vint height = 0;

					while (reading)
					{
						height++;
						const wchar_t* crlf = ::wcsstr(reading, L"\r\n");
						if (crlf)
						{
							vint length = crlf - reading + 2;
							if (width < length) width = length;
							reading = crlf + 2;
						}
						else
						{
							vint length = ::wcslen(reading) + 2;
							if (width < length) width = length;
							reading = 0;
						}
					}

					if (rowHeights[i] < height) rowHeights[i] = height;
					if (columnWidths[j] < width) columnWidths[j] = width;
				}
			}

			monospace_tabling::WriteBorderLine(writer, columnWidths, columns);
			for (vint i = 0; i < rows; i++)
			{
				monospace_tabling::WriteContentLine(writer, columnWidths, rowHeights[i], columns, tableByRow, i);
				monospace_tabling::WriteBorderLine(writer, columnWidths, columns);
			}
		}
	}
}
