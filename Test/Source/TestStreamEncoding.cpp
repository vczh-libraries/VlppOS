#include "../../Source/Stream/Accessor.h"
#include "../../Source/Stream/EncodingStream.h"
#include "../../Source/Stream/CharFormat.h"
#include "../../Source/Locale.h"

using namespace vl;
using namespace vl::stream;
using namespace vl::collections;

TEST_FILE
{
	/***********************************************************************
	Encoding
	***********************************************************************/

	const wchar_t text1L[] = L"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
	const char8_t text1U8[] = u8"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
	const char16_t text1U16[] = u"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
	const char16_t text1U16BE[] = u"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";

	const wchar_t text2L[] = L"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char text2A[] = "ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char8_t text2U8[] = u8"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char16_t text2U16[] = u"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";
	const char16_t text2U16BE[] = u"ABCDEFG-HIJKLMN-OPQRST-UVWXYZ";

	for (const char16_t& c : text1U16BE)
	{
		vuint8_t* bs = (vuint8_t*)&c;
		vuint8_t t = bs[0];
		bs[0] = bs[1];
		bs[1] = t;
	}

	for (const char16_t& c : text2U16BE)
	{
		vuint8_t* bs = (vuint8_t*)&c;
		vuint8_t t = bs[0];
		bs[0] = bs[1];
		bs[1] = t;
	}

	auto TestEncodingInternal = [](
		IEncoder& encoder,
		IDecoder& decoder,
		BomEncoder::Encoding encoding,
		const wchar_t* text,
		vint decodedBomOffset,
		const void* decodedBytes,
		vint decodedByteLength
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
			BomEncoder::Encoding resultEncoding;
			bool resultContainsBom;
			TestEncoding(&buffer[0], buffer.Count(), resultEncoding, resultContainsBom);
			TEST_ASSERT(encoding == resultEncoding);
			TEST_ASSERT((decodedBomOffset != 0) == resultContainsBom);

			if (encoding != BomEncoder::Mbcs)
			{
				DecoderStream decoderStream(memoryStream, decoder);
				StreamReader reader(decoderStream);
				WString read = reader.ReadToEnd();
				TEST_ASSERT(read == text);
			}
		}
	};

	TEST_CATEGORY(L"Encoding Unicode")
	{
		TEST_CASE(L"<UTF8, NO-BOM>")
		{
			Utf8Encoder encoder;
			Utf8Decoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf8, text1L, 0, text1U8, (sizeof(text1U8) - sizeof(*text1U8)));
		});
		TEST_CASE(L"<UTF16, NO-BOM>")
		{
			Utf16Encoder encoder;
			Utf16Decoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf16, text1L, 0, text1U16, (sizeof(text1U16) - sizeof(*text1U16)));
		});
		TEST_CASE(L"<UTF16_BE, NO-BOM>")
		{
			Utf16BEEncoder encoder;
			Utf16BEDecoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf16BE, text1L, 0, text1U16BE, (sizeof(text1U16BE) - sizeof(*text1U16BE)));
		});

		TEST_CASE(L"<UTF8, BOM>")
		{
			BomEncoder encoder(BomEncoder::Utf8);
			BomDecoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf8, text1L, 3, text1U8, (sizeof(text1U8) - sizeof(*text1U8)));
		});
		TEST_CASE(L"<UTF16, BOM>")
		{
			BomEncoder encoder(BomEncoder::Utf16);
			BomDecoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf16, text1L, 2, text1U16, (sizeof(text1U16) - sizeof(*text1U16)));
		});
		TEST_CASE(L"<UTF16_BE, BOM>")
		{
			BomEncoder encoder(BomEncoder::Utf16BE);
			BomDecoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf16BE, text1L, 2, text1U16BE, (sizeof(text1U16BE) - sizeof(*text1U16BE)));
		});
	});

	TEST_CATEGORY(L"Encoding Ansi")
	{
		TEST_CASE(L"<MBCS, NO-BOM>")
		{
			MbcsEncoder encoder;
			MbcsDecoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Mbcs, text2L, 0, text2A, (sizeof(text2A) - sizeof(*text2A)));
		});
		TEST_CASE(L"<UTF8, NO-BOM>")
		{
			Utf8Encoder encoder;
			Utf8Decoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf8, text2L, 0, text2U8, (sizeof(text2U8) - sizeof(*text2U8)));
		});
		TEST_CASE(L"<UTF16, NO-BOM>")
		{
			Utf16Encoder encoder;
			Utf16Decoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf16, text2L, 0, text2U16, (sizeof(text2U16) - sizeof(*text2U16)));
		});
		TEST_CASE(L"<UTF16_BE, NO-BOM>")
		{
			Utf16BEEncoder encoder;
			Utf16BEDecoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf16BE, text2L, 0, text2U16BE, (sizeof(text2U16BE) - sizeof(*text2U16BE)));
		});

		TEST_CASE(L"<MBCS, BOM>")
		{
			BomEncoder encoder(BomEncoder::Mbcs);
			BomDecoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Mbcs, text2L, 0, text2A, (sizeof(text2A) - sizeof(*text2A)));
		});
		TEST_CASE(L"<UTF8, BOM>")
		{
			BomEncoder encoder(BomEncoder::Utf8);
			BomDecoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf8, text2L, 3, text2U8, (sizeof(text2U8) - sizeof(*text2U8)));
		});
		TEST_CASE(L"<UTF16, BOM>")
		{
			BomEncoder encoder(BomEncoder::Utf16);
			BomDecoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf16, text2L, 2, text2U16, (sizeof(text2U16) - sizeof(*text2U16)));
		});
		TEST_CASE(L"<UTF16_BE, BOM>")
		{
			BomEncoder encoder(BomEncoder::Utf16BE);
			BomDecoder decoder;
			TestEncodingInternal(encoder, decoder, BomEncoder::Utf16BE, text2L, 2, text2U16BE, (sizeof(text2U16BE) - sizeof(*text2U16BE)));
		});
	});
}