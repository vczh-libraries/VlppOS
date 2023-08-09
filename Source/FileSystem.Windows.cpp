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
#include <Windows.h>
#include <Shlwapi.h>

#ifndef VCZH_MSVC
static_assert(false, "Do not build this file for non-Windows applications.");
#endif

#pragma comment(lib, "Shlwapi.lib")

namespace vl
{
	namespace filesystem
	{
		using namespace collections;
		using namespace stream;

/***********************************************************************
FilePath
***********************************************************************/

		void FilePath::Initialize()
		{
			{
				Array<wchar_t> buffer(fullPath.Length() + 1);
				wcscpy_s(&buffer[0], fullPath.Length() + 1, fullPath.Buffer());
				NormalizeDelimiters(buffer);
				fullPath = &buffer[0];
			}

			if (fullPath != L"")
			{
				if (fullPath.Length() < 2 || fullPath[1] != L':')
				{
					wchar_t buffer[MAX_PATH + 1] = { 0 };
					auto result = GetCurrentDirectory(sizeof(buffer) / sizeof(*buffer), buffer);
					if (result > MAX_PATH + 1 || result == 0)
					{
						throw ArgumentException(L"Failed to call GetCurrentDirectory.", L"vl::filesystem::FilePath::Initialize", L"");
					}
					fullPath = WString(buffer) + L"\\" + fullPath;
				}
				{
					wchar_t buffer[MAX_PATH + 1] = { 0 };
					if (fullPath.Length() == 2 && fullPath[1] == L':')
					{
						fullPath += L"\\";
					}
					auto result = GetFullPathName(fullPath.Buffer(), sizeof(buffer) / sizeof(*buffer), buffer, NULL);
					if (result > MAX_PATH + 1 || result == 0)
					{
						throw ArgumentException(L"The path is illegal.", L"vl::filesystem::FilePath::FilePath", L"_filePath");
					}

					{
						wchar_t shortPath[MAX_PATH + 1];
						wchar_t longPath[MAX_PATH + 1];
						if (GetShortPathName(buffer, shortPath, MAX_PATH) > 0)
						{
							if (GetLongPathName(shortPath, longPath, MAX_PATH) > 0)
							{
								memcpy(buffer, longPath, sizeof(buffer));
							}
						}
					}
					fullPath = buffer;
				}
			}

			TrimLastDelimiter(fullPath);
		}

		bool FilePath::IsFile()const
		{
			WIN32_FILE_ATTRIBUTE_DATA info;
			BOOL result = GetFileAttributesEx(fullPath.Buffer(), GetFileExInfoStandard, &info);
			if (!result) return false;
			return (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}

		bool FilePath::IsFolder()const
		{
			WIN32_FILE_ATTRIBUTE_DATA info;
			BOOL result = GetFileAttributesEx(fullPath.Buffer(), GetFileExInfoStandard, &info);
			if (!result) return false;
			return (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		}

		bool FilePath::IsRoot()const
		{
			return fullPath == L"";
		}

		WString FilePath::GetRelativePathFor(const FilePath& _filePath) const
		{
			if (fullPath.Length() == 0 || _filePath.fullPath.Length() == 0 || fullPath[0] != _filePath.fullPath[0])
			{
				return _filePath.fullPath;
			}

			wchar_t buffer[MAX_PATH + 1] = { 0 };
			PathRelativePathTo(
				buffer,
				fullPath.Buffer(),
				(IsFolder() ? FILE_ATTRIBUTE_DIRECTORY : 0),
				_filePath.fullPath.Buffer(),
				(_filePath.IsFolder() ? FILE_ATTRIBUTE_DIRECTORY : 0)
			);
			return buffer;
		}

/***********************************************************************
File
***********************************************************************/

		bool File::Delete()const
		{
			return DeleteFile(filePath.GetFullPath().Buffer()) != 0;
		}

		bool File::Rename(const WString& newName)const
		{
			WString oldFileName = filePath.GetFullPath();
			WString newFileName = (filePath.GetFolder() / newName).GetFullPath();
			return MoveFile(oldFileName.Buffer(), newFileName.Buffer()) != 0;
		}

/***********************************************************************
Folder
***********************************************************************/

		bool Folder::GetFolders(collections::List<Folder>& folders)const
		{
			if (filePath.IsRoot())
			{
				auto bufferSize = GetLogicalDriveStrings(0, nullptr);
				if (bufferSize > 0)
				{
					Array<wchar_t> buffer(bufferSize);
					if (GetLogicalDriveStrings((DWORD)buffer.Count(), &buffer[0]) > 0)
					{
						auto begin = &buffer[0];
						auto end = begin + buffer.Count();
						while (begin < end && *begin)
						{
							WString driveString = begin;
							begin += driveString.Length() + 1;
							folders.Add(Folder(FilePath(driveString)));
						}
						return true;
					}
				}
				return false;
			}
			else
			{
				if (!Exists()) return false;
				WIN32_FIND_DATA findData;
				HANDLE findHandle = INVALID_HANDLE_VALUE;

				while (true)
				{
					if (findHandle == INVALID_HANDLE_VALUE)
					{
						WString searchPath = (filePath / L"*").GetFullPath();
						findHandle = FindFirstFile(searchPath.Buffer(), &findData);
						if (findHandle == INVALID_HANDLE_VALUE)
						{
							break;
						}
					}
					else
					{
						BOOL result = FindNextFile(findHandle, &findData);
						if (result == 0)
						{
							FindClose(findHandle);
							break;
						}
					}

					if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					{
						if (wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0)
						{
							folders.Add(Folder(filePath / findData.cFileName));
						}
					}
				}
				return true;
			}
		}

		bool Folder::GetFiles(collections::List<File>& files)const
		{
			if (filePath.IsRoot())
			{
				return true;
			}
			if (!Exists()) return false;
			WIN32_FIND_DATA findData;
			HANDLE findHandle = INVALID_HANDLE_VALUE;

			while (true)
			{
				if (findHandle == INVALID_HANDLE_VALUE)
				{
					WString searchPath = (filePath / L"*").GetFullPath();
					findHandle = FindFirstFile(searchPath.Buffer(), &findData);
					if (findHandle == INVALID_HANDLE_VALUE)
					{
						break;
					}
				}
				else
				{
					BOOL result = FindNextFile(findHandle, &findData);
					if (result == 0)
					{
						FindClose(findHandle);
						break;
					}
				}

				if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					files.Add(File(filePath / findData.cFileName));
				}
			}
			return true;
		}

		bool Folder::CreateNonRecursively()const
		{
			return CreateDirectory(filePath.GetFullPath().Buffer(), NULL) != 0;
		}

		bool Folder::DeleteNonRecursively()const
		{
			return RemoveDirectory(filePath.GetFullPath().Buffer()) != 0;
		}

		bool Folder::Rename(const WString& newName)const
		{
			WString oldFileName = filePath.GetFullPath();
			WString newFileName = (filePath.GetFolder() / newName).GetFullPath();
			return MoveFile(oldFileName.Buffer(), newFileName.Buffer()) != 0;
		}
	}
}
