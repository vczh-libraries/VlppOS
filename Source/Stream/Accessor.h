/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_ACCESSOR
#define VCZH_STREAM_ACCESSOR

#include "Interfaces.h"
#include "MemoryStream.h"

namespace vl
{
	namespace stream
	{

/***********************************************************************
Text Related
***********************************************************************/

		/// <summary>Text reader. All line breaks are normalized to CRLF regardless whatever in the input stream.</summary>
		/// <typeparam name="T">The character type.</typeparam>
		template<typename T>
		class TextReader_ : public Object
		{
		public:
			NOT_COPYABLE(TextReader_);
			TextReader_() = default;

			/// <summary>Test does the reader reach the end or not.</summary>
			/// <returns>Returns true if the reader reaches the end.</returns>
			virtual bool				IsEnd()=0;
			/// <summary>Read a single character.</summary>
			/// <returns>The character.</returns>
			virtual T					ReadChar()=0;
			/// <summary>Read a string of a specified size in characters.</summary>
			/// <returns>The read string. It could be shorter than the expected length if the reader reaches the end.</returns>
			/// <param name="length">Expected length of the string to read.</param>
			virtual ObjectString<T>		ReadString(vint length);
			/// <summary>Read a string until a line breaks is reached.</summary>
			/// <returns>The string without the line break. If the reader reaches the end, it returns an empty string.</returns>
			virtual ObjectString<T>		ReadLine();
			/// <summary>Read everying remain.</summary>
			/// <returns>The read string.</returns>
			virtual ObjectString<T>		ReadToEnd();
		};
		
		/// <summary>Text writer.</summary>
		/// <typeparam name="T">The character type.</typeparam>
		template<typename T>
		class TextWriter_ : public Object
		{
		public:
			NOT_COPYABLE(TextWriter_);
			TextWriter_() = default;

			/// <summary>Write a single character.</summary>
			/// <param name="c">The character to write.</param>
			virtual void				WriteChar(T c)=0;
			/// <summary>Write a string.</summary>
			/// <param name="string">Buffer of the string to write.</param>
			/// <param name="charCount">Size of the string in characters, not including the zero terminator.</param>
			virtual void				WriteString(const T* string, vint charCount);
			/// <summary>Write a string.</summary>
			/// <param name="string">Buffer of the zero terminated string to write.</param>
			virtual void				WriteString(const T* string);
			/// <summary>Write a string.</summary>
			/// <param name="string">The string to write.</param>
			virtual void				WriteString(const ObjectString<T>& string);
			/// <summary>Write a string with a CRLF.</summary>
			/// <param name="string">Buffer to the string to write.</param>
			/// <param name="charCount">Size of the string in characters, not including the zero terminator.</param>
			virtual void				WriteLine(const T* string, vint charCount);
			/// <summary>Write a string with a CRLF.</summary>
			/// <param name="string">Buffer to the zero terminated string to write.</param>
			virtual void				WriteLine(const T* string);
			/// <summary>Write a string with a CRLF.</summary>
			/// <param name="string">The string to write.</param>
			virtual void				WriteLine(const ObjectString<T>& string);
		};

		/// <summary>Text reader from a string.</summary>
		/// <typeparam name="T">The character type.</typeparam>
		template<typename T>
		class StringReader_ : public TextReader_<T>
		{
		protected:
			ObjectString<T>				string;
			vint						current;
			bool						lastCallIsReadLine;

			void						PrepareIfLastCallIsReadLine();
		public:
			/// <summary>Create a text reader.</summary>
			/// <param name="_string">The string to read.</param>
			StringReader_(const ObjectString<T>& _string);

			bool						IsEnd();
			T							ReadChar();
			ObjectString<T>				ReadString(vint length);
			ObjectString<T>				ReadLine();
			ObjectString<T>				ReadToEnd();
		};

		/// <summary>
		/// Text reader from a stream storing characters in code point.
		/// </summary>
		/// <typeparam name="T">The character type.</typeparam>
		/// <remarks>
		/// To specify the encoding in the input stream,
		/// you are recommended to create a <see cref="DecoderStream"/> with a <see cref="UtfGeneralDecoder"/> implementation,
		/// like <see cref="BomDecoder"/>, <see cref="MbcsDecoder"/>, <see cref="Utf16Decoder"/>, <see cref="Utf16BEDecoder"/> or <see cref="Utf8Decoder"/>.
		/// </remarks>
		/// <example output="false"><![CDATA[
		/// int main()
		/// {
		///     FileStream fileStream(L"C:/a.txt", FileStream::ReadOnly);
		///     Utf8Decoder decoder;
		///     DecoderStream decoderStream(fileStream, decoder);
		///     StreamReader reader(decoderStream);
		///     Console::WriteLine(reader.ReadToEnd());
		/// }
		/// ]]></example>
		template<typename T>
		class StreamReader_ : public TextReader_<T>
		{
		protected:
			IStream*					stream;
		public:
			/// <summary>Create a text reader.</summary>
			/// <param name="_stream">The stream to read.</param>
			StreamReader_(IStream& _stream);

			bool						IsEnd();
			T							ReadChar();
		};

		/// <summary>
		/// Text reader from a stream storing characters in code point.
		/// </summary>
		/// <typeparam name="T">The character type.</typeparam>
		/// <remarks>
		/// To specify the encoding in the input stream,
		/// you are recommended to create a <see cref="EncoderStream"/> with a <see cref="UtfGeneralEncoder"/> implementation,
		/// like <see cref="BomEncoder"/>, <see cref="MbcsEncoder"/>, <see cref="Utf16Encoder"/>, <see cref="Utf16BEEncoder"/> or <see cref="Utf8Encoder"/>.
		/// </remarks>
		/// <example output="false"><![CDATA[
		/// int main()
		/// {
		///     FileStream fileStream(L"C:/a.txt", FileStream::WriteOnly);
		///     Utf8Encoder encoder;
		///     EncoderStream encoderStream(fileStream, encoder);
		///     StreamWriter writer(encoderStream);
		///     writer.Write(L"Hello, world!");
		/// }
		/// ]]></example>
		template<typename T>
		class StreamWriter_ : public TextWriter_<T>
		{
		protected:
			IStream* stream;
		public:
			/// <summary>Create a text writer.</summary>
			/// <param name="_stream">The stream to write.</param>
			StreamWriter_(IStream& _stream);
			using TextWriter_<T>::WriteString;

			void						WriteChar(T c);
			void						WriteString(const T* string, vint charCount);
		};

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

/***********************************************************************
Helper Functions
***********************************************************************/

		using TextReader = TextReader_<wchar_t>;
		using TextWriter = TextWriter_<wchar_t>;
		using StringReader = StringReader_<wchar_t>;
		using StreamReader = StreamReader_<wchar_t>;
		using StreamWriter = StreamWriter_<wchar_t>;

		void WriteMonospacedEnglishTable(TextWriter& writer, collections::Array<WString>& tableByRow, vint rows, vint columns);

		/// <summary>
		/// Build a big string using <see cref="StreamWriter"/>.
		/// </summary>
		/// <typeparam name="TCallback">The type of the callback.</typeparam>
		/// <returns>The built big string.</returns>
		/// <param name="callback">
		/// The callback to receive a big string.
		/// The argument is a reference to a <see cref="StreamWriter"/>.
		/// After the callback is executed, everything written to the writer will be returned from "GenerateToStream".
		/// </param>
		/// <param name="block">Size of the cache in bytes.</param>
		/// <example><![CDATA[
		/// int main()
		/// {
		///     Console::Write(GenerateToStream([](StreamWriter& writer)
		///     {
		///         writer.WriteLine(L"Hello, world!");
		///         writer.WriteLine(L"Welcome to Gaclib!");
		///     }));
		/// }
		/// ]]></example>
		template<typename TCallback>
		WString GenerateToStream(const TCallback& callback, vint block = 65536)
		{
			MemoryStream stream(block);
			{
				StreamWriter writer(stream);
				callback(writer);
			}
			stream.SeekFromBegin(0);
			{
				StreamReader reader(stream);
				return reader.ReadToEnd();
			}
		}
	}
}

#endif