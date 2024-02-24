#include "../../Source/Stream/Accessor.h"
#include "../../Source/Stream/EncodingStream.h"
#include "../../Source/Stream/CharFormat.h"
#include "../../Source/Locale.h"

using namespace vl;
using namespace vl::stream;
using namespace vl::collections;

namespace TestStreamEncoding_TestObjects
{
	void TestEncodingWithStreamReaderWriter(
		IEncoder& encoder,
		IDecoder& decoder,
		BomEncoder::Encoding encoding,
		const wchar_t* text,
		vint decodedBomOffset,
		const void* decodedBytes,
		vint decodedByteLength,
		bool testEncoding
		)
	{
		// encode the text
		MemoryStream memoryStream;
		{
			EncoderStream encoderStream(memoryStream, encoder);
			StreamWriter writer(encoderStream);
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
		TEST_ASSERT(buffer.Count() == decodedBomOffset + decodedByteLength);
		TEST_ASSERT(memcmp(&buffer[decodedBomOffset], decodedBytes, decodedByteLength) == 0);

		// test the encoding and decode
		{
			if (testEncoding)
			{
				BomEncoder::Encoding resultEncoding;
				bool resultContainsBom;
				TestEncoding(&buffer[0], buffer.Count(), resultEncoding, resultContainsBom);
				TEST_ASSERT(encoding == resultEncoding);
				TEST_ASSERT((decodedBomOffset != 0) == resultContainsBom);
			}

			DecoderStream decoderStream(memoryStream, decoder);
			StreamReader reader(decoderStream);
			WString read = reader.ReadToEnd();
			TEST_ASSERT(read == text);
		}
	};

	void TestEncodingWithEncoderDecoderStream(
		IEncoder& encoder,
		IDecoder& decoder,
		const wchar_t* text,
		const void* decodedBytes,
		vint decodedByteLength
	)
	{
		WString input = WString::Unmanaged(text);
		// encode the text
		MemoryStream memoryStream;
		{
			EncoderStream encoderStream(memoryStream, encoder);
			vint size = encoderStream.Write((void*)input.Buffer(), input.Length() * sizeof(wchar_t));
			TEST_ASSERT(size == input.Length() * sizeof(wchar_t));
		}
		memoryStream.SeekFromBegin(0);

		{
			// read the encoded data
			Array<vuint8_t> buffer;
			buffer.Resize((vint)memoryStream.Size());
			memoryStream.Read(&buffer[0], buffer.Count());
			memoryStream.SeekFromBegin(0);

			// compare the encoded data to the expected data
			TEST_ASSERT(buffer.Count() == decodedByteLength);
			TEST_ASSERT(memcmp(&buffer[0], decodedBytes, decodedByteLength) == 0);
		}

		// test the encoding and decode
		{
			DecoderStream decoderStream(memoryStream, decoder);
			wchar_t* buffer = new wchar_t[input.Length() + 1];
			vint size = decoderStream.Read(buffer, input.Length() * sizeof(wchar_t));
			TEST_ASSERT(size == input.Length() * sizeof(wchar_t));
			vint zero = decoderStream.Read(buffer, 1);
			TEST_ASSERT(zero == 0);
			buffer[input.Length()] = 0;
			WString read = WString::TakeOver(buffer, input.Length());
			TEST_ASSERT(read == text);
		}
	};

	void TestEncodingWithEncoderDecoderStreamPerByte(
		IEncoder& encoder,
		IDecoder& decoder,
		const wchar_t* text,
		const void* decodedBytes,
		vint decodedByteLength
	)
	{
		WString input = WString::Unmanaged(text);
		// encode the text
		MemoryStream memoryStream;
		{
			EncoderStream encoderStream(memoryStream, encoder);
			vint size = input.Length() * sizeof(wchar_t);
			char* bytes = (char*)input.Buffer();
			for (vint i = 0; i < size; i++)
			{
				vint written = encoderStream.Write(bytes + i, 1);
				TEST_ASSERT(written == 1);
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
			TEST_ASSERT(buffer.Count() == decodedByteLength);
			TEST_ASSERT(memcmp(&buffer[0], decodedBytes, decodedByteLength) == 0);
		}

		// test the encoding and decode
		{
			DecoderStream decoderStream(memoryStream, decoder);
			wchar_t* buffer = new wchar_t[input.Length() + 1];
			{
				vint size = input.Length() * sizeof(wchar_t);
				char* bytes = (char*)buffer;
				for (vint i = 0; i < size; i++)
				{
					vint read = decoderStream.Read(bytes + i, 1);
					TEST_ASSERT(read == 1);
				}
				vint zero = decoderStream.Read(bytes, 1);
				TEST_ASSERT(zero == 0);
			}
			buffer[input.Length()] = 0;
			WString read = WString::TakeOver(buffer, input.Length());
			TEST_ASSERT(read == text);
		}
	};

	template<typename TEncoder, typename TDecoder>
	void TestEncodingWithoutBOM(
		BomEncoder::Encoding encoding,
		const wchar_t* text,
		const void* decodedBytes,
		vint decodedByteLength,
		bool testEncoding
	)
	{
		{
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithStreamReaderWriter(encoder, decoder, encoding, text, 0, decodedBytes, decodedByteLength, testEncoding);
		}
		{
			TEST_PRINT(L"Encoder Decoder");
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithEncoderDecoderStream(encoder, decoder, text, decodedBytes, decodedByteLength);
		}
		{
			TEST_PRINT(L"Per Byte");
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithEncoderDecoderStreamPerByte(encoder, decoder, text, decodedBytes, decodedByteLength);
		}
	}

	template<typename TEncoder, typename TDecoder>
	void TestEncodingWithoutBOMWithoutTestEncoding(
		const wchar_t* text,
		const void* decodedBytes,
		vint decodedByteLength
	)
	{
		{
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithStreamReaderWriter(encoder, decoder, BomEncoder::Mbcs, text, 0, decodedBytes, decodedByteLength, false);
		}
		{
			TEST_PRINT(L"Encoder Decoder");
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithEncoderDecoderStream(encoder, decoder, text, decodedBytes, decodedByteLength);
		}
		{
			TEST_PRINT(L"Per Byte");
			TEncoder encoder;
			TDecoder decoder;
			TestEncodingWithEncoderDecoderStreamPerByte(encoder, decoder, text, decodedBytes, decodedByteLength);
		}
	}

	void TestEncodingWithBOM(
		BomEncoder::Encoding encoding,
		const wchar_t* text,
		vint decodedBomOffset,
		const void* decodedBytes,
		vint decodedByteLength,
		bool testEncoding
	)
	{
		BomEncoder encoder(encoding);
		BomDecoder decoder;
		TestEncodingWithStreamReaderWriter(encoder, decoder, encoding, text, decodedBomOffset, decodedBytes, decodedByteLength, testEncoding);
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
	const char16_t text1U16BE[] = u"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
	const char32_t text1U32[] = U"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";

	const wchar_t text2L[] = L"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char text2A[] = "ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char8_t text2U8[] = u8"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char16_t text2U16[] = u"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char16_t text2U16BE[] = u"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char32_t text2U32[] = U"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";

	encoding::SwapBytesForUtf16BE(text1U16BE, sizeof(text1U16BE) / sizeof(*text1U16BE));
	encoding::SwapBytesForUtf16BE(text2U16BE, sizeof(text2U16BE) / sizeof(*text2U16BE));

	/***********************************************************************
	Encoding
	***********************************************************************/

	TEST_CATEGORY(L"Encoding Unicode")
	{
		TEST_CASE(L"<UTF8, NO-BOM>")
		{
			return;
			TestEncodingWithoutBOM<Utf8Encoder, Utf8Decoder>(
				BomEncoder::Utf8,
				text1L,
				text1U8,
				(sizeof(text1U8) - sizeof(*text1U8)),
				true
				);
		});
		TEST_CASE(L"<UTF16, NO-BOM>")
		{
			TestEncodingWithoutBOM<Utf16Encoder, Utf16Decoder>(
				BomEncoder::Utf16,
				text1L,
				text1U16,
				(sizeof(text1U16) - sizeof(*text1U16)),
				true
				);
		});
		TEST_CASE(L"<UTF16_BE, NO-BOM>")
		{
			TestEncodingWithoutBOM<Utf16BEEncoder, Utf16BEDecoder>(
				BomEncoder::Utf16BE,
				text1L,
				text1U16BE,
				(sizeof(text1U16BE) - sizeof(*text1U16BE)),
				true
				);
		});
		TEST_CASE(L"<UTF32, NO-BOM>")
		{
			TestEncodingWithoutBOMWithoutTestEncoding<Utf32Encoder, Utf32Decoder>(
				text1L,
				text1U32,
				(sizeof(text1U32) - sizeof(*text1U32))
				);
		});

		TEST_CASE(L"<UTF8, BOM>")
		{
			TestEncodingWithBOM(
				BomEncoder::Utf8,
				text1L,
				3,
				text1U8,
				(sizeof(text1U8) - sizeof(*text1U8)),
				true
				);
		});
		TEST_CASE(L"<UTF16, BOM>")
		{
			TestEncodingWithBOM(
				BomEncoder::Utf16,
				text1L,
				2,
				text1U16,
				(sizeof(text1U16) - sizeof(*text1U16)),
				true
				);
		});
		TEST_CASE(L"<UTF16_BE, BOM>")
		{
			TestEncodingWithBOM(
				BomEncoder::Utf16BE,
				text1L,
				2,
				text1U16BE,
				(sizeof(text1U16BE) - sizeof(*text1U16BE)),
				true
				);
		});
	});

	TEST_CATEGORY(L"Encoding Ansi")
	{
		TEST_CASE(L"<MBCS, NO-BOM>")
		{
			TestEncodingWithoutBOM<MbcsEncoder, MbcsDecoder>(
				BomEncoder::Mbcs,
				text2L,
				text2A,
				(sizeof(text2A) - sizeof(*text2A)),
				false
				);
		});
		TEST_CASE(L"<UTF8, NO-BOM>")
		{
			TestEncodingWithoutBOM<Utf8Encoder, Utf8Decoder>(
				BomEncoder::Utf8,
				text2L,
				text2U8,
				(sizeof(text2U8) - sizeof(*text2U8)),
				false
				);
		});
		TEST_CASE(L"<UTF16, NO-BOM>")
		{
			TestEncodingWithoutBOM<Utf16Encoder, Utf16Decoder>(
				BomEncoder::Utf16,
				text2L,
				text2U16,
				(sizeof(text2U16) - sizeof(*text2U16)),
				false
				);
		});
		TEST_CASE(L"<UTF16_BE, NO-BOM>")
		{
			TestEncodingWithoutBOM<Utf16BEEncoder, Utf16BEDecoder>(
				BomEncoder::Utf16BE,
				text2L,
				text2U16BE,
				(sizeof(text2U16BE) - sizeof(*text2U16BE)),
				false
				);
		});
		TEST_CASE(L"<UTF32, NO-BOM>")
		{
			TestEncodingWithoutBOMWithoutTestEncoding<Utf32Encoder, Utf32Decoder>(
				text2L,
				text2U32,
				(sizeof(text2U32) - sizeof(*text2U32))
				);
		});

		TEST_CASE(L"<MBCS, BOM>")
		{
			TestEncodingWithBOM(
				BomEncoder::Mbcs,
				text2L,
				0,
				text2A,
				(sizeof(text2A) - sizeof(*text2A)),
				false
				);
		});
		TEST_CASE(L"<UTF8, BOM>")
		{
			TestEncodingWithBOM(
				BomEncoder::Utf8,
				text2L,
				3,
				text2U8,
				(sizeof(text2U8) - sizeof(*text2U8)),
				false
				);
		});
		TEST_CASE(L"<UTF16, BOM>")
		{
			TestEncodingWithBOM(
				BomEncoder::Utf16,
				text2L,
				2,
				text2U16,
				(sizeof(text2U16) - sizeof(*text2U16)),
				false
				);
		});
		TEST_CASE(L"<UTF16_BE, BOM>")
		{
			TestEncodingWithBOM(
				BomEncoder::Utf16BE,
				text2L,
				2,
				text2U16BE,
				(sizeof(text2U16BE) - sizeof(*text2U16BE)),
				false
				);
		});
	});
}