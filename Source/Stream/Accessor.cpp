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

/***********************************************************************
TextReader_<T>
***********************************************************************/

		template<typename T>
		ObjectString<T> TextReader_<T>::ReadString(vint length)
		{
			T* buffer=new T[length+1];
			vint i=0;
			for(;i<length;i++)
			{
				if((buffer[i]=ReadChar())==L'\0')
				{
					break;
				}
			}
			buffer[i]=L'\0';
			ObjectString<T> result(buffer);
			delete[] buffer;
			return result;
		}

		template<typename T>
		ObjectString<T> TextReader_<T>::ReadLine()
		{
			ObjectString<T> result;
			auto buffer = new T[65537];
			buffer[0]=L'\0';
			vint i=0;
			while(true)
			{
				T c=ReadChar();
				if(c==L'\n' || c==L'\0')
				{
					buffer[i]=L'\0';
					result+=buffer;
					buffer[0]=L'\0';
					i=0;
					break;
				}
				else
				{
					if(i==65536)
					{
						buffer[i]=L'\0';
						result+=buffer;
						buffer[0]=L'\0';
						i=0;
					}
					buffer[i++]=c;
				}
			}
			result+=buffer;
			delete[] buffer;
			if(result.Length()>0 && result[result.Length()-1]==L'\r')
			{
				return result.Left(result.Length()-1);
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
			buffer[0]=L'\0';
			vint i=0;
			while(true)
			{
				T c=ReadChar();
				if(c==L'\0')
				{
					buffer[i]=L'\0';
					result+=buffer;
					buffer[0]=L'\0';
					i=0;
					break;
				}
				else
				{
					if(i==65536)
					{
						buffer[i]=L'\0';
						result+=buffer;
						buffer[0]=L'\0';
						i=0;
					}
					buffer[i++]=c;
				}
			}
			result+=buffer;
			delete[] buffer;
			return result;
		}

/***********************************************************************
TextWriter_<T>
***********************************************************************/

		template<typename T>
		void TextWriter_<T>::WriteString(const T* string, vint charCount)
		{
			while(*string)
			{
				WriteChar(*string++);
			}
		}

		template<typename T>
		void TextWriter_<T>::WriteString(const T* string)
		{
			WriteString(string, (vint)wcslen(string));
		}

		template<typename T>
		void TextWriter_<T>::WriteString(const ObjectString<T>& string)
		{
			if(string.Length())
			{
				WriteString(string.Buffer(), string.Length());
			}
		}

		template<typename T>
		void TextWriter_<T>::WriteLine(const T* string, vint charCount)
		{
			WriteString(string, charCount);
			WriteString(L"\r\n", 2);
		}

		template<typename T>
		void TextWriter_<T>::WriteLine(const T* string)
		{
			WriteString(string);
			WriteString(L"\r\n", 2);
		}

		template<typename T>
		void TextWriter_<T>::WriteLine(const ObjectString<T>& string)
		{
			WriteString(string);
			WriteString(L"\r\n", 2);
		}

		namespace monospace_tabling
		{
			template<typename T>
			void WriteBorderLine(TextWriter_<T>& writer, Array<vint>& columnWidths, vint columns)
			{
				writer.WriteChar(L'+');
				for(vint i=0;i<columns;i++)
				{
					vint c=columnWidths[i];
					for(vint j=0;j<c;j++)
					{
						writer.WriteChar(L'-');
					}
					writer.WriteChar(L'+');
				}
				writer.WriteLine(L"");
			}

			template<typename T>
			void WriteContentLine(TextWriter_<T>& writer, Array<vint>& columnWidths, vint rowHeight, vint columns, Array<ObjectString<T>>& tableByRow, vint startRow)
			{
				vint cellStart=startRow*columns;
				for(vint r=0;r<rowHeight;r++)
				{
					writer.WriteChar(L'|');
					for(vint c=0;c<columns;c++)
					{
						const T* cell=tableByRow[cellStart+c].Buffer();
						for(vint i=0;i<r;i++)
						{
							if(cell) cell=::wcsstr(cell, L"\r\n");
							if(cell) cell+=2;
						}

						writer.WriteChar(L' ');
						vint length=0;
						if(cell)
						{
							const T* end=::wcsstr(cell, L"\r\n");
							length=end?end-cell:(vint)wcslen(cell);
							writer.WriteString(cell, length);
						}

						for(vint i=columnWidths[c]-2;i>=length;i--)
						{
							writer.WriteChar(L' ');
						}
						writer.WriteChar(L'|');
					}
					writer.WriteLine(L"");
				}
			}
		}
		using namespace monospace_tabling;

		template<typename T>
		void TextWriter_<T>::WriteMonospacedEnglishTable(collections::Array<ObjectString<T>>& tableByRow, vint rows, vint columns)
		{
			Array<vint> rowHeights(rows);
			Array<vint> columnWidths(columns);
			for(vint i=0;i<rows;i++) rowHeights[i]=0;
			for(vint j=0;j<columns;j++) columnWidths[j]=0;

			for(vint i=0;i<rows;i++)
			{
				for(vint j=0;j<columns;j++)
				{
					ObjectString<T> text=tableByRow[i*columns+j];
					const T* reading=text.Buffer();
					vint width=0;
					vint height=0;

					while(reading)
					{
						height++;
						const T* crlf=::wcsstr(reading, L"\r\n");
						if(crlf)
						{
							vint length=crlf-reading+2;
							if(width<length) width=length;
							reading=crlf+2;
						}
						else
						{
							vint length=(vint)wcslen(reading)+2;
							if(width<length) width=length;
							reading=0;
						}
					}

					if(rowHeights[i]<height) rowHeights[i]=height;
					if(columnWidths[j]<width) columnWidths[j]=width;
				}
			}

			WriteBorderLine(*this, columnWidths, columns);
			for(vint i=0;i<rows;i++)
			{
				WriteContentLine(*this, columnWidths, rowHeights[i], columns, tableByRow, i);
				WriteBorderLine(*this, columnWidths, columns);
			}
		}

/***********************************************************************
StringReader_<T>
***********************************************************************/

		template<typename T>
		void StringReader_<T>::PrepareIfLastCallIsReadLine()
		{
			if(lastCallIsReadLine)
			{
				lastCallIsReadLine=false;
				if(current<string.Length() && string[current]==L'\r') current++;
				if(current<string.Length() && string[current]==L'\n') current++;
			}
		}

		template<typename T>
		StringReader_<T>::StringReader_(const ObjectString<T>& _string)
			:string(_string)
			,current(0)
			,lastCallIsReadLine(false)
		{
		}

		template<typename T>
		bool StringReader_<T>::IsEnd()
		{
			return current==string.Length();
		}

		template<typename T>
		T StringReader_<T>::ReadChar()
		{
			PrepareIfLastCallIsReadLine();
			if(IsEnd())
			{
				return L'\0';
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
			if(IsEnd())
			{
				return L"";
			}
			else
			{
				vint remain=string.Length()-current;
				if(length>remain) length=remain;
				ObjectString<T> result=string.Sub(current, length);
				current+=length;
				return result;
			}
		}

		template<typename T>
		ObjectString<T> StringReader_<T>::ReadLine()
		{
			PrepareIfLastCallIsReadLine();
			if(IsEnd())
			{
				return L"";
			}
			else
			{
				vint lineEnd=current;
				while(lineEnd<string.Length())
				{
					T c=string[lineEnd];
					if(c==L'\r' || c==L'\n') break;
					lineEnd++;
				}
				ObjectString<T> result=string.Sub(current, lineEnd-current);
				current=lineEnd;
				lastCallIsReadLine=true;
				return result;
			}
		}

		template<typename T>
		ObjectString<T> StringReader_<T>::ReadToEnd()
		{
			return ReadString(string.Length()-current);
		}

/***********************************************************************
StreamReader_<T>
***********************************************************************/

		template<typename T>
		StreamReader_<T>::StreamReader_(IStream& _stream)
			:stream(&_stream)
		{
		}

		template<typename T>
		bool StreamReader_<T>::IsEnd()
		{
			return stream==0;
		}

		template<typename T>
		T StreamReader_<T>::ReadChar()
		{
			if(stream)
			{
				T buffer=0;
				if(stream->Read(&buffer, sizeof(buffer))==0)
				{
					stream=0;
					return 0;
				}
				else
				{
					return buffer;
				}
			}
			else
			{
				return L'\0';
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
			stream->Write((void*)string, charCount*sizeof(*string));
		}

/***********************************************************************
Extern Templates
***********************************************************************/

		extern template class TextReader_<wchar_t>;
		extern template class TextReader_<char8_t>;
		extern template class TextReader_<char16_t>;
		extern template class TextReader_<char32_t>;

		extern template class TextWriter_<wchar_t>;
		extern template class TextWriter_<char8_t>;
		extern template class TextWriter_<char16_t>;
		extern template class TextWriter_<char32_t>;

		extern template class StringReader_<wchar_t>;
		extern template class StringReader_<char8_t>;
		extern template class StringReader_<char16_t>;
		extern template class StringReader_<char32_t>;

		extern template class StreamReader_<wchar_t>;
		extern template class StreamReader_<char8_t>;
		extern template class StreamReader_<char16_t>;
		extern template class StreamReader_<char32_t>;

		extern template class StreamWriter_<wchar_t>;
		extern template class StreamWriter_<char8_t>;
		extern template class StreamWriter_<char16_t>;
		extern template class StreamWriter_<char32_t>;
	}
}
