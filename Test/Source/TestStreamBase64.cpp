#include "../../Source/Stream/Accessor.h"
#include "../../Source/Stream/EncodingStream.h"
#include "../../Source/Encoding/Base64Encoding.h"

using namespace vl;
using namespace vl::stream;
using namespace vl::collections;

namespace TestStreamBase64_TestObjects
{
	template<size_t Bytes, size_t Chars>
	void TestBase64OnBytesPerData(const uint8_t(&bytes)[Bytes], const char8_t(&chars)[Chars])
	{
		MemoryStream memoryStream;
		{
			Utf8Base64Encoder encoder;
			EncoderStream encoderStream(memoryStream, encoder);
			vint written = encoderStream.Write((void*)bytes, Bytes);
			TEST_ASSERT(written == Bytes);
		}
		memoryStream.SeekFromBegin(0);
		{
			StreamReader_<char8_t> reader(memoryStream);
			auto base64 = reader.ReadToEnd();
			TEST_ASSERT(base64 == chars);
		}
		memoryStream.SeekFromBegin(0);
		{
			Utf8Base64Decoder decoder;
			DecoderStream decoderStream(memoryStream, decoder);
			uint8_t buffer[Bytes];
			vint read = decoderStream.Read(buffer, Bytes);
			TEST_ASSERT(read == Bytes);
			TEST_ASSERT(memcmp(buffer, bytes, Bytes) == 0);
			TEST_ASSERT(decoderStream.Read(buffer, 1) == 0);
		}
	}

	template<size_t Bytes, size_t Chars>
	void TestBase64OnBytesPerByte(const uint8_t(&bytes)[Bytes], const char8_t(&chars)[Chars])
	{
		MemoryStream memoryStream;
		{
			Utf8Base64Encoder encoder;
			EncoderStream encoderStream(memoryStream, encoder);
			for (vint i = 0; i < Bytes; i++)
			{
				vint written = encoderStream.Write((void*)(bytes + i), 1);
				TEST_ASSERT(written == 1);
			}
		}
		memoryStream.SeekFromBegin(0);
		{
			StreamReader_<char8_t> reader(memoryStream);
			auto base64 = reader.ReadToEnd();
			TEST_ASSERT(base64 == chars);
		}
		memoryStream.SeekFromBegin(0);
		{
			Utf8Base64Decoder decoder;
			DecoderStream decoderStream(memoryStream, decoder);
			for (vint i = 0; i < Bytes; i++)
			{
				uint8_t byte = 0;
				vint read = decoderStream.Read(&byte, 1);
				TEST_ASSERT(read == 1);
				TEST_ASSERT(byte == bytes[i]);
			}
			{
				uint8_t byte;
				TEST_ASSERT(decoderStream.Read(&byte, 1) == 0);
			}
		}
	}

	template<size_t Bytes, size_t Chars>
	void TestBase64OnBytes(const uint8_t(&bytes)[Bytes], const char8_t(&chars)[Chars])
	{
		TestBase64OnBytesPerData(bytes, chars);
		TestBase64OnBytesPerByte(bytes, chars);
	}

	template<size_t Bytes, size_t Chars>
	void TestBase64OnChars(const char8_t(&bytes)[Bytes], const char8_t(&chars)[Chars])
	{
		TestBase64OnBytes<Bytes - 1>(reinterpret_cast<const uint8_t(&)[Bytes - 1]>(bytes), chars);
	}
}
using namespace TestStreamBase64_TestObjects;

TEST_FILE
{
	TEST_CASE(L"Wikipedia[light w]")
	{
		TestBase64OnChars(u8"light w", u8"bGlnaHQgdw==");
	});
	TEST_CASE(L"Wikipedia[light wo]")
	{
		TestBase64OnChars(u8"light wo", u8"bGlnaHQgd28=");
	});
	TEST_CASE(L"Wikipedia[light wor]")
	{
		TestBase64OnChars(u8"light wor", u8"bGlnaHQgd29y");
	});
	TEST_CASE(L"Wikipedia[light work]")
	{
		TestBase64OnChars(u8"light work", u8"bGlnaHQgd29yaw==");
	});
	TEST_CASE(L"Wikipedia[light work.]")
	{
		TestBase64OnChars(u8"light work.", u8"bGlnaHQgd29yay4=");
	});

	TEST_CASE(L"0b01010101")
	{
		uint8_t bytes[] = { 0b01010101 };
		TestBase64OnBytes(bytes, u8"VQ==");
	});

	TEST_CASE(L"0b01010101 0b10101010")
	{
		uint8_t bytes[] = { 0b01010101,0b10101010 };
		TestBase64OnBytes(bytes, u8"Vao=");
	});

	TEST_CASE(L"0b01010101 0b10101010 0b01011010")
	{
		uint8_t bytes[] = { 0b01010101,0b10101010,0b01011010 };
		TestBase64OnBytes(bytes, u8"Vapa");
	});
}