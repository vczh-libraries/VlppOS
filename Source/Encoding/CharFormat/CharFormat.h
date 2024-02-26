/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_ENCODING_CHARFORMAT
#define VCZH_STREAM_ENCODING_CHARFORMAT

#include "MbcsEncoding.h"
#include "UtfEncoding.h"
#include "BomEncoding.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
Utf-8
***********************************************************************/
		
#if defined VCZH_MSVC
		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf8Encoder : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars) override;
		};
#elif defined VCZH_GCC
		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf8Encoder : public UtfGeneralEncoder<char8_t> {};
#endif
		
		/// <summary>Decoder to read UTF-8 text.</summary>
		class Utf8Decoder : public UtfGeneralDecoder<char8_t> {};

/***********************************************************************
Utf-16 / Utf-16BE / Utf-32
***********************************************************************/

		/// <summary>Encoder to write big endian UTF-16 to.</summary>
		class Utf16BEEncoder : public UtfGeneralEncoder<char16be_t> {};
		/// <summary>Decoder to read big endian UTF-16 text.</summary>
		class Utf16BEDecoder : public UtfGeneralDecoder<char16be_t> {};

#if defined VCZH_WCHAR_UTF16
		
		/// <summary>Encoder to write UTF-16 text.</summary>
		class Utf16Encoder : public UtfGeneralEncoder<wchar_t> {};
		/// <summary>Decoder to read UTF-16 text.</summary>
		class Utf16Decoder : public UtfGeneralDecoder<wchar_t> {};
		
		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf32Encoder : public UtfGeneralEncoder<char32_t> {};
		/// <summary>Decoder to read UTF-8 text.</summary>
		class Utf32Decoder : public UtfGeneralDecoder<char32_t> {};

#elif defined VCZH_WCHAR_UTF32
		
		/// <summary>Encoder to write UTF-16 text.</summary>
		class Utf16Encoder : public UtfGeneralEncoder<char16_t> {};
		/// <summary>Decoder to read UTF-16 text.</summary>
		class Utf16Decoder : public UtfGeneralDecoder<char16_t> {};

		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf32Encoder : public UtfGeneralEncoder<wchar_t> {};
		/// <summary>Decoder to read UTF-8 text.</summary>
		class Utf32Decoder : public UtfGeneralDecoder<wchar_t> {};

#endif

/***********************************************************************
Encoding Test
***********************************************************************/

		/// <summary>Guess the text encoding in a buffer.</summary>
		/// <param name="buffer">The buffer to guess.</param>
		/// <param name="size">Size of the buffer in bytes.</param>
		/// <param name="encoding">Returns the most possible encoding.</param>
		/// <param name="containsBom">Returns true if the BOM information is at the beginning of the buffer.</param>
		extern void							TestEncoding(unsigned char* buffer, vint size, BomEncoder::Encoding& encoding, bool& containsBom);
	}
}

#endif
