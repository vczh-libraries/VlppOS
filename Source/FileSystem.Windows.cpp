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
#define _WINSOCKAPI_
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
		private:
			static Nullable<DateTime> FileTimeToDateTime(vuint64_t time)
			{
				if (time == 0) return {};
				auto result = DateTime::FromOSInternal(time);
				// Preserve FILETIME precision for ordering, beyond the calendar's milliseconds.
				result.osInternal = time;
				return result;
			}

			static Nullable<DateTime> FileTimeToDateTime(FILETIME time)
			{
				return FileTimeToDateTime((static_cast<vuint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime);
			}

		public:
			wchar_t GetPathDelimiter() const override
			{
				return L'\\';
			}

			const wchar_t* GetCompatibleDelimiters() const override
			{
				return L"/";
			}

			WString ConcatPath(const WString& fullPath, const WString& relativePath) const override
			{
				if (IsRoot(fullPath))
				{
					return relativePath;
				}

				return fullPath + WString::FromChar(GetPathDelimiter()) + relativePath;
			}

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

			FileInfo GetFileInfo(const FilePath& path) const override
			{
				WIN32_FILE_ATTRIBUTE_DATA data;
				CHECK_ERROR(GetFileAttributesExW(path.GetFullPath().Buffer(), GetFileExInfoStandard, &data), L"vl::filesystem::WindowsFileSystemImpl::GetFileInfo()#Cannot read metadata.");
				FileInfo info;
				info.size = (static_cast<vuint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
				info.creationTime = FileTimeToDateTime(data.ftCreationTime);
				info.lastAccessTime = FileTimeToDateTime(data.ftLastAccessTime);
				info.lastModifiedTime = FileTimeToDateTime(data.ftLastWriteTime);
				info.isReparsePoint = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
				auto handle = CreateFileW(path.GetFullPath().Buffer(), FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
				if (handle != INVALID_HANDLE_VALUE)
				{
					FILE_BASIC_INFO basic;
					if (GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)))
					{
						info.creationTime = FileTimeToDateTime(basic.CreationTime.QuadPart);
						info.lastAccessTime = FileTimeToDateTime(basic.LastAccessTime.QuadPart);
						info.lastModifiedTime = FileTimeToDateTime(basic.LastWriteTime.QuadPart);
						info.lastChangeTime = FileTimeToDateTime(basic.ChangeTime.QuadPart);
						data.dwFileAttributes = basic.FileAttributes;
					}
					BY_HANDLE_FILE_INFORMATION identity;
					if (GetFileInformationByHandle(handle, &identity))
					{
						info.size = (static_cast<vuint64_t>(identity.nFileSizeHigh) << 32) | identity.nFileSizeLow;
						info.hardLinkCount = identity.nNumberOfLinks;
					}
					CloseHandle(handle);
				}
				info.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				info.isReadOnly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
				info.isHidden = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
				info.isSystem = (data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
				info.isArchive = (data.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0;
				info.isCompressed = (data.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0;
				info.isEncrypted = (data.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) != 0;
				info.isSparse = (data.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0;
				info.isTemporary = (data.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0;
				info.isOffline = (data.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) != 0;
				info.isNotContentIndexed = (data.dwFileAttributes & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) != 0;
				if (info.isReparsePoint)
				{
					WIN32_FIND_DATAW found;
					auto search = FindFirstFileW(path.GetFullPath().Buffer(), &found);
					if (search != INVALID_HANDLE_VALUE)
					{
						info.isSymbolicLink = found.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
						FindClose(search);
					}
				}
				auto canAccess = [&](DWORD access)
				{
					auto probe = CreateFileW(path.GetFullPath().Buffer(), access,
						FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
					if (probe == INVALID_HANDLE_VALUE) return false;
					CloseHandle(probe);
					return true;
				};
				info.canRead = canAccess(FILE_READ_DATA);
				info.canWrite = (!info.isReadOnly || info.isDirectory) && canAccess(FILE_WRITE_DATA);
				info.canExecute = canAccess(FILE_EXECUTE);
				return info;
			}

			bool FileCopy(const FilePath& source, const FilePath& destination) const override
			{
				auto input = CreateFileW(source.GetFullPath().Buffer(), FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
				if (input == INVALID_HANDLE_VALUE) return false;
				FILE_BASIC_INFO basic;
				auto queried = GetFileInformationByHandleEx(input, FileBasicInfo, &basic, sizeof(basic));
				auto closed = CloseHandle(input);
				if (!queried || !closed || !CopyFileW(source.GetFullPath().Buffer(), destination.GetFullPath().Buffer(), FALSE)) return false;
				// CopyFile keeps native attributes/streams. Restore all settable timestamps too.
				auto output = CreateFileW(destination.GetFullPath().Buffer(), FILE_WRITE_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
				if (output == INVALID_HANDLE_VALUE) return false;
				basic.FileAttributes = 0;
				auto copied = SetFileInformationByHandle(output, FileBasicInfo, &basic, sizeof(basic));
				closed = CloseHandle(output);
				return copied && closed;
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
