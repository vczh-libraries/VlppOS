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
		class TextReader : public Object
		{
		public:
			NOT_COPYABLE(TextReader);
			TextReader() = default;

			/// <summary>Test does the reader reach the end or not.</summary>
			/// <returns>Returns true if the reader reaches the end.</returns>
			virtual bool				IsEnd()=0;
			/// <summary>Read a single character.</summary>
			/// <returns>The character.</returns>
			virtual wchar_t				ReadChar()=0;
			/// <summary>Read a string of a specified size in characters.</summary>
			/// <returns>The read string. It could be shorter than the expected length if the reader reaches the end.</returns>
			/// <param name="length">Expected length of the string to read.</param>
			virtual WString				ReadString(vint length);
			/// <summary>Read a string until a line breaks is reached.</summary>
			/// <returns>The string without the line break. If the reader reaches the end, it returns an empty string.</returns>
			virtual WString				ReadLine();
			/// <summary>Read everying remain.</summary>
			/// <returns>The read string.</returns>
			virtual WString				ReadToEnd();
		};
		
		/// <summary>Text writer.</summary>
		class TextWriter : public Object
		{
		public:
			NOT_COPYABLE(TextWriter);
			TextWriter() = default;

			/// <summary>Write a single character.</summary>
			/// <param name="c">The character to write.</param>
			virtual void				WriteChar(wchar_t c)=0;
			/// <summary>Write a string.</summary>
			/// <param name="string">Buffer of the string to write.</param>
			/// <param name="charCount">Size of the string in characters, not including the zero terminator.</param>
			virtual void				WriteString(const wchar_t* string, vint charCount);
			/// <summary>Write a string.</summary>
			/// <param name="string">Buffer of the zero terminated string to write.</param>
			virtual void				WriteString(const wchar_t* string);
			/// <summary>Write a string.</summary>
			/// <param name="string">The string to write.</param>
			virtual void				WriteString(const WString& string);
			/// <summary>Write a string with a CRLF.</summary>
			/// <param name="string">Buffer to the string to write.</param>
			/// <param name="charCount">Size of the string in characters, not including the zero terminator.</param>
			virtual void				WriteLine(const wchar_t* string, vint charCount);
			/// <summary>Write a string with a CRLF.</summary>
			/// <param name="string">Buffer to the zero terminated string to write.</param>
			virtual void				WriteLine(const wchar_t* string);
			/// <summary>Write a string with a CRLF.</summary>
			/// <param name="string">The string to write.</param>
			virtual void				WriteLine(const WString& string);

			virtual void				WriteMonospacedEnglishTable(collections::Array<WString>& tableByRow, vint rows, vint columns);
		};

		/// <summary>Text reader from a string.</summary>
		class StringReader : public TextReader
		{
		protected:
			WString						string;
			vint						current;
			bool						lastCallIsReadLine;

			void						PrepareIfLastCallIsReadLine();
		public:
			/// <summary>Create a text reader.</summary>
			/// <param name="_string">The string to read.</param>
			StringReader(const WString& _string);

			bool						IsEnd();
			wchar_t						ReadChar();
			WString						ReadString(vint length);
			WString						ReadLine();
			WString						ReadToEnd();
		};

		/// <summary>
		/// Text reader from a stream storing characters in wchar_t.
		/// </summary>
		/// <remarks>
		/// To specify the encoding in the input stream,
		/// you are recommended to create a <see cref="DecoderStream"/> with a <see cref="CharDecoder"/>,
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
		class StreamReader : public TextReader
		{
		protected:
			IStream*					stream;
		public:
			/// <summary>Create a text reader.</summary>
			/// <param name="_stream">The stream to read.</param>
			StreamReader(IStream& _stream);

			bool						IsEnd();
			wchar_t						ReadChar();
		};

		/// <summary>
		/// Text reader from a stream storing characters in wchar_t.
		/// </summary>
		/// <remarks>
		/// To specify the encoding in the input stream,
		/// you are recommended to create a <see cref="EncoderStream"/> with a <see cref="CharEncoder"/>,
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
		class StreamWriter : public TextWriter
		{
		protected:
			IStream*					stream;
		public:
			/// <summary>Create a text writer.</summary>
			/// <param name="_stream">The stream to write.</param>
			StreamWriter(IStream& _stream);
			using TextWriter::WriteString;

			void						WriteChar(wchar_t c);
			void						WriteString(const wchar_t* string, vint charCount);
		};

/***********************************************************************
Helper Functions
***********************************************************************/

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