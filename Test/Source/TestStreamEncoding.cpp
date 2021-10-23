#include <string.h>
#include "../../Source/Stream/Interfaces.h"
#include "../../Source/Stream/MemoryWrapperStream.h"
#include "../../Source/Stream/MemoryStream.h"
#include "../../Source/Stream/FileStream.h"
#include "../../Source/Stream/RecorderStream.h"
#include "../../Source/Stream/BroadcastStream.h"
#include "../../Source/Stream/CacheStream.h"
#include "../../Source/Stream/Accessor.h"
#include "../../Source/Stream/CharFormat.h"
#include "../../Source/Stream/CompressionStream.h"
#include "../../Source/Locale.h"

using namespace vl;
using namespace vl::stream;
using namespace vl::collections;

TEST_FILE
{
	/***********************************************************************
	Encoding
	***********************************************************************/

	auto TestEncodingInternal = [](const WString& encodingName, IEncoder& encoder, IDecoder& decoder, BomEncoder::Encoding encoding, bool containsBom)
	{
		TEST_CASE(encodingName)
		{
			const wchar_t* text = L"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才";
			MemoryStream memoryStream;
			{
				EncoderStream encoderStream(memoryStream, encoder);
				StreamWriter writer(encoderStream);
				writer.WriteString(text);
			}
			memoryStream.SeekFromBegin(0);
			Array<vuint8_t> buffer;
			buffer.Resize((vint)memoryStream.Size());
			memoryStream.Read(&buffer[0], buffer.Count());
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

			BomEncoder::Encoding resultEncoding;
			bool resultContainsBom;
			TestEncoding(&buffer[0], buffer.Count(), resultEncoding, resultContainsBom);
			TEST_ASSERT(encoding == resultEncoding);
			TEST_ASSERT(containsBom == resultContainsBom);

			if (encoding != BomEncoder::Mbcs)
			{
				memoryStream.SeekFromBegin(0);
				DecoderStream decoderStream(memoryStream, decoder);
				StreamReader reader(decoderStream);
				WString read = reader.ReadToEnd();
				TEST_ASSERT(read == text);
			}
		});
	};

	TEST_CATEGORY(L"Encoding")
	{
		if (Locale::SystemDefault().GetName() == L"zh-CN")
		{
			MbcsEncoder encoder;
			MbcsDecoder decoder;
			TestEncodingInternal(L"<MBCS, NO-BOM>", encoder, decoder, BomEncoder::Mbcs, false);
		}
		{
			Utf8Encoder encoder;
			Utf8Decoder decoder;
			TestEncodingInternal(L"<UTF8, NO-BOM>", encoder, decoder, BomEncoder::Utf8, false);
		}
		{
			Utf16Encoder encoder;
			Utf16Decoder decoder;
			TestEncodingInternal(L"<UTF16, NO-BOM>", encoder, decoder, BomEncoder::Utf16, false);
		}
		{
			Utf16BEEncoder encoder;
			Utf16BEDecoder decoder;
			TestEncodingInternal(L"<UTF16_BE, NO-BOM>", encoder, decoder, BomEncoder::Utf16BE, false);
		}
		if (Locale::SystemDefault().GetName() == L"zh-CN")
		{
			BomEncoder encoder(BomEncoder::Mbcs);
			BomDecoder decoder;
			TestEncodingInternal(L"<MBCS, BOM>", encoder, decoder, BomEncoder::Mbcs, false);
		}
		{
			BomEncoder encoder(BomEncoder::Utf8);
			BomDecoder decoder;
			TestEncodingInternal(L"<UTF8, BOM>", encoder, decoder, BomEncoder::Utf8, true);
		}
		{
			BomEncoder encoder(BomEncoder::Utf16);
			BomDecoder decoder;
			TestEncodingInternal(L"<UTF16, BOM>", encoder, decoder, BomEncoder::Utf16, true);
		}
		{
			BomEncoder encoder(BomEncoder::Utf16BE);
			BomDecoder decoder;
			TestEncodingInternal(L"<UTF16_BE, BOM>", encoder, decoder, BomEncoder::Utf16BE, true);
		}
	});
}