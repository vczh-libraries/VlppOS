/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "FileSystem.h"
#include "Locale.h"
#include "Stream/FileStream.h"
#include "Stream/MemoryWrapperStream.h"
#include "Stream/Accessor.h"
#include "Stream/EncodingStream.h"

namespace vl
{
	namespace filesystem
	{
		using namespace collections;
		using namespace stream;

		// ReadDirectoryChangesW

/***********************************************************************
FilePath
***********************************************************************/

		void FilePath::NormalizeDelimiters(collections::Array<wchar_t>& buffer)
		{
			for (vint i = 0; i < buffer.Count(); i++)
			{
				if (buffer[i] == L'\\' || buffer[i] == L'/')
				{
					buffer[i] = Delimiter;
				}
			}
		}

		void FilePath::TrimLastDelimiter(WString& fullPath)
		{
			if (fullPath != L"/" && fullPath.Length() > 0 && fullPath[fullPath.Length() - 1] == Delimiter)
			{
				fullPath = fullPath.Left(fullPath.Length() - 1);
			}
		}

		FilePath::FilePath()
		{
			Initialize();
		}

		FilePath::FilePath(const WString& _filePath)
			:fullPath(_filePath)
		{
			Initialize();
		}

		FilePath::FilePath(const wchar_t* _filePath)
			:fullPath(_filePath)
		{
			Initialize();
		}

		FilePath::FilePath(const FilePath& _filePath)
			:fullPath(_filePath.fullPath)
		{
		}

		FilePath FilePath::operator/(const WString& relativePath)const
		{
			if (IsRoot())
			{
				return relativePath;
			}
			else
			{
				return fullPath + L"/" + relativePath;
			}
		}

		WString FilePath::GetName()const
		{
			auto delimiter = WString::FromChar(Delimiter);
			auto index = INVLOC.FindLast(fullPath, delimiter, Locale::None);
			if (index.key == -1) return fullPath;
			return fullPath.Right(fullPath.Length() - index.key - 1);
		}

		FilePath FilePath::GetFolder()const
		{
			auto delimiter = WString::FromChar(Delimiter);
			auto index = INVLOC.FindLast(fullPath, delimiter, Locale::None);
			if (index.key == -1) return FilePath();
			return fullPath.Left(index.key);
		}

		WString FilePath::GetFullPath()const
		{
			return fullPath;
		}

		void FilePath::GetPathComponents(WString path, collections::List<WString>& components)
		{
			WString pathRemaining = path;
			auto delimiter = WString::FromChar(Delimiter);

			components.Clear();

			while (true)
			{
				auto index = INVLOC.FindFirst(pathRemaining, delimiter, Locale::None);
				if (index.key == -1)
					break;

				if (index.key != 0)
					components.Add(pathRemaining.Left(index.key));
				else
				{
#if defined VCZH_MSVC
					if (pathRemaining.Length() >= 2 && pathRemaining[1] == Delimiter)
					{
						// Windows UNC Path starting with "\\"
						// components[0] will be L"\\"
						components.Add(L"\\");
						index.value++;
					}
#elif defined VCZH_GCC
					// Unix absolute path starting with "/"
					// components[0] will be L"/"
					components.Add(delimiter);
#endif
				}

				pathRemaining = pathRemaining.Right(pathRemaining.Length() - (index.key + index.value));
			}

			if (pathRemaining.Length() != 0)
			{
				components.Add(pathRemaining);
			}
		}

		WString FilePath::ComponentsToPath(const collections::List<WString>& components)
		{
			WString result;
			auto delimiter = WString::FromChar(Delimiter);

			int i = 0;
			
#if defined VCZH_MSVC
			// For Windows, if first component is "\\" then it is an UNC path
			if(components.Count() > 0 && components[0] == L"\\")
			{
				result += delimiter;
				i++;
			}
#elif defined VCZH_GCC
			// For Unix-like OSes, if first component is "/" then take it as absolute path
			if(components.Count() > 0 && components[0] == delimiter)
			{
				result += delimiter;
				i++;
			}
#endif

			for(; i < components.Count(); i++)
			{
				result += components[i];
				if(i + 1 < components.Count())
					result += delimiter;
			}

			return result;
		}

/***********************************************************************
File
***********************************************************************/
		
		File::File(const FilePath& _filePath)
			:filePath(_filePath)
		{
		}

		const FilePath& File::GetFilePath()const
		{
			return filePath;
		}

		bool File::ReadAllTextWithEncodingTesting(WString& text, stream::BomEncoder::Encoding& encoding, bool& containsBom)
		{
			Array<unsigned char> buffer;
			{
				FileStream fileStream(filePath.GetFullPath(), FileStream::ReadOnly);
				if (!fileStream.IsAvailable()) return false;
				if (fileStream.Size() == 0)
				{
					text = L"";
					encoding = BomEncoder::Mbcs;
					containsBom = false;
					return true;
				}

				buffer.Resize((vint)fileStream.Size());
				vint count = fileStream.Read(&buffer[0], buffer.Count());
				CHECK_ERROR(count == buffer.Count(), L"vl::filesystem::File::ReadAllTextWithEncodingTesting(WString&, BomEncoder::Encoding&, bool&)#Failed to read the whole file.");
			}
			TestEncoding(&buffer[0], buffer.Count(), encoding, containsBom);

			MemoryWrapperStream memoryStream(&buffer[0], buffer.Count());
			if (containsBom)
			{
				BomDecoder decoder;
				DecoderStream decoderStream(memoryStream, decoder);
				StreamReader reader(decoderStream);
				text = reader.ReadToEnd();
			}
			else
			{
				switch (encoding)
				{
				case BomEncoder::Utf8:
					{
						Utf8Decoder decoder;
						DecoderStream decoderStream(memoryStream, decoder);
						StreamReader reader(decoderStream);
						text = reader.ReadToEnd();
					}
					break;
				case BomEncoder::Utf16:
					{
						Utf16Decoder decoder;
						DecoderStream decoderStream(memoryStream, decoder);
						StreamReader reader(decoderStream);
						text = reader.ReadToEnd();
					}
					break;
				case BomEncoder::Utf16BE:
					{
						Utf16BEDecoder decoder;
						DecoderStream decoderStream(memoryStream, decoder);
						StreamReader reader(decoderStream);
						text = reader.ReadToEnd();
					}
					break;
				default:
					{
						MbcsDecoder decoder;
						DecoderStream decoderStream(memoryStream, decoder);
						StreamReader reader(decoderStream);
						text = reader.ReadToEnd();
					}
				}
			}
			return true;
		}

		WString File::ReadAllTextByBom()const
		{
			WString text;
			ReadAllTextByBom(text);
			return text;
		}

		bool File::ReadAllTextByBom(WString& text)const
		{
			FileStream fileStream(filePath.GetFullPath(), FileStream::ReadOnly);
			if (!fileStream.IsAvailable()) return false;
			BomDecoder decoder;
			DecoderStream decoderStream(fileStream, decoder);
			StreamReader reader(decoderStream);
			text = reader.ReadToEnd();
			return true;
		}

		bool File::ReadAllLinesByBom(collections::List<WString>& lines)const
		{
			FileStream fileStream(filePath.GetFullPath(), FileStream::ReadOnly);
			if (!fileStream.IsAvailable()) return false;
			BomDecoder decoder;
			DecoderStream decoderStream(fileStream, decoder);
			StreamReader reader(decoderStream);
			while (!reader.IsEnd())
			{
				lines.Add(reader.ReadLine());
			}
			return true;
		}

		bool File::WriteAllText(const WString& text, bool bom, stream::BomEncoder::Encoding encoding)
		{
			FileStream fileStream(filePath.GetFullPath(), FileStream::WriteOnly);
			if (!fileStream.IsAvailable()) return false;
			
			IEncoder* encoder = nullptr;
			if (bom)
			{
				encoder = new BomEncoder(encoding);
			}
			else switch (encoding)
			{
			case BomEncoder::Utf8:
				encoder = new Utf8Encoder;
				break;
			case BomEncoder::Utf16:
				encoder = new Utf16Encoder;
				break;
			case BomEncoder::Utf16BE:
				encoder = new Utf16BEEncoder;
				break;
			default:
				encoder = new MbcsEncoder;
				break;
			}

			{
				EncoderStream encoderStream(fileStream, *encoder);
				StreamWriter writer(encoderStream);
				writer.WriteString(text);
			}
			delete encoder;
			return true;
		}

		bool File::WriteAllLines(collections::List<WString>& lines, bool bom, stream::BomEncoder::Encoding encoding)
		{
			FileStream fileStream(filePath.GetFullPath(), FileStream::WriteOnly);
			if (!fileStream.IsAvailable()) return false;
			
			IEncoder* encoder = nullptr;
			if (bom)
			{
				encoder = new BomEncoder(encoding);
			}
			else switch (encoding)
			{
			case BomEncoder::Utf8:
				encoder = new Utf8Encoder;
				break;
			case BomEncoder::Utf16:
				encoder = new Utf16Encoder;
				break;
			case BomEncoder::Utf16BE:
				encoder = new Utf16BEEncoder;
				break;
			default:
				encoder = new MbcsEncoder;
				break;
			}

			{
				EncoderStream encoderStream(fileStream, *encoder);
				StreamWriter writer(encoderStream);
				for (auto line : lines)
				{
					writer.WriteLine(line);
				}
			}
			delete encoder;
			return true;
		}

		bool File::Exists()const
		{
			return filePath.IsFile();
		}

/***********************************************************************
Folder
***********************************************************************/
		
		Folder::Folder(const FilePath& _filePath)
			:filePath(_filePath)
		{
		}

		const FilePath& Folder::GetFilePath()const
		{
			return filePath;
		}

		bool Folder::Exists()const
		{
			return filePath.IsFolder();
		}

		bool Folder::Create(bool recursively)const
		{
			if (recursively)
			{
				auto folder = filePath.GetFolder();
				if (folder.IsFile()) return false;
				if (folder.IsFolder()) return Create(false);
				return Folder(folder).Create(true) && Create(false);
			}
			else
			{
				return CreateNonRecursively();
			}
		}

		bool Folder::Delete(bool recursively)const
		{
			if (!Exists()) return false;
			
			if (recursively)
			{
				List<Folder> folders;
				GetFolders(folders);
				for (auto folder : folders)
				{
					if (!folder.Delete(true)) return false;
				}
				
				List<File> files;
				GetFiles(files);
				for (auto file : files)
				{
					if (!file.Delete()) return false;
				}

				return Delete(false);
			}
			return DeleteNonRecursively();
		}
	}
}
