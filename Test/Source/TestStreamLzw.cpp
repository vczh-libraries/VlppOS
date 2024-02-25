#include "../../Source/Stream/FileStream.h"
#include "../../Source/Stream/MemoryStream.h"
#include "../../Source/Stream/EncodingStream.h"
#include "../../Source/Encoding/LzwEncoding.h"

using namespace vl;
using namespace vl::stream;
using namespace vl::collections;

extern WString GetTestOutputPath();

TEST_FILE
{
	/***********************************************************************
	Lzw
	***********************************************************************/

	auto TestLzwEncodingWithEncoderAndDecoder = [](const char* input, LzwEncoder& encoder, LzwDecoder& decoder)
	{
		MemoryStream stream;
		vint size = strlen(input);
		{
			EncoderStream encoderStream(stream, encoder);
			vint size = strlen(input);
			TEST_ASSERT(encoderStream.Write((void*)input, size) == size);
		}
		stream.SeekFromBegin(0);
		unittest::UnitTest::PrintMessage(L"    [" + atow(input) + L"]", unittest::UnitTest::MessageKind::Info);
		unittest::UnitTest::PrintMessage(L"    " + itow(size) + L" -> " + i64tow(stream.Size()), unittest::UnitTest::MessageKind::Info);
		{
			Array<char> output(size + 1);
			DecoderStream decoderStream(stream, decoder);
			TEST_ASSERT(decoderStream.Read(&output[0], size) == size);
			TEST_ASSERT(decoderStream.Read(&output[0], size) == 0);
			output[size] = 0;
			TEST_ASSERT(strcmp(input, &output[0]) == 0);
		}
	};

	auto TestLzwEncodingDefault = [&](const char* input)
	{
		LzwEncoder encoder;
		LzwDecoder decoder;
		TestLzwEncodingWithEncoderAndDecoder(input, encoder, decoder);
	};

	auto TestLzwEncodingPrepared = [&](const char* input)
	{
		bool existingBytes[256] = { 0 };
		const char* current = input;
		while (vuint8_t c = (vuint8_t)*current++)
		{
			existingBytes[c] = true;
		}

		LzwEncoder encoder(existingBytes);
		LzwDecoder decoder(existingBytes);
		TestLzwEncodingWithEncoderAndDecoder(input, encoder, decoder);
	};

	TEST_CASE(L"Test Lzw Encoding")
	{
		const char* buffer[] =
		{
			"",
			"0000000000000000000000000000000000000000",
			"Vczh is genius!Vczh is genius!Vczh is genius!",
		};

		for (vint i = 0; i < sizeof(buffer) / sizeof(*buffer); i++)
		{
			TestLzwEncodingDefault(buffer[i]);
			TestLzwEncodingPrepared(buffer[i]);
		}
	});

#if defined VCZH_MSVC && defined NDEBUG

	auto Copy = [](IStream& dst, IStream& src, Array<vuint8_t>& buffer, vint totalSize)
	{
		vint BufferSize = buffer.Count();
		while (true)
		{
			vint size = src.Read(&buffer[0], BufferSize);
			if (size == 0)
			{
				break;
			}
			dst.Write(&buffer[0], size);
		}
	};

	TEST_CASE(L"Test Lzw Performance")
	{
		const vint BufferSize = 33554432;
		Array<vuint8_t> buffer(BufferSize);
		MemoryStream compressedStream(BufferSize), decompressedStream(BufferSize);
		unittest::UnitTest::PrintMessage(L"    Reading UnitTest.pdb ...", unittest::UnitTest::MessageKind::Info);
		{
			FileStream fileStream(GetTestOutputPath() + L"../UnitTest/Release/UnitTest.pdb", FileStream::ReadOnly);
			Copy(decompressedStream, fileStream, buffer, (vint)fileStream.Size());
		}

		decompressedStream.SeekFromBegin(0);
		vint totalSize = (vint)decompressedStream.Size();

		unittest::UnitTest::PrintMessage(L"    Compressing UnitTest.pdb ...", unittest::UnitTest::MessageKind::Info);
		{
			DateTime begin = DateTime::LocalTime();

			LzwEncoder encoder;
			EncoderStream encoderStream(compressedStream, encoder);
			Copy(encoderStream, decompressedStream, buffer, totalSize);

			DateTime end = DateTime::LocalTime();

			double time = (end.osMilliseconds - begin.osMilliseconds) / 1000.0;
			unittest::UnitTest::PrintMessage(L"    Time elasped: " + ftow(time) + L" seconds", unittest::UnitTest::MessageKind::Info);
			unittest::UnitTest::PrintMessage(L"    Performance: " + ftow(totalSize / time / (1 << 20)) + L" MB/s", unittest::UnitTest::MessageKind::Info);
		}

		compressedStream.SeekFromBegin(0);
		unittest::UnitTest::PrintMessage(L"    " + i64tow(totalSize) + L" -> " + i64tow(compressedStream.Size()), unittest::UnitTest::MessageKind::Info);

		unittest::UnitTest::PrintMessage(L"    Decompressing UnitTest.pdb ...", unittest::UnitTest::MessageKind::Info);
		{
			DateTime begin = DateTime::LocalTime();

			LzwDecoder decoder;
			DecoderStream decoderStream(compressedStream, decoder);
			Copy(decompressedStream, decoderStream, buffer, totalSize);

			DateTime end = DateTime::LocalTime();
			double time = (end.osMilliseconds - begin.osMilliseconds) / 1000.0;
			unittest::UnitTest::PrintMessage(L"    Time elasped: " + ftow(time) + L" seconds", unittest::UnitTest::MessageKind::Info);
			unittest::UnitTest::PrintMessage(L"    Performance: " + ftow(totalSize / time / (1 << 20)) + L" MB/s", unittest::UnitTest::MessageKind::Info);
		}
	});
#endif
}