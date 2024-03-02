#include "../../Source/Stream/Accessor.h"
#include "../../Source/Stream/EncodingStream.h"
#include "../../Source/Encoding/CharFormat/CharFormat.h"
#include "../../Source/Locale.h"

using namespace vl;
using namespace vl::stream;
using namespace vl::collections;

namespace TestStreamEncoding_TestObjects
{
	template<typename T>
	void SwapBytesForUtf16BE(T* _buffer, vint chars)
	{
		static_assert(sizeof(T) == sizeof(char16_t));
		for (vint i = 0; i < chars; i++)
		{
			encoding::SwapByteForUtf16BE(_buffer[i]);
		}
	}

	template<typename T> constexpr BomEncoder::Encoding EncodingOf = BomEncoder::Mbcs;
	template<> constexpr BomEncoder::Encoding EncodingOf<char8_t> = BomEncoder::Utf8;
	template<> constexpr BomEncoder::Encoding EncodingOf<char16_t> = BomEncoder::Utf16;
	template<> constexpr BomEncoder::Encoding EncodingOf<char16be_t> = BomEncoder::Utf16BE;

	template<typename TNative>
	void TestEncoding(
		vint decodedBomOffset,
		Array<vuint8_t>& buffer
	)
	{
		BomEncoder::Encoding resultEncoding;
		bool resultContainsBom;
		TestEncoding(&buffer[0], buffer.Count(), resultEncoding, resultContainsBom);
		TEST_ASSERT(EncodingOf<TNative> == resultEncoding);
		TEST_ASSERT((decodedBomOffset != 0) == resultContainsBom);
	}

	template<>
	void TestEncoding<char>(
		vint decodedBomOffset,
		Array<vuint8_t>& buffer
		) {}

	template<>
	void TestEncoding<char32_t>(
		vint decodedBomOffset,
		Array<vuint8_t>& buffer
		) {}

	template<typename TNative, typename TExpect, size_t NativeLength, size_t ExpectLength>
	void TestEncodingWithStreamReaderWriter(
		IEncoder& encoder,
		IDecoder& decoder,
		const TExpect(&text)[ExpectLength],
		vint decodedBomOffset,
		const TNative(&decodedText)[NativeLength]
		)
	{
		constexpr vint TextLength = ExpectLength - 1;
		constexpr vint TextBytes = TextLength * sizeof(TExpect);
		constexpr vint DecodedLength = NativeLength - 1;
		constexpr vint DecodedBytes = DecodedLength * sizeof(TNative);

		// encode the text
		MemoryStream memoryStream;
		{
			EncoderStream encoderStream(memoryStream, encoder);
			StreamWriter_<TExpect> writer(encoderStream);
			writer.WriteString(text);
		}
		memoryStream.SeekFromBegin(0);

		// read the encoded data
		Array<vuint8_t> buffer;
		buffer.Resize((vint)memoryStream.Size());
		memoryStream.Read(&buffer[0], buffer.Count());
		memoryStream.SeekFromBegin(0);

		// log the encoded data
		{
			WString output;
			for (vint i = 0; i < buffer.Count(); i++)
			{
				vuint8_t byte = buffer[i];
				output += WString::FromChar(L"0123456789ABCDEF"[byte / 16]);
				output += WString::FromChar(L"0123456789ABCDEF"[byte % 16]);
				output += WString::FromChar(L' ');
			}
			TEST_PRINT(L"\tEncoded: " + output);
		}

		// compare the encoded data to the expected data
		TEST_ASSERT(buffer.Count() == decodedBomOffset + DecodedBytes);
		TEST_ASSERT(memcmp(&buffer[decodedBomOffset], decodedText, DecodedBytes) == 0);

		// test the encoding and decode
		{
			TestEncoding<TNative>(decodedBomOffset, buffer);

			DecoderStream decoderStream(memoryStream, decoder);
			StreamReader_<TExpect> reader(decoderStream);
			auto read = reader.ReadToEnd();
			TEST_ASSERT(read == text);
		}
	};

	template<typename TNative, typename TExpect, size_t NativeLength, size_t ExpectLength>
	void TestEncodingWithEncoderDecoderStream(
		IEncoder& encoder,
		IDecoder& decoder,
		const TExpect(&text)[ExpectLength],
		const TNative(&decodedText)[NativeLength]
	)
	{
		constexpr vint TextLength = ExpectLength - 1;
		constexpr vint TextBytes = TextLength * sizeof(TExpect);
		constexpr vint DecodedLength = NativeLength - 1;
		constexpr vint DecodedBytes = DecodedLength * sizeof(TNative);

		// encode the text
		MemoryStream memoryStream;
		{
			EncoderStream encoderStream(memoryStream, encoder);
			vint size = encoderStream.Write((void*)text, TextBytes);
			TEST_ASSERT(size == TextBytes);
		}
		memoryStream.SeekFromBegin(0);

		{
			// read the encoded data
			Array<vuint8_t> buffer;
			buffer.Resize((vint)memoryStream.Size());
			memoryStream.Read(&buffer[0], buffer.Count());
			memoryStream.SeekFromBegin(0);

			// compare the encoded data to the expected data
			TEST_ASSERT(buffer.Count() == DecodedBytes);
			TEST_ASSERT(memcmp(&buffer[0], decodedText, DecodedBytes) == 0);
		}

		// test the encoding and decode
		{
			DecoderStream decoderStream(memoryStream, decoder);
			TExpect* buffer = new TExpect[ExpectLength];
			vint size = decoderStream.Read(buffer, TextBytes);
			TEST_ASSERT(size == TextBytes);
			vint zero = decoderStream.Read(buffer, 1);
			TEST_ASSERT(zero == 0);
			buffer[TextLength] = 0;
			auto read = ObjectString<TExpect>::TakeOver(buffer, TextLength);
			TEST_ASSERT(read == text);
		}
	};

	template<size_t BatchSize, typename TNative, typename TExpect, size_t NativeLength, size_t ExpectLength>
	void TestEncodingWithEncoderDecoderStreamPerByte(
		IEncoder& encoder,
		IDecoder& decoder,
		const TExpect(&text)[ExpectLength],
		const TNative(&decodedText)[NativeLength]
	)
	{
		constexpr vint TextLength = ExpectLength - 1;
		constexpr vint TextBytes = TextLength * sizeof(TExpect);
		constexpr vint DecodedLength = NativeLength - 1;
		constexpr vint DecodedBytes = DecodedLength * sizeof(TNative);

		// encode the text
		MemoryStream memoryStream;
		{
			EncoderStream encoderStream(memoryStream, encoder);
			auto bytes = (const char*)text;
			for (vint i = 0; i < TextBytes; i+=BatchSize)
			{
				vint remaining = TextBytes - i;
				vint batch = remaining < BatchSize ? remaining : BatchSize;
				vint written = encoderStream.Write((void*)(bytes + i), batch);
				TEST_ASSERT(written == batch);
			}
		}
		memoryStream.SeekFromBegin(0);

		{
			// read the encoded data
			Array<vuint8_t> buffer;
			buffer.Resize((vint)memoryStream.Size());
			memoryStream.Read(&buffer[0], buffer.Count());
			memoryStream.SeekFromBegin(0);

			// compare the encoded data to the expected data
			TEST_ASSERT(buffer.Count() == DecodedBytes);
			TEST_ASSERT(memcmp(&buffer[0], decodedText, DecodedBytes) == 0);
		}

		// test the encoding and decode
		{
			DecoderStream decoderStream(memoryStream, decoder);
			TExpect* buffer = new TExpect[ExpectLength];
			{
				char* bytes = (char*)buffer;
				for (vint i = 0; i < TextBytes; i+=BatchSize)
				{
					vint remaining = TextBytes - i;
					vint batch = remaining < BatchSize ? remaining : BatchSize;
					vint read = decoderStream.Read(bytes + i, batch);
					TEST_ASSERT(read == batch);
				}
				vint zero = decoderStream.Read(bytes, 1);
				TEST_ASSERT(zero == 0);
			}
			buffer[TextLength] = 0;
			auto read = ObjectString<TExpect>::TakeOver(buffer, TextLength);
			TEST_ASSERT(read == text);
		}
	};

	template<typename TNative, typename TExpect, size_t NativeLength, size_t ExpectLength>
	void TestEncodingWithBOM(
		const TExpect(&text)[ExpectLength],
		vint decodedBomOffset,
		const TNative(&decodedText)[NativeLength]
	)
	{
		BomEncoder encoder(EncodingOf<TNative>);
		BomDecoder decoder;
		TestEncodingWithStreamReaderWriter(encoder, decoder, text, decodedBomOffset, decodedText);
	}

	template<typename TNative, typename TExpect, typename TEncoder, typename TDecoder, size_t NativeLength, size_t ExpectLength>
	void TestEncodingUnrelatedToBOM(
		const TExpect(&text)[ExpectLength],
		const TNative(&decodedText)[NativeLength]
	)
	{
		{
			TEST_PRINT(L"Encoder Decoder");
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithEncoderDecoderStream(encoder, decoder, text, decodedText);
		}
		{
			TEST_PRINT(L"Per 1 Byte");
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithEncoderDecoderStreamPerByte<1>(encoder, decoder, text, decodedText);
		}
		{
			TEST_PRINT(L"Per8  Byte");
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithEncoderDecoderStreamPerByte<8>(encoder, decoder, text, decodedText);
		}
		{
			TEST_PRINT(L"Per 11 Byte");
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithEncoderDecoderStreamPerByte<11>(encoder, decoder, text, decodedText);
		}
	}

	template<typename TNative, typename TExpect, typename TEncoder, typename TDecoder, size_t NativeLength, size_t ExpectLength>
	void TestEncodingWithoutBOM(
		const TExpect(&text)[ExpectLength],
		const TNative(&decodedText)[NativeLength]
	)
	{
		{
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithStreamReaderWriter(encoder, decoder, text, 0, decodedText);
		}
		TestEncodingUnrelatedToBOM<TNative, TExpect, TEncoder, TDecoder>(text, decodedText);
	}
}
using namespace TestStreamEncoding_TestObjects;

TEST_FILE
{
	/***********************************************************************
	Baselines
	***********************************************************************/

	const wchar_t text1L[] = L"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
	const char8_t text1U8[] = u8"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
	const char16_t text1U16[] = u"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
	char16be_t text1U16BE[sizeof(text1U16) / sizeof(*text1U16)];
	const char32_t text1U32[] = U"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";

	const wchar_t text2L[] = L"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char text2A[] = "ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char8_t text2U8[] = u8"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char16_t text2U16[] = u"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	char16be_t text2U16BE[sizeof(text2U16) / sizeof(*text2U16)];
	const char32_t text2U32[] = U"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";

	memcpy(text1U16BE, text1U16, sizeof(text1U16));
	SwapBytesForUtf16BE(text1U16BE, sizeof(text1U16BE) / sizeof(*text1U16BE));

	memcpy(text2U16BE, text2U16, sizeof(text2U16));
	SwapBytesForUtf16BE(text2U16BE, sizeof(text2U16BE) / sizeof(*text2U16BE));

	/***********************************************************************
	Encoding
	***********************************************************************/

	TEST_CATEGORY(L"Predefined UTF Encoding")
	{
		TEST_CASE(L"<MBCS, NO-BOM>")
		{
			TestEncodingWithoutBOM<char, wchar_t, MbcsEncoder, MbcsDecoder>(
				text2L,
				text2A
				);
		});
		TEST_CASE(L"<UTF8, NO-BOM>")
		{
			TestEncodingWithoutBOM<char8_t, wchar_t, Utf8Encoder, Utf8Decoder>(
				text1L,
				text1U8
				);
		});
		TEST_CASE(L"<UTF16, NO-BOM>")
		{
			TestEncodingWithoutBOM<char16_t, wchar_t, Utf16Encoder, Utf16Decoder>(
				text1L,
				text1U16
				);
		});
		TEST_CASE(L"<UTF16_BE, NO-BOM>")
		{
			TestEncodingWithoutBOM<char16be_t, wchar_t, Utf16BEEncoder, Utf16BEDecoder>(
				text1L,
				text1U16BE
				);
		});
		TEST_CASE(L"<UTF32, NO-BOM>")
		{
			TestEncodingWithoutBOM<char32_t, wchar_t, Utf32Encoder, Utf32Decoder>(
				text1L,
				text1U32
				);
		});
	});

	TEST_CATEGORY(L"BOM Encoding")
	{
		TEST_CASE(L"<MBCS, BOM>")
		{
			TestEncodingWithBOM<char, wchar_t>(
				text2L,
				0,
				text2A
				);
		});
		TEST_CASE(L"<UTF8, BOM>")
		{
			TestEncodingWithBOM<char8_t, wchar_t>(
				text2L,
				3,
				text2U8
				);
		});
		TEST_CASE(L"<UTF16, BOM>")
		{
			TestEncodingWithBOM<char16_t, wchar_t>(
				text2L,
				2,
				text2U16
				);
		});
		TEST_CASE(L"<UTF16_BE, BOM>")
		{
			TestEncodingWithBOM<char16be_t, wchar_t>(
				text2L,
				2,
				text2U16BE
				);
		});
	});

	TEST_CATEGORY(L"General UTF Encoding")
	{
#define BUFFER_wchar_t text1L
#define BUFFER_char8_t text1U8
#define BUFFER_char16_t text1U16
#define BUFFER_char16be_t text1U16BE
#define BUFFER_char32_t text1U32

#define UTF_ENCODING_TEST_SECOND_CHAR_TYPE(FIRST, SECOND)\
		TEST_CASE(L"Native:" L ## #FIRST L" Expect:" L ## #SECOND)\
		{\
			TestEncodingUnrelatedToBOM<FIRST, SECOND, UtfGeneralEncoder<FIRST, SECOND>, UtfGeneralDecoder<FIRST, SECOND>>(\
				BUFFER_ ## SECOND,\
				BUFFER_ ## FIRST\
				);\
		});\

#define UTF_ENCODING_TEST_FIRST_CHAR_TYPE(FIRST)\
		UTF_ENCODING_TEST_SECOND_CHAR_TYPE(FIRST, wchar_t)\
		UTF_ENCODING_TEST_SECOND_CHAR_TYPE(FIRST, char8_t)\
		UTF_ENCODING_TEST_SECOND_CHAR_TYPE(FIRST, char16_t)\
		UTF_ENCODING_TEST_SECOND_CHAR_TYPE(FIRST, char16be_t)\
		UTF_ENCODING_TEST_SECOND_CHAR_TYPE(FIRST, char32_t)\

#define UTF_ENCODING_TEST\
		UTF_ENCODING_TEST_FIRST_CHAR_TYPE(wchar_t)\
		UTF_ENCODING_TEST_FIRST_CHAR_TYPE(char8_t)\
		UTF_ENCODING_TEST_FIRST_CHAR_TYPE(char16_t)\
		UTF_ENCODING_TEST_FIRST_CHAR_TYPE(char16be_t)\
		UTF_ENCODING_TEST_FIRST_CHAR_TYPE(char32_t)\

		UTF_ENCODING_TEST
	});
}