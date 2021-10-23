#include "../../Source/Stream/MemoryWrapperStream.h"
#include "../../Source/Stream/Accessor.h"

using namespace vl;
using namespace vl::stream;

const vint BUFFER_SIZE = 1024;

TEST_FILE
{
	/***********************************************************************
	StringReader
	***********************************************************************/

	TEST_CASE(L"Test StringReader")
	{
		const wchar_t text[] = L"1:Vczh is genius!\r\n2:Vczh is genius!\r\n3:Vczh is genius!\r\n4:Vczh is genius!";
		StringReader reader(text);

		TEST_ASSERT(reader.ReadChar() == L'1');
		TEST_ASSERT(reader.ReadString(5) == L":Vczh");
		TEST_ASSERT(reader.ReadLine() == L" is genius!");
		TEST_ASSERT(reader.ReadLine() == L"2:Vczh is genius!");
		TEST_ASSERT(reader.ReadToEnd() == L"3:Vczh is genius!\r\n4:Vczh is genius!");
	});

	TEST_CASE(L"Test StringReader with ending CRLF")
	{
		const wchar_t text[] = L"1:Vczh is genius!\r\n2:Vczh is genius!!\r\n3:Vczh is genius!!!\r\n4:Vczh is genius!!!!\r\n";
		const wchar_t* lines[] = {L"1:Vczh is genius!", L"2:Vczh is genius!!", L"3:Vczh is genius!!!", L"4:Vczh is genius!!!!",L""};
		StringReader reader(text);
		vint index = 0;

		while (index < sizeof(lines) / sizeof(*lines))
		{
			TEST_ASSERT(reader.IsEnd() == false);
			TEST_ASSERT(reader.ReadLine() == lines[index++]);
		}
		TEST_ASSERT(reader.IsEnd() == true);
	});

	TEST_CASE(L"Test StringReader without ending CRLF")
	{
		const wchar_t text[] = L"1:Vczh is genius!\r\n2:Vczh is genius!!\r\n3:Vczh is genius!!!\r\n4:Vczh is genius!!!!";
		const wchar_t* lines[] = {L"1:Vczh is genius!", L"2:Vczh is genius!!", L"3:Vczh is genius!!!", L"4:Vczh is genius!!!!"};
		StringReader reader(text);
		vint index = 0;

		while (index < sizeof(lines) / sizeof(*lines))
		{
			TEST_ASSERT(reader.IsEnd() == false);
			TEST_ASSERT(reader.ReadLine() == lines[index++]);
		}
		TEST_ASSERT(reader.IsEnd() == true);
	});

	/***********************************************************************
	StreamReader
	***********************************************************************/

	TEST_CASE(L"Test StreamReader")
	{
		wchar_t text[] = L"1:Vczh is genius!\r\n2:Vczh is genius!\r\n3:Vczh is genius!\r\n4:Vczh is genius!";
		MemoryWrapperStream stream(text, sizeof(text) - sizeof(*text));
		StreamReader reader(stream);

		TEST_ASSERT(reader.ReadChar() == L'1');
		TEST_ASSERT(reader.ReadString(5) == L":Vczh");
		TEST_ASSERT(reader.ReadLine() == L" is genius!");
		TEST_ASSERT(reader.ReadLine() == L"2:Vczh is genius!");
		TEST_ASSERT(reader.ReadToEnd() == L"3:Vczh is genius!\r\n4:Vczh is genius!");
	});

	TEST_CASE(L"Test StreamReader with ending CRLF")
	{
		wchar_t text[] = L"1:Vczh is genius!\r\n2:Vczh is genius!!\r\n3:Vczh is genius!!!\r\n4:Vczh is genius!!!!\r\n";
		const wchar_t* lines[] = {L"1:Vczh is genius!", L"2:Vczh is genius!!", L"3:Vczh is genius!!!", L"4:Vczh is genius!!!!",L""};
		MemoryWrapperStream stream(text, sizeof(text) - sizeof(*text));
		StreamReader reader(stream);
		vint index = 0;

		while (index < sizeof(lines) / sizeof(*lines))
		{
			TEST_ASSERT(reader.IsEnd() == false);
			TEST_ASSERT(reader.ReadLine() == lines[index++]);
		}
		TEST_ASSERT(reader.IsEnd() == true);
	});

	TEST_CASE(L"Test StreamReader without ending CRLF")
	{
		wchar_t text[] = L"1:Vczh is genius!\r\n2:Vczh is genius!!\r\n3:Vczh is genius!!!\r\n4:Vczh is genius!!!!";
		const wchar_t* lines[] = {L"1:Vczh is genius!", L"2:Vczh is genius!!", L"3:Vczh is genius!!!", L"4:Vczh is genius!!!!"};
		MemoryWrapperStream stream(text, sizeof(text) - sizeof(*text));
		StreamReader reader(stream);
		vint index = 0;

		while (index < sizeof(lines) / sizeof(*lines))
		{
			TEST_ASSERT(reader.IsEnd() == false);
			TEST_ASSERT(reader.ReadLine() == lines[index++]);
		}
		TEST_ASSERT(reader.IsEnd() == true);
	});

	/***********************************************************************
	StreamWriter
	***********************************************************************/

	TEST_CASE(L"Test StreamWriter")
	{
		wchar_t text[BUFFER_SIZE] = {0};
		MemoryWrapperStream stream(text, sizeof(text) - sizeof(*text));
		StreamWriter writer(stream);

		writer.WriteChar(L'1');
		writer.WriteChar(L':');
		writer.WriteChar(L'V');
		writer.WriteChar(L'c');
		writer.WriteChar(L'z');
		writer.WriteChar(L'h');
		writer.WriteChar(L' ');
		writer.WriteChar(L'i');
		writer.WriteChar(L's');
		writer.WriteChar(L' ');
		writer.WriteChar(L'g');
		writer.WriteChar(L'e');
		writer.WriteChar(L'n');
		writer.WriteChar(L'i');
		writer.WriteChar(L'u');
		writer.WriteChar(L's');
		writer.WriteChar(L'!');
		writer.WriteString(L"");
		writer.WriteString(L"\r\n2:Vczh is genius!");
		writer.WriteString(WString(L""));
		writer.WriteLine(L"");
		writer.WriteLine(L"3:Vczh is genius!");
		writer.WriteLine(WString(L"4:Vczh is genius!"));

		wchar_t baseline[] = L"1:Vczh is genius!\r\n2:Vczh is genius!\r\n3:Vczh is genius!\r\n4:Vczh is genius!\r\n";
		TEST_ASSERT(wcscmp(text, baseline) == 0);
	});
}