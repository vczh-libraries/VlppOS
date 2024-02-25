#include "../../Source/FileSystem.h"
#include "../../Source/Locale.h"
#include "../../Source/Stream/FileStream.h"
#include "../../Source/Encoding/CharFormat/CharFormat.h"
#include "../../Source/Stream/Accessor.h"
#include "../../Source/Stream/EncodingStream.h"
#include "../../Source/Threading.h"

using namespace vl;
using namespace vl::filesystem;
using namespace vl::collections;
using namespace vl::stream;

extern WString GetTestOutputPath();

void ClearTestFolders()
{
	TEST_CASE(L"Ensure clearing test folder")
	{
		FilePath path = GetTestOutputPath();
		TEST_ASSERT(path.IsFolder());

		Folder folder(GetTestOutputPath() + L"FileSystem");
		auto folderPath = folder.GetFilePath().GetFullPath();
#if defined VCZH_MSVC
		TEST_ASSERT(folderPath[1] == L':');
		TEST_ASSERT(INVLOC.FindLast(folderPath, L"\\FileSystem", Locale::None).key == folderPath.Length() - 11);
#elif defined VCZH_GCC
		TEST_ASSERT(folderPath[0] == L'/');
		TEST_ASSERT(INVLOC.FindLast(folderPath, L"/FileSystem", Locale::None).key == folderPath.Length() - 11);
#endif

		if (folder.Exists())
		{
			TEST_ASSERT(folder.Delete(true) == true);
			Thread::Sleep(100);
		}
		TEST_ASSERT(folder.Create(false) == true);
		TEST_ASSERT(folder.Exists() == true);
		TEST_ASSERT(folder.Create(false) == false);
	});
}

TEST_FILE
{
	TEST_CATEGORY(L"File Paths")
	{
	#if defined VCZH_MSVC
		ClearTestFolders();
		TEST_CASE(L"Comparison")
		{
			{
				FilePath a, b;
				TEST_ASSERT((a == b) == true);
				TEST_ASSERT((a != b) == false);
				TEST_ASSERT((a < b) == false);
				TEST_ASSERT((a <= b) == true);
				TEST_ASSERT((a > b) == false);
				TEST_ASSERT((a >= b) == true);
			}
			{
				FilePath a(L"C:\\"), b(L"D::\\");
				TEST_ASSERT((a == b) == false);
				TEST_ASSERT((a != b) == true);
				TEST_ASSERT((a < b) == true);
				TEST_ASSERT((a <= b) == true);
				TEST_ASSERT((a > b) == false);
				TEST_ASSERT((a >= b) == false);
			}
		});
		TEST_CASE(L"<Empty>")
		{
			FilePath p;
			TEST_ASSERT(p.IsFile() == false);
			TEST_ASSERT(p.IsFolder() == false);
			TEST_ASSERT(p.IsRoot() == true);
			TEST_ASSERT(p.GetFullPath() == L"");
			TEST_ASSERT(p.GetName() == L"");
			TEST_ASSERT(p.GetFolder().GetFullPath() == L"");
		});
		TEST_CASE(L"C:\\")
		{
			FilePath p = L"C:\\";
			TEST_ASSERT(p.IsFile() == false);
			TEST_ASSERT(p.IsFolder() == true);
			TEST_ASSERT(p.IsRoot() == false);
			TEST_ASSERT(p.GetFullPath() == L"C:");
			TEST_ASSERT(p.GetName() == L"C:");
			TEST_ASSERT(p.GetFolder().GetFullPath() == L"");
		});
		TEST_CASE(L"C:\\Windows\\")
		{
			FilePath p = L"C:\\Windows\\";
			TEST_ASSERT(p.IsFile() == false);
			TEST_ASSERT(p.IsFolder() == true);
			TEST_ASSERT(p.IsRoot() == false);
			TEST_ASSERT(p.GetFullPath() == L"C:\\Windows");
			TEST_ASSERT(p.GetName() == L"Windows");
			TEST_ASSERT(p.GetFolder().GetFullPath() == L"C:");
		});
		TEST_CASE(L"C:\\Windows\\Explorer.exe")
		{
			FilePath p = L"C:\\Windows\\Explorer.exe";
			TEST_ASSERT(p.IsFile() == true);
			TEST_ASSERT(p.IsFolder() == false);
			TEST_ASSERT(p.IsRoot() == false);
			TEST_ASSERT(p.GetFullPath() == L"C:\\Windows\\explorer.exe");
			TEST_ASSERT(p.GetName() == L"explorer.exe");
			TEST_ASSERT(p.GetFolder().GetFullPath() == L"C:\\Windows");
		});
		TEST_CASE(L"C:\\Windows\\vczh.txt")
		{
			FilePath p = L"C:\\Windows\\vczh.txt";
			TEST_ASSERT(p.IsFile() == false);
			TEST_ASSERT(p.IsFolder() == false);
			TEST_ASSERT(p.IsRoot() == false);
			TEST_ASSERT(p.GetFullPath() == L"C:\\Windows\\vczh.txt");
		});
		TEST_CASE(L"C:\\Windows / Explorer.exe")
		{
			FilePath p = L"C:\\Windows";
			auto q = p / L"Explorer.exe";
			TEST_ASSERT(q.GetFullPath() == L"C:\\Windows\\explorer.exe");
		});
		TEST_CASE(L"C:\\Program Files / ..\\Windows\\.\\Explorer.exe")
		{
			FilePath p = L"C:\\Program Files";
			auto q = p / L"..\\Windows\\.\\Explorer.exe";
			TEST_ASSERT(q.GetFullPath() == L"C:\\Windows\\explorer.exe");
		});
		TEST_CASE(L"C:\\Program Files -> C:\\Windows\\Explorer.exe")
		{
			FilePath p = L"C:\\Program Files";
			FilePath q = L"C:\\Windows\\Explorer.exe";
			auto r = p.GetRelativePathFor(q);
			TEST_ASSERT(r == L"..\\Windows\\explorer.exe");
		});
		TEST_CASE(L"C:\\Program Files -> C:\\Windows\\Explorer.exe")
		{
			FilePath p = L"C:\\Program Files\\vczh.txt";
			FilePath q = L"C:\\Windows\\Explorer.exe";
			auto r = p.GetRelativePathFor(q);
			TEST_ASSERT(r == L"..\\Windows\\explorer.exe");
		});
		TEST_CASE(L"C:\\Program Files -> D:\\Windows\\Explorer.exe")
		{
			FilePath p = L"C:\\Program Files\\vczh.txt";
			FilePath q = L"D:\\Windows\\Explorer.exe";
			auto r = p.GetRelativePathFor(q);
			TEST_ASSERT(r == L"D:\\Windows\\Explorer.exe");
		});
	#elif defined VCZH_GCC
		ClearTestFolders();
		TEST_CASE(L"<Empty>")
		{
			FilePath p;
			TEST_ASSERT(p.IsFile() == false);
			TEST_ASSERT(p.IsFolder() == true);
			TEST_ASSERT(p.IsRoot() == true);
			TEST_ASSERT(p.GetFullPath() == L"/");
		});
		TEST_CASE(L"/")
		{
			FilePath p = L"/";
			TEST_ASSERT(p.IsFile() == false);
			TEST_ASSERT(p.IsFolder() == true);
			TEST_ASSERT(p.IsRoot() == true);
			TEST_ASSERT(p.GetFullPath() == L"/");
		});
		TEST_CASE(L"/bin/")
		{
			FilePath p = L"/bin/";
			TEST_ASSERT(p.IsFile() == false);
			TEST_ASSERT(p.IsFolder() == true);
			TEST_ASSERT(p.IsRoot() == false);
			TEST_ASSERT(p.GetFullPath() == L"/bin");
			TEST_ASSERT(p.GetName() == L"bin");
			TEST_ASSERT(p.GetFolder().GetFullPath() == L"/");
		});
		TEST_CASE(L"/bin/ls")
		{
			FilePath p = L"/bin/ls";
			TEST_ASSERT(p.IsFile() == true);
			TEST_ASSERT(p.IsFolder() == false);
			TEST_ASSERT(p.IsRoot() == false);
			TEST_ASSERT(p.GetFullPath() == L"/bin/ls");
			TEST_ASSERT(p.GetName() == L"ls");
			TEST_ASSERT(p.GetFolder().GetFullPath() == L"/bin");
		});
		TEST_CASE(L"/bin/vczh.txt")
		{
			FilePath p = L"/bin/vczh.txt";
			TEST_ASSERT(p.IsFile() == false);
			TEST_ASSERT(p.IsFolder() == false);
			TEST_ASSERT(p.IsRoot() == false);
			TEST_ASSERT(p.GetFullPath() == L"/bin/vczh.txt");
		});
		TEST_CASE(L"/bin / ls")
		{
			FilePath p = L"/bin";
			auto q = p / L"ls";
			TEST_ASSERT(q.GetFullPath() == L"/bin/ls");
		});
		TEST_CASE(L"/usr/bin/ / ../../bin/./ls")
		{
			FilePath p = L"/usr/bin/";
			auto q = p / L"../../bin/./ls";
			TEST_ASSERT(q.GetFullPath() == L"/bin/ls");
		});
		TEST_CASE(L"/usr/bin/ -> /bin/ls")
		{
			FilePath p = L"/usr/bin/";
			FilePath q = L"/bin/ls";
			auto r = p.GetRelativePathFor(q);
			TEST_ASSERT(r == L"../../bin/ls");
		});
		TEST_CASE(L"/usr/bin/cc -> /bin/ls")
		{
			FilePath p = L"/usr/bin/cc";
			FilePath q = L"/bin/ls";
			auto r = p.GetRelativePathFor(q);
			TEST_ASSERT(r == L"../../bin/ls");
		});
		TEST_CASE(L"Throws on /../../bin")
		{
			TEST_EXCEPTION({ FilePath p = L"/../../bin"; }, ArgumentException, [](const ArgumentException&) {});
		});
	#endif
	});

#ifdef VCZH_MSVC
	TEST_CASE(L"Enumerate drives")
	{
		Folder folder;
		List<Folder> folders;
		List<File> files;
		TEST_ASSERT(folder.GetFilePath().IsRoot());
		TEST_ASSERT(folder.GetFolders(folders));
		TEST_ASSERT(folder.GetFiles(files));
		TEST_ASSERT(folders.Count() > 0);
		TEST_ASSERT(files.Count() == 0);

		TEST_ASSERT(folders[0].Exists());
		TEST_ASSERT(folders[0].GetFilePath().IsFolder());
		auto drive = folders[0].GetFilePath().GetFullPath();
		TEST_ASSERT(drive.Length() == 2);
		TEST_ASSERT(drive[1] == L':');
	});
#endif

	ClearTestFolders();
	TEST_CASE(L"Create and delete folders")
	{
		FilePath folder = GetTestOutputPath() + L"FileSystem";

		File a = folder / L"A/vczh.txt";
		File b = folder / L"A/B/C/vczh.txt";
		Folder c = folder / L"A";
		Folder d = folder / L"A/B/C";

		TEST_ASSERT(a.Exists() == false);
		TEST_ASSERT(b.Exists() == false);
		TEST_ASSERT(c.Exists() == false);
		TEST_ASSERT(d.Exists() == false);
		TEST_ASSERT(a.WriteAllText(L"A") == false);
		TEST_ASSERT(b.WriteAllText(L"B") == false);
		TEST_ASSERT(a.ReadAllTextByBom() == L"");
		TEST_ASSERT(b.ReadAllTextByBom() == L"");

		TEST_ASSERT(c.Create(false) == true);
		TEST_ASSERT(d.Create(false) == false);
		TEST_ASSERT(d.Create(true) == true);
		TEST_ASSERT(c.Create(true) == false);
		TEST_ASSERT(d.Create(true) == false);

		TEST_ASSERT(a.Exists() == false);
		TEST_ASSERT(b.Exists() == false);
		TEST_ASSERT(c.Exists() == true);
		TEST_ASSERT(d.Exists() == true);
		TEST_ASSERT(a.WriteAllText(L"A") == true);
		TEST_ASSERT(b.WriteAllText(L"B") == true);
		TEST_ASSERT(a.ReadAllTextByBom() == L"A");
		TEST_ASSERT(b.ReadAllTextByBom() == L"B");
		TEST_ASSERT(a.Exists() == true);
		TEST_ASSERT(b.Exists() == true);

		TEST_ASSERT(c.Delete(false) == false);
		TEST_ASSERT(d.Delete(false) == false);
		TEST_ASSERT(c.Delete(true) == true);
		TEST_ASSERT(d.Delete(true) == false);

		TEST_ASSERT(a.Exists() == false);
		TEST_ASSERT(b.Exists() == false);
		TEST_ASSERT(c.Exists() == false);
		TEST_ASSERT(d.Exists() == false);
		TEST_ASSERT(a.WriteAllText(L"A") == false);
		TEST_ASSERT(b.WriteAllText(L"B") == false);
		TEST_ASSERT(a.ReadAllTextByBom() == L"");
		TEST_ASSERT(b.ReadAllTextByBom() == L"");
	});

	ClearTestFolders();
	TEST_CASE(L"Enumerate folders and files")
	{
		FilePath folder = GetTestOutputPath() + L"FileSystem";

		File a = folder / L"a.txt";
		File b = folder / L"b.txt";
		Folder c = folder / L"c";
		Folder d = folder / L"d";
		List<File> files;
		List<Folder> folders;

		TEST_ASSERT(a.Exists() == false);
		TEST_ASSERT(b.Exists() == false);
		TEST_ASSERT(c.Exists() == false);
		TEST_ASSERT(d.Exists() == false);
		files.Clear();
		folders.Clear();
		TEST_ASSERT(Folder(folder).GetFiles(files) == true && files.Count() == 0);
		TEST_ASSERT(Folder(folder).GetFolders(folders) == true && folders.Count() == 0);

		TEST_ASSERT(a.WriteAllText(L"") == true);
		TEST_ASSERT(b.WriteAllText(L"") == true);
		TEST_ASSERT(c.Create(false) == true);
		TEST_ASSERT(d.Create(false) == true);

		TEST_ASSERT(a.Exists() == true);
		TEST_ASSERT(b.Exists() == true);
		TEST_ASSERT(c.Exists() == true);
		TEST_ASSERT(d.Exists() == true);
		files.Clear();
		folders.Clear();
		TEST_ASSERT(Folder(folder).GetFiles(files) == true && files.Count() == 2 && ((files[0].GetFilePath().GetName() == L"a.txt" && files[1].GetFilePath().GetName() == L"b.txt") || (files[0].GetFilePath().GetName() == L"b.txt" && files[1].GetFilePath().GetName() == L"a.txt")));
		TEST_ASSERT(Folder(folder).GetFolders(folders) == true && folders.Count() == 2 && ((folders[0].GetFilePath().GetName() == L"c" && folders[1].GetFilePath().GetName() == L"d") || (folders[0].GetFilePath().GetName() == L"d" && folders[1].GetFilePath().GetName() == L"c")));

		TEST_ASSERT(a.Delete() == true);
		TEST_ASSERT(b.Delete() == true);
		TEST_ASSERT(c.Delete(false) == true);
		TEST_ASSERT(d.Delete(false) == true);

		TEST_ASSERT(a.Exists() == false);
		TEST_ASSERT(b.Exists() == false);
		TEST_ASSERT(c.Exists() == false);
		TEST_ASSERT(d.Exists() == false);
		files.Clear();
		folders.Clear();
		TEST_ASSERT(Folder(folder).GetFiles(files) == true && files.Count() == 0);
		TEST_ASSERT(Folder(folder).GetFolders(folders) == true && folders.Count() == 0);
	});

	ClearTestFolders();
	TEST_CASE(L"Rename folders and files")
	{
		FilePath folder = GetTestOutputPath() + L"FileSystem";

		File a = folder / L"a.txt";
		File b = folder / L"d/b.txt";
		File x = folder / L"y/x.txt";
		Folder c = folder / L"c";
		Folder d = folder / L"d";
		Folder y = folder / L"y";

		TEST_ASSERT(c.Create(false) == true);
		TEST_ASSERT(d.Create(false) == true);
		TEST_ASSERT(a.WriteAllText(L"A") == true);
		TEST_ASSERT(b.WriteAllText(L"B") == true);

		TEST_ASSERT(a.Exists() == true);
		TEST_ASSERT(b.Exists() == true);
		TEST_ASSERT(x.Exists() == false);
		TEST_ASSERT(c.Exists() == true);
		TEST_ASSERT(d.Exists() == true);
		TEST_ASSERT(y.Exists() == false);
		TEST_ASSERT(a.ReadAllTextByBom() == L"A");
		TEST_ASSERT(b.ReadAllTextByBom() == L"B");
		TEST_ASSERT(x.ReadAllTextByBom() == L"");

		TEST_ASSERT(d.Rename(L"y"));
		TEST_ASSERT(a.Exists() == true);
		TEST_ASSERT(b.Exists() == false);
		TEST_ASSERT(x.Exists() == false);
		TEST_ASSERT(c.Exists() == true);
		TEST_ASSERT(d.Exists() == false);
		TEST_ASSERT(y.Exists() == true);
		TEST_ASSERT(a.ReadAllTextByBom() == L"A");
		TEST_ASSERT(b.ReadAllTextByBom() == L"");
		TEST_ASSERT(x.ReadAllTextByBom() == L"");

		TEST_ASSERT(File(folder / L"y/b.txt").Rename(L"x.txt"));
		TEST_ASSERT(a.Exists() == true);
		TEST_ASSERT(b.Exists() == false);
		TEST_ASSERT(x.Exists() == true);
		TEST_ASSERT(c.Exists() == true);
		TEST_ASSERT(d.Exists() == false);
		TEST_ASSERT(y.Exists() == true);
		TEST_ASSERT(a.ReadAllTextByBom() == L"A");
		TEST_ASSERT(b.ReadAllTextByBom() == L"");
		TEST_ASSERT(x.ReadAllTextByBom() == L"B");
	});

	ClearTestFolders();
	TEST_CASE(L"Read and write text files")
	{
		FilePath folder = GetTestOutputPath() + L"FileSystem";
		File file = folder / L"vczh.txt";

		WString text;
		List<WString> lines;
		TEST_ASSERT(file.Exists() == false);
		TEST_ASSERT(file.ReadAllTextByBom() == L"");
		TEST_ASSERT(file.ReadAllTextByBom(text) == false && text == L"");
		TEST_ASSERT(file.ReadAllLinesByBom(lines) == false && lines.Count() == 0);

		text = L"";
		lines.Clear();
		file.WriteAllText(L"Vczh is a genius!");
		TEST_ASSERT(file.Exists() == true);
		TEST_ASSERT(file.ReadAllTextByBom() == L"Vczh is a genius!");
		TEST_ASSERT(file.ReadAllTextByBom(text) == true && text == L"Vczh is a genius!");
		TEST_ASSERT(file.ReadAllLinesByBom(lines) == true && lines.Count() == 1 && lines[0] == L"Vczh is a genius!");

		text = L"";
		lines.Clear();
		file.WriteAllText(L"Vczh is a genius!", true, BomEncoder::Mbcs);
		TEST_ASSERT(file.Exists() == true);
		TEST_ASSERT(file.ReadAllTextByBom() == L"Vczh is a genius!");
		TEST_ASSERT(file.ReadAllTextByBom(text) == true && text == L"Vczh is a genius!");
		TEST_ASSERT(file.ReadAllLinesByBom(lines) == true && lines.Count() == 1 && lines[0] == L"Vczh is a genius!");

		text = L"";
		lines.Clear();
		file.WriteAllText(L"Vczh is a genius!", true, BomEncoder::Utf8);
		TEST_ASSERT(file.Exists() == true);
		TEST_ASSERT(file.ReadAllTextByBom() == L"Vczh is a genius!");
		TEST_ASSERT(file.ReadAllTextByBom(text) == true && text == L"Vczh is a genius!");
		TEST_ASSERT(file.ReadAllLinesByBom(lines) == true && lines.Count() == 1 && lines[0] == L"Vczh is a genius!");

		text = L"";
		lines.Clear();
		file.WriteAllText(L"Vczh is a genius!", true, BomEncoder::Utf16);
		TEST_ASSERT(file.Exists() == true);
		TEST_ASSERT(file.ReadAllTextByBom() == L"Vczh is a genius!");
		TEST_ASSERT(file.ReadAllTextByBom(text) == true && text == L"Vczh is a genius!");
		TEST_ASSERT(file.ReadAllLinesByBom(lines) == true && lines.Count() == 1 && lines[0] == L"Vczh is a genius!");

		text = L"";
		lines.Clear();
		file.WriteAllText(L"Vczh is a genius!", true, BomEncoder::Utf16BE);
		TEST_ASSERT(file.Exists() == true);
		TEST_ASSERT(file.ReadAllTextByBom() == L"Vczh is a genius!");
		TEST_ASSERT(file.ReadAllTextByBom(text) == true && text == L"Vczh is a genius!");
		TEST_ASSERT(file.ReadAllLinesByBom(lines) == true && lines.Count() == 1 && lines[0] == L"Vczh is a genius!");

		text = L"";
		lines.Clear();
		TEST_ASSERT(file.Delete());
		TEST_ASSERT(file.Exists() == false);
		TEST_ASSERT(file.ReadAllTextByBom() == L"");
		TEST_ASSERT(file.ReadAllTextByBom(text) == false && text == L"");
		TEST_ASSERT(file.ReadAllLinesByBom(lines) == false && lines.Count() == 0);
	});

	auto TestFastAccessFilesWithEncodingTestingInternal = [](const WString& encodingName, IEncoder& encoder, IDecoder& decoder, BomEncoder::Encoding encoding, bool containsBom)
	{
		TEST_CASE(encodingName)
		{
			WString filePath = GetTestOutputPath() + L"TestFile.ReadWrite.txt";
			const wchar_t* text =
				encoding == BomEncoder::Mbcs
				? L"Vczh is genius!@我是天才"
				: L"𩰪㦲𦰗𠀼 𣂕𣴑𣱳𦁚 Vczh is genius!@我是天才"
				;

			{
				FileStream fileStream(filePath, FileStream::WriteOnly);
				{
					EncoderStream encoderStream(fileStream, encoder);
					StreamWriter writer(encoderStream);
					writer.WriteString(text);
				}
			}

			File file(filePath);
			TEST_ASSERT(file.Exists());

			WString fileText;
			BomEncoder::Encoding fileEncoding = BomEncoder::Mbcs;
			bool fileContainsBom = false;
			TEST_ASSERT(file.ReadAllTextWithEncodingTesting(fileText, fileEncoding, fileContainsBom));

			TEST_ASSERT(fileText == text);
			TEST_ASSERT(fileEncoding == encoding);
			TEST_ASSERT(fileContainsBom == containsBom);
		});
	};

	TEST_CATEGORY(L"Read and write files in encoding")
	{
		if (Locale::SystemDefault().GetName() == L"zh-CN")
		{
			MbcsEncoder encoder;
			MbcsDecoder decoder;
			TestFastAccessFilesWithEncodingTestingInternal(L"<MBCS, NO-BOM>", encoder, decoder, BomEncoder::Mbcs, false);
		}
		{
			Utf8Encoder encoder;
			Utf8Decoder decoder;
			TestFastAccessFilesWithEncodingTestingInternal(L"<UTF8, NO-BOM>", encoder, decoder, BomEncoder::Utf8, false);
		}
		{
			Utf16Encoder encoder;
			Utf16Decoder decoder;
			TestFastAccessFilesWithEncodingTestingInternal(L"<UTF16, NO-BOM>", encoder, decoder, BomEncoder::Utf16, false);
		}
		{
			Utf16BEEncoder encoder;
			Utf16BEDecoder decoder;
			TestFastAccessFilesWithEncodingTestingInternal(L"<UTF16_BE, NO-BOM>", encoder, decoder, BomEncoder::Utf16BE, false);
		}
		if (Locale::SystemDefault().GetName() == L"zh-CN")
		{
			BomEncoder encoder(BomEncoder::Mbcs);
			BomDecoder decoder;
			TestFastAccessFilesWithEncodingTestingInternal(L"<MBCS, BOM>", encoder, decoder, BomEncoder::Mbcs, false);
		}
		{
			BomEncoder encoder(BomEncoder::Utf8);
			BomDecoder decoder;
			TestFastAccessFilesWithEncodingTestingInternal(L"<UTF8, BOM>", encoder, decoder, BomEncoder::Utf8, true);
		}
		{
			BomEncoder encoder(BomEncoder::Utf16);
			BomDecoder decoder;
			TestFastAccessFilesWithEncodingTestingInternal(L"<UTF16, BOM>", encoder, decoder, BomEncoder::Utf16, true);
		}
		{
			BomEncoder encoder(BomEncoder::Utf16BE);
			BomDecoder decoder;
			TestFastAccessFilesWithEncodingTestingInternal(L"<UTF16_BE, BOM>", encoder, decoder, BomEncoder::Utf16BE, true);
		}
	});
}