#include "../../Source/Stream/Interfaces.h"
#include "../../Source/Stream/MemoryWrapperStream.h"
#include "../../Source/Stream/MemoryStream.h"
#include "../../Source/Stream/FileStream.h"
#include "../../Source/Stream/RecorderStream.h"
#include "../../Source/Stream/BroadcastStream.h"
#include "../../Source/Stream/CacheStream.h"

using namespace vl;
using namespace vl::stream;

extern WString GetTestOutputPath();
const vint BUFFER_SIZE = 1024;

/***********************************************************************
Shared Test Functions
***********************************************************************/

void TestClosedProperty(IStream& stream)
{
	TEST_ASSERT(stream.CanRead()==false);
	TEST_ASSERT(stream.CanWrite()==false);
	TEST_ASSERT(stream.CanPeek()==false);
	TEST_ASSERT(stream.CanSeek()==false);
	TEST_ASSERT(stream.IsLimited()==false);
	TEST_ASSERT(stream.IsAvailable()==false);
	TEST_ASSERT(stream.Position()==-1);
	TEST_ASSERT(stream.Size()==-1);
}

void TestLimitedProperty(IStream& stream, pos_t position, pos_t size)
{
	TEST_ASSERT(stream.CanRead()==true);
	TEST_ASSERT(stream.CanWrite()==true);
	TEST_ASSERT(stream.CanPeek()==true);
	TEST_ASSERT(stream.CanSeek()==true);
	TEST_ASSERT(stream.IsLimited()==true);
	TEST_ASSERT(stream.IsAvailable()==true);
	TEST_ASSERT(stream.Position()==position);
	TEST_ASSERT(stream.Size()==size);
}

void TestBidirectionalLimitedStreamWithSize15(IStream& stream)
{
	char buffer[BUFFER_SIZE];

	TestLimitedProperty(stream, 0, 15);
	TEST_ASSERT(stream.Write((void*)"vczh", 4)==4);
	TestLimitedProperty(stream, 4, 15);
	stream.Seek(-4);
	TestLimitedProperty(stream, 0, 15);
	TEST_ASSERT(stream.Peek(buffer, 4)==4);
	TEST_ASSERT(strncmp(buffer, "vczh", 4)==0);
	TestLimitedProperty(stream, 0, 15);
	memset(buffer, 0, sizeof(buffer));
	TEST_ASSERT(stream.Read(buffer, 4)==4);
	TEST_ASSERT(strncmp(buffer, "vczh", 4)==0);
	TestLimitedProperty(stream, 4, 15);

	TEST_ASSERT(stream.Write((void*)" is genius!0123456789", 21)==11);
	TestLimitedProperty(stream, 15, 15);
	stream.SeekFromEnd(7);
	TestLimitedProperty(stream, 8, 15);
	TEST_ASSERT(stream.Read(buffer, 100)==7);
	TEST_ASSERT(strncmp(buffer, "genius!", 7)==0);
	TestLimitedProperty(stream, 15, 15);

	stream.SeekFromBegin(100);
	TestLimitedProperty(stream, 15, 15);
	stream.SeekFromEnd(100);
	TestLimitedProperty(stream, 0, 15);
	stream.Seek(100);
	TestLimitedProperty(stream, 15, 15);
	stream.Seek(-100);
	TestLimitedProperty(stream, 0, 15);

	stream.Seek(4);
	TEST_ASSERT(stream.Read(buffer, 4)==4);
	TEST_ASSERT(strncmp(buffer, " is ", 4)==0);
}

void TestUnlimitedProperty(IStream& stream, pos_t position, pos_t size)
{
	TEST_ASSERT(stream.CanRead()==true);
	TEST_ASSERT(stream.CanWrite()==true);
	TEST_ASSERT(stream.CanPeek()==true);
	TEST_ASSERT(stream.CanSeek()==true);
	TEST_ASSERT(stream.IsLimited()==false);
	TEST_ASSERT(stream.IsAvailable()==true);
	TEST_ASSERT(stream.Position()==position);
	TEST_ASSERT(stream.Size()==size);
}

void TestBidirectionalUnlimitedStream(IStream& stream)
{
	char buffer[BUFFER_SIZE];

	TestUnlimitedProperty(stream, 0, 0);
	TEST_ASSERT(stream.Write((void*)"vczh", 4)==4);
	TestUnlimitedProperty(stream, 4, 4);
	stream.Seek(-4);
	TestUnlimitedProperty(stream, 0, 4);
	TEST_ASSERT(stream.Peek(buffer, 4)==4);
	TEST_ASSERT(strncmp(buffer, "vczh", 4)==0);
	TestUnlimitedProperty(stream, 0, 4);
	memset(buffer, 0, sizeof(buffer));
	TEST_ASSERT(stream.Read(buffer, 4)==4);
	TEST_ASSERT(strncmp(buffer, "vczh", 4)==0);
	TestUnlimitedProperty(stream, 4, 4);

	TEST_ASSERT(stream.Write((void*)" is genius!0123456789", 11)==11);
	TestUnlimitedProperty(stream, 15, 15);
	stream.SeekFromEnd(7);
	TestUnlimitedProperty(stream, 8, 15);
	TEST_ASSERT(stream.Read(buffer, 100)==7);
	TEST_ASSERT(strncmp(buffer, "genius!", 7)==0);
	TestUnlimitedProperty(stream, 15, 15);

	stream.SeekFromBegin(100);
	TestUnlimitedProperty(stream, 15, 15);
	stream.SeekFromEnd(100);
	TestUnlimitedProperty(stream, 0, 15);
	stream.Seek(100);
	TestUnlimitedProperty(stream, 15, 15);
	stream.Seek(-100);
	TestUnlimitedProperty(stream, 0, 15);

	stream.Seek(4);
	TEST_ASSERT(stream.Read(buffer, 4)==4);
	TEST_ASSERT(strncmp(buffer, " is ", 4)==0);
}

void TestReadonlySeekableProperty(IStream& stream, pos_t position, pos_t size)
{
	TEST_ASSERT(stream.CanRead()==true);
	TEST_ASSERT(stream.CanWrite()==false);
	TEST_ASSERT(stream.CanPeek()==true);
	TEST_ASSERT(stream.CanSeek()==true);
	TEST_ASSERT(stream.IsLimited()==true);
	TEST_ASSERT(stream.IsAvailable()==true);
	TEST_ASSERT(stream.Position()==position);
	TEST_ASSERT(stream.Size()==size);
}

void TestReadonlylSeekableStreamWithSize15(IStream& stream)
{
	char buffer[BUFFER_SIZE];

	TestReadonlySeekableProperty(stream, 0, 15);
	stream.SeekFromEnd(7);
	TestReadonlySeekableProperty(stream, 8, 15);
	TEST_ASSERT(stream.Read(buffer, 100)==7);
	TEST_ASSERT(strncmp(buffer, "genius!", 7)==0);
	TestReadonlySeekableProperty(stream, 15, 15);

	stream.SeekFromBegin(100);
	TestReadonlySeekableProperty(stream, 15, 15);
	stream.SeekFromEnd(100);
	TestReadonlySeekableProperty(stream, 0, 15);
	TEST_ASSERT(stream.Read(buffer, 4)==4);
	TEST_ASSERT(strncmp(buffer, "vczh", 4)==0);
	TestReadonlySeekableProperty(stream, 4, 15);
	stream.Seek(100);
	TestReadonlySeekableProperty(stream, 15, 15);
	stream.Seek(-100);
	TestReadonlySeekableProperty(stream, 0, 15);

	stream.Seek(4);
	TEST_ASSERT(stream.Read(buffer, 4)==4);
	TEST_ASSERT(strncmp(buffer, " is ", 4)==0);
}

void TestWriteonlySeekableProperty(IStream& stream, pos_t position, pos_t size)
{
	TEST_ASSERT(stream.CanRead()==false);
	TEST_ASSERT(stream.CanWrite()==true);
	TEST_ASSERT(stream.CanPeek()==false);
	TEST_ASSERT(stream.CanSeek()==true);
	TEST_ASSERT(stream.IsLimited()==false);
	TEST_ASSERT(stream.IsAvailable()==true);
	TEST_ASSERT(stream.Position()==position);
	TEST_ASSERT(stream.Size()==size);
}

void TestWriteonlySeekableStream(IStream& stream)
{
	TestWriteonlySeekableProperty(stream, 0, 0);
	TEST_ASSERT(stream.Write((void*)"genius!", 7)==7);
	TestWriteonlySeekableProperty(stream, 7, 7);
	stream.Seek(-7);
	TestWriteonlySeekableProperty(stream, 0, 7);
	TEST_ASSERT(stream.Write((void*)"vczh is genius!", 15)==15);
	TestWriteonlySeekableProperty(stream, 15, 15);

	stream.SeekFromBegin(100);
	TestWriteonlySeekableProperty(stream, 15, 15);
	stream.SeekFromEnd(100);
	TestWriteonlySeekableProperty(stream, 0, 15);
	stream.Seek(100);
	TestWriteonlySeekableProperty(stream, 15, 15);
	stream.Seek(-100);
	TestWriteonlySeekableProperty(stream, 0, 15);
}

void TestReadonlyUnseekableProperty(IStream& stream, pos_t position, pos_t size, bool limited)
{
	TEST_ASSERT(stream.CanRead()==true);
	TEST_ASSERT(stream.CanWrite()==false);
	TEST_ASSERT(stream.CanPeek()==false);
	TEST_ASSERT(stream.CanSeek()==false);
	TEST_ASSERT(stream.IsLimited()==limited);
	TEST_ASSERT(stream.IsAvailable()==true);
	TEST_ASSERT(stream.Position()==position);
	TEST_ASSERT(stream.Size()==size);
}

void TestReadonlyUnseekableStreamWithSize15(IStream& stream, bool limited)
{
	char buffer[BUFFER_SIZE];

	TestReadonlyUnseekableProperty(stream, 0, 15, limited);
	TEST_ASSERT(stream.Read(buffer, 8)==8);
	TEST_ASSERT(strncmp(buffer, "vczh is ", 8)==0);
	TestReadonlyUnseekableProperty(stream, 8, 15, limited);
	TEST_ASSERT(stream.Read(buffer, 100)==7);
	TEST_ASSERT(strncmp(buffer, "genius!", 7)==0);
	TestReadonlyUnseekableProperty(stream, 15, 15, limited);
}

void TestWriteonlyUnseekableProperty(IStream& stream, pos_t position, pos_t size, bool limited)
{
	TEST_ASSERT(stream.CanRead()==false);
	TEST_ASSERT(stream.CanWrite()==true);
	TEST_ASSERT(stream.CanPeek()==false);
	TEST_ASSERT(stream.CanSeek()==false);
	TEST_ASSERT(stream.IsLimited()==limited);
	TEST_ASSERT(stream.IsAvailable()==true);
	TEST_ASSERT(stream.Position()==position);
	TEST_ASSERT(stream.Size()==size);
}

void TestWriteonlyUnseekableStream(IStream& stream, bool limited)
{
	TestWriteonlyUnseekableProperty(stream, 0, 0, limited);
	TEST_ASSERT(stream.Write((void*)"vczh is ", 8)==8);
	TestWriteonlyUnseekableProperty(stream, 8, 8, limited);
	TEST_ASSERT(stream.Write((void*)"genius!", 7)==7);
	TestWriteonlyUnseekableProperty(stream, 15, 15, limited);
}

TEST_FILE
{
	/***********************************************************************
	Normal Streams
	***********************************************************************/

	TEST_CASE(L"Test MemoryWrapperStream")
	{
		char buffer[BUFFER_SIZE] = {0};
		MemoryWrapperStream stream(buffer, 15);
		TestBidirectionalLimitedStreamWithSize15(stream);
		stream.Close();
		TestClosedProperty(stream);
		TEST_ASSERT(strncmp(buffer, "vczh is genius!", 15) == 0);
	});

	TEST_CASE(L"Test MemoryStream")
	{
		MemoryStream stream;
		TestBidirectionalUnlimitedStream(stream);
		stream.Close();
		TestClosedProperty(stream);
	});

	TEST_CASE(L"Test FileStream")
	{
		FileStream destroyer(GetTestOutputPath() + L"TestFile.ReadWrite.txt", FileStream::WriteOnly);
		TestWriteonlySeekableProperty(destroyer, 0, 0);
		destroyer.Close();
		TestClosedProperty(destroyer);

		FileStream tryRead(GetTestOutputPath() + L"TestFile.ReadWrite.txt", FileStream::ReadOnly);
		TestReadonlySeekableProperty(tryRead, 0, 0);
		tryRead.Close();
		TestClosedProperty(tryRead);

		FileStream w(GetTestOutputPath() + L"TestFile.ReadWrite.txt", FileStream::WriteOnly);
		TestWriteonlySeekableStream(w);
		w.Close();
		TestClosedProperty(w);

		FileStream r(GetTestOutputPath() + L"TestFile.ReadWrite.txt", FileStream::ReadOnly);
		TestReadonlylSeekableStreamWithSize15(r);
		r.Close();
		TestClosedProperty(r);

		FileStream rw(GetTestOutputPath() + L"TestFile.ReadWrite.txt", FileStream::ReadWrite);
		TestBidirectionalUnlimitedStream(rw);
		rw.Close();
		TestClosedProperty(rw);
	});

	TEST_CASE(L"Test RecorderStream")
	{
		char reading[] = "vczh is genius!";
		char writing[BUFFER_SIZE];
		MemoryWrapperStream readingStream(reading, 15);
		MemoryWrapperStream writingStream(writing, 15);
		RecorderStream recorder(readingStream, writingStream);
		TestReadonlyUnseekableStreamWithSize15(recorder, true);
		TEST_ASSERT(strncmp(writing, "vczh is genius!", 15) == 0);
		recorder.Close();
		TestClosedProperty(recorder);
	});

	TEST_CASE(L"Test BroadcastStream")
	{
		char buffer1[BUFFER_SIZE];
		char buffer2[BUFFER_SIZE];
		MemoryWrapperStream target1(buffer1, 15);
		MemoryWrapperStream target2(buffer2, 15);
		BroadcastStream stream;
		stream.Targets().Add(&target1);
		stream.Targets().Add(&target2);
		TestWriteonlyUnseekableStream(stream, false);
		TEST_ASSERT(strncmp(buffer1, "vczh is genius!", 15) == 0);
		TEST_ASSERT(strncmp(buffer2, "vczh is genius!", 15) == 0);
		stream.Close();
		TestClosedProperty(stream);
	});

	/***********************************************************************
	CacheStream
	***********************************************************************/

	TEST_CASE(L"Test CacheStream with readonly unseekable stream")
	{
		char reading[] = "vczh is genius!";
		char writing[BUFFER_SIZE];
		MemoryWrapperStream readingStream(reading, 15);
		MemoryWrapperStream writingStream(writing, 15);
		RecorderStream recorder(readingStream, writingStream);
		CacheStream cache(recorder, 4);
		TestReadonlyUnseekableStreamWithSize15(cache, true);
		cache.Close();
		TestClosedProperty(cache);
		TEST_ASSERT(strncmp(writing, "vczh is genius!", 15) == 0);
	});

	TEST_CASE(L"Test CacheStream with writeonly unseekable stream")
	{
		char buffer[BUFFER_SIZE];
		MemoryWrapperStream target(buffer, 15);
		BroadcastStream broadcast;
		broadcast.Targets().Add(&target);
		CacheStream cache(broadcast, 4);
		TestWriteonlyUnseekableStream(cache, false);
		cache.Close();
		TestClosedProperty(cache);
		TEST_ASSERT(strncmp(buffer, "vczh is genius!", 15) == 0);
	});

	TEST_CASE(L"Test CacheStream with seekable stream")
	{
		FileStream w(GetTestOutputPath() + L"TestFile.ReadWrite.txt", FileStream::WriteOnly);
		CacheStream cw(w, 4);
		TestWriteonlySeekableStream(cw);
		cw.Close();
		TestClosedProperty(cw);
		w.Close();
		TestClosedProperty(w);

		FileStream r(GetTestOutputPath() + L"TestFile.ReadWrite.txt", FileStream::ReadOnly);
		CacheStream cr(r, 4);
		TestReadonlylSeekableStreamWithSize15(cr);
		cr.Close();
		TestClosedProperty(cr);
		r.Close();
		TestClosedProperty(r);
	});

	TEST_CASE(L"Test CacheStream with bidirectional limited stream")
	{
		char buffer[BUFFER_SIZE];
		MemoryWrapperStream memory(buffer, 15);
		CacheStream cache(memory, 4);
		TestBidirectionalLimitedStreamWithSize15(cache);
		cache.Close();
		TestClosedProperty(cache);
		TEST_ASSERT(strncmp(buffer, "vczh is genius!", 15) == 0);
	});

	TEST_CASE(L"Test CacheStream with bidirectional unlimited stream")
	{
		MemoryStream memory;
		CacheStream cache(memory, 4);
		TestBidirectionalUnlimitedStream(cache);
		cache.Close();
		TestClosedProperty(cache);
	});

	TEST_CASE(L"Test CacheStream")
	{
		char buffer[BUFFER_SIZE];

		MemoryStream memory;
		CacheStream cache(memory, 4);
		TestUnlimitedProperty(cache, 0, 0);

		TEST_ASSERT(cache.Write((void*)"vcz", 3) == 3);
		TestUnlimitedProperty(cache, 3, 3);
		cache.Seek(-2);
		TestUnlimitedProperty(cache, 1, 3);
		TEST_ASSERT(cache.Read(buffer, 4) == 2);
		TEST_ASSERT(strncmp(buffer, "cz", 2) == 0);
		TestUnlimitedProperty(cache, 3, 3);

		TEST_ASSERT(cache.Write((void*)"h ", 2) == 2);
		TestUnlimitedProperty(cache, 5, 5);
		cache.Seek(-5);
		TestUnlimitedProperty(cache, 0, 5);
		TEST_ASSERT(cache.Write((void*)"V", 1) == 1);
		TestUnlimitedProperty(cache, 1, 5);
		cache.SeekFromEnd(1);
		TestUnlimitedProperty(cache, 4, 5);
		TEST_ASSERT(cache.Read(buffer, 4) == 1);
		TEST_ASSERT(strncmp(buffer, " ", 1) == 0);
		TestUnlimitedProperty(cache, 5, 5);

		TEST_ASSERT(cache.Write((void*)"is", 2) == 2);
		TestUnlimitedProperty(cache, 7, 7);
		TEST_ASSERT(cache.Write((void*)" genius!", 8) == 8);
		TestUnlimitedProperty(cache, 15, 15);
		cache.Seek(-8);
		TEST_ASSERT(cache.Read(buffer, 1) == 1);
		TEST_ASSERT(cache.Read(buffer + 1, 1) == 1);
		TEST_ASSERT(cache.Read(buffer + 2, 1) == 1);
		TEST_ASSERT(cache.Read(buffer + 3, 1) == 1);
		TEST_ASSERT(strncmp(buffer, " gen", 4) == 0);

		cache.SeekFromBegin(0);
		TestUnlimitedProperty(cache, 0, 15);
		TEST_ASSERT(cache.Read(buffer, 14) == 14);
		TEST_ASSERT(strncmp(buffer, "Vczh is genius", 14) == 0);

		cache.Close();
		TestClosedProperty(cache);
		memory.SeekFromBegin(0);
		TEST_ASSERT(memory.Read(buffer, 15) == 15);
		TEST_ASSERT(strncmp(buffer, "Vczh is genius!", 15) == 0);
	});
}