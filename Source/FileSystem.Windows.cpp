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
	namespace stream
	{
		extern Ptr<IFileStreamImpl>		CreateOSFileStreamImpl(const WString& fileName, FileStream::AccessRight accessRight);
	}

	namespace filesystem
	{
		using namespace collections;
		using namespace stream;

/***********************************************************************
WindowsFileSystemImpl
***********************************************************************/

		class WindowsFileSystemImpl : public feature_injection::FeatureImpl<IFileSystemImpl>
		{
		public:
			void Initialize(WString& fullPath) const override
			{
				{
					Array<wchar_t> buffer(fullPath.Length() + 1);
					wcscpy_s(&buffer[0], fullPath.Length() + 1, fullPath.Buffer());
					FilePath::NormalizeDelimiters(buffer);
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

				FilePath::TrimLastDelimiter(fullPath);
			}

			bool IsFile(const WString& fullPath) const override
			{
				WIN32_FILE_ATTRIBUTE_DATA info;
				BOOL result = GetFileAttributesEx(fullPath.Buffer(), GetFileExInfoStandard, &info);
				if (!result) return false;
				return (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
			}

			bool IsFolder(const WString& fullPath) const override
			{
				WIN32_FILE_ATTRIBUTE_DATA info;
				BOOL result = GetFileAttributesEx(fullPath.Buffer(), GetFileExInfoStandard, &info);
				if (!result) return false;
				return (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			}

			bool IsRoot(const WString& fullPath) const override
			{
				return fullPath == L"";
			}

			WString GetRelativePathFor(const WString& fromPath, const WString& toPath) const override
			{
				if (fromPath.Length() == 0 || toPath.Length() == 0 || fromPath[0] != toPath[0])
				{
					return toPath;
				}

				wchar_t buffer[MAX_PATH + 1] = { 0 };
				PathRelativePathTo(
					buffer,
					fromPath.Buffer(),
					(IsFolder(fromPath) ? FILE_ATTRIBUTE_DIRECTORY : 0),
					toPath.Buffer(),
					(IsFolder(toPath) ? FILE_ATTRIBUTE_DIRECTORY : 0)
				);
				return buffer;
			}

			bool FileDelete(const FilePath& filePath) const override
			{
				return DeleteFile(filePath.GetFullPath().Buffer()) != 0;
			}

			bool FileRename(const FilePath& filePath, const WString& newName) const override
			{
				WString oldFileName = filePath.GetFullPath();
				WString newFileName = (filePath.GetFolder() / newName).GetFullPath();
				return MoveFile(oldFileName.Buffer(), newFileName.Buffer()) != 0;
			}

			bool GetFolders(const FilePath& folderPath, collections::List<Folder>& folders) const override
			{
				if (folderPath.IsRoot())
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
					if (!IsFolder(folderPath.GetFullPath())) return false;
					WIN32_FIND_DATA findData;
					HANDLE findHandle = INVALID_HANDLE_VALUE;

					while (true)
					{
						if (findHandle == INVALID_HANDLE_VALUE)
						{
							WString searchPath = (folderPath / L"*").GetFullPath();
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
								folders.Add(Folder(folderPath / findData.cFileName));
							}
						}
					}
					return true;
				}
			}

			bool GetFiles(const FilePath& folderPath, collections::List<File>& files) const override
			{
				if (IsRoot(folderPath.GetFullPath()))
				{
					return true;
				}
				if (!IsFolder(folderPath.GetFullPath())) return false;
				WIN32_FIND_DATA findData;
				HANDLE findHandle = INVALID_HANDLE_VALUE;

				while (true)
				{
					if (findHandle == INVALID_HANDLE_VALUE)
					{
						WString searchPath = (folderPath / L"*").GetFullPath();
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
						files.Add(File(folderPath / findData.cFileName));
					}
				}
				return true;
			}

			bool CreateFolder(const FilePath& folderPath) const override
			{
				return CreateDirectory(folderPath.GetFullPath().Buffer(), NULL) != 0;
			}

			bool DeleteFolder(const FilePath& folderPath) const override
			{
				return RemoveDirectory(folderPath.GetFullPath().Buffer()) != 0;
			}

			bool FolderRename(const FilePath& folderPath, const WString& newName) const override
			{
				WString oldFileName = folderPath.GetFullPath();
				WString newFileName = (folderPath.GetFolder() / newName).GetFullPath();
				return MoveFile(oldFileName.Buffer(), newFileName.Buffer()) != 0;
			}

			Ptr<stream::IFileStreamImpl> GetFileStreamImpl(const WString& fileName, stream::FileStream::AccessRight accessRight) const override
			{
				return stream::CreateOSFileStreamImpl(fileName, accessRight);
			}
		};

		IFileSystemImpl* GetOSFileSystemImpl()
		{
			static WindowsFileSystemImpl osFileSystemImpl;
			return &osFileSystemImpl;
		}
	}
}
