/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "FileSystem.h"

#if defined VCZH_GCC
#include "Locale.h"
#include "Stream/FileStream.h"
#include "Stream/MemoryWrapperStream.h"
#include "Stream/Accessor.h"
#include "Stream/EncodingStream.h"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#if defined VCZH_APPLE
#include <copyfile.h>
#else
#include <sys/xattr.h>
#endif


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
LinuxFileSystemImpl
***********************************************************************/

		class LinuxFileSystemImpl : public feature_injection::FeatureImpl<IFileSystemImpl>
		{
		private:
			static Nullable<DateTime> FileTimeToDateTime(time_t seconds, long nanoseconds)
			{
				if (seconds < 0) return {};
				return DateTime::FromOSInternal(static_cast<vuint64_t>(seconds) * 1000 + nanoseconds / 1000000).ToUtcTime();
			}

		public:
			// FilePath operations implementation
			wchar_t GetPathDelimiter() const override
			{
				return L'/';
			}

			const wchar_t* GetCompatibleDelimiters() const override
			{
				return L"";
			}

			WString ConcatPath(const WString& fullPath, const WString& relativePath) const override
			{
				if (relativePath.Length() > 0 && relativePath[0] == GetPathDelimiter())
				{
					return relativePath;
				}
				auto delimiter = WString::FromChar(GetPathDelimiter());
				if (IsRoot(fullPath))
				{
					return delimiter + relativePath;
				}

				return fullPath + delimiter + relativePath;
			}

			void Initialize(WString& fullPath) const override
			{
				{
					Array<wchar_t> buffer(fullPath.Length() + 1);
					wcscpy(&buffer[0], fullPath.Buffer());
					FilePath::NormalizeDelimiters(buffer);
					fullPath = &buffer[0];
				}

				if (fullPath.Length() == 0)
					fullPath = WString::FromChar(GetPathDelimiter());

				if (fullPath[0] != GetPathDelimiter())
				{
					char buffer[PATH_MAX] = { 0 };
					getcwd(buffer, PATH_MAX);
					fullPath = atow(AString(buffer)) + WString::FromChar(GetPathDelimiter()) + fullPath;
				}

				{
					collections::List<WString> components;
					FilePath::GetPathComponents(fullPath, components);
					for (int i = 0; i < components.Count(); i++)
					{
						if (components[i] == L".")
						{
							components.RemoveAt(i);
							i--;
						}
						else if (components[i] == L"..")
						{
							if (i > 0)
							{
								components.RemoveAt(i);
								components.RemoveAt(i - 1);
								i -= 2;
							}
							else
							{
								throw ArgumentException(L"Illegal path.");
							}
						}
					}

					fullPath = FilePath::ComponentsToPath(components);
				}

				FilePath::TrimLastDelimiter(fullPath);
			}

			bool IsFile(const WString& fullPath) const override
			{
				struct stat info;
				AString path = wtoa(fullPath);
				int result = stat(path.Buffer(), &info);
				if(result != 0) return false;
				else return S_ISREG(info.st_mode);
			}

			bool IsFolder(const WString& fullPath) const override
			{
				struct stat info;
				AString path = wtoa(fullPath);
				int result = stat(path.Buffer(), &info);
				if(result != 0) return false;
				else return S_ISDIR(info.st_mode);
			}

			bool IsRoot(const WString& fullPath) const override
			{
				return fullPath == WString::FromChar(GetPathDelimiter());
			}

			WString GetRelativePathFor(const WString& fromPath, const WString& toPath) const override
			{
				if (fromPath.Length() == 0 || toPath.Length() == 0 || fromPath[0] != toPath[0])
				{
					return toPath;
				}

				collections::List<WString> srcComponents, tgtComponents, resultComponents;
				FilePath::GetPathComponents(IsFolder(fromPath) ? fromPath : FilePath(fromPath).GetFolder().GetFullPath(), srcComponents);
				FilePath::GetPathComponents(toPath, tgtComponents);

				int minLength = srcComponents.Count() <= tgtComponents.Count() ? srcComponents.Count() : tgtComponents.Count();
				int lastCommonComponent = 0;
				for (int i = 0; i < minLength; i++)
				{
					if (srcComponents[i] == tgtComponents[i])
					{
						lastCommonComponent = i;
					}
					else
						break;
				}

				for (int i = lastCommonComponent + 1; i < srcComponents.Count(); i++)
				{
					resultComponents.Add(L"..");
				}

				for (int i = lastCommonComponent + 1; i < tgtComponents.Count(); i++)
				{
					resultComponents.Add(tgtComponents[i]);
				}

				return FilePath::ComponentsToPath(resultComponents);
			}

			// File operations implementation
			FileInfo GetFileInfo(const FilePath& path) const override
			{
				auto name = wtoa(path.GetFullPath());
				struct stat data;
				CHECK_ERROR(stat(name.Buffer(), &data) == 0, L"vl::filesystem::LinuxFileSystemImpl::GetFileInfo()#Cannot read metadata.");
				FileInfo info;
				info.size = static_cast<vuint64_t>(data.st_size);
				info.hardLinkCount = data.st_nlink;
				info.isDirectory = S_ISDIR(data.st_mode);
				struct stat link;
				if (lstat(name.Buffer(), &link) == 0) info.isSymbolicLink = S_ISLNK(link.st_mode);
				info.canRead = faccessat(AT_FDCWD, name.Buffer(), R_OK, AT_EACCESS) == 0;
				info.canWrite = faccessat(AT_FDCWD, name.Buffer(), W_OK, AT_EACCESS) == 0;
				info.canExecute = faccessat(AT_FDCWD, name.Buffer(), X_OK, AT_EACCESS) == 0;
				info.isReadOnly = (data.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0;
				auto leaf = path.GetName();
				info.isHidden = leaf.Length() > 0 && leaf[0] == L'.';
				info.isSparse = S_ISREG(data.st_mode) && static_cast<vuint64_t>(data.st_blocks) * 512 < info.size;
#if defined VCZH_APPLE
				info.creationTime = FileTimeToDateTime(data.st_birthtimespec.tv_sec, data.st_birthtimespec.tv_nsec);
				info.lastAccessTime = FileTimeToDateTime(data.st_atimespec.tv_sec, data.st_atimespec.tv_nsec);
				info.lastModifiedTime = FileTimeToDateTime(data.st_mtimespec.tv_sec, data.st_mtimespec.tv_nsec);
				info.lastChangeTime = FileTimeToDateTime(data.st_ctimespec.tv_sec, data.st_ctimespec.tv_nsec);
				info.isHidden = info.isHidden || (data.st_flags & UF_HIDDEN) != 0;
				info.isImmutable = (data.st_flags & (UF_IMMUTABLE | SF_IMMUTABLE)) != 0;
				info.isAppendOnly = (data.st_flags & (UF_APPEND | SF_APPEND)) != 0;
#else
				info.lastAccessTime = FileTimeToDateTime(data.st_atim.tv_sec, data.st_atim.tv_nsec);
				info.lastModifiedTime = FileTimeToDateTime(data.st_mtim.tv_sec, data.st_mtim.tv_nsec);
				info.lastChangeTime = FileTimeToDateTime(data.st_ctim.tv_sec, data.st_ctim.tv_nsec);
#if defined STATX_BTIME
				struct statx extra;
				if (statx(AT_FDCWD, name.Buffer(), AT_STATX_SYNC_AS_STAT, STATX_BTIME, &extra) == 0)
				{
					if (extra.stx_mask & STATX_BTIME) info.creationTime = FileTimeToDateTime(extra.stx_btime.tv_sec, extra.stx_btime.tv_nsec);
					auto attributes = extra.stx_attributes & extra.stx_attributes_mask;
					info.isCompressed = (attributes & STATX_ATTR_COMPRESSED) != 0;
					info.isEncrypted = (attributes & STATX_ATTR_ENCRYPTED) != 0;
					info.isImmutable = (attributes & STATX_ATTR_IMMUTABLE) != 0;
					info.isAppendOnly = (attributes & STATX_ATTR_APPEND) != 0;
				}
#endif
#endif
				return info;
			}

			bool FileCopy(const FilePath& source, const FilePath& destination) const override
			{
				auto sourceName = wtoa(source.GetFullPath());
				auto destinationName = wtoa(destination.GetFullPath());
				auto input = open(sourceName.Buffer(), O_RDONLY);
				if (input == -1) return false;
				struct stat original;
				if (fstat(input, &original) != 0 || !S_ISREG(original.st_mode)) { close(input); return false; }
				// Do not truncate until hard-link aliases and non-file destinations are rejected.
				auto output = open(destinationName.Buffer(), O_WRONLY | O_CREAT | O_NONBLOCK, 0600);
				if (output == -1) { close(input); return false; }
				auto copy = [&]()
				{
					struct stat target;
					if (fstat(output, &target) != 0 || !S_ISREG(target.st_mode)) return false;
					if (original.st_dev == target.st_dev && original.st_ino == target.st_ino) return false;
					if (ftruncate(output, 0) != 0) return false;
#if defined VCZH_APPLE
					return fcopyfile(input, output, nullptr, COPYFILE_ALL) == 0;
#else
					char buffer[65536];
					while (true)
					{
						auto count = read(input, buffer, sizeof(buffer));
						if (count == -1 && errno == EINTR) continue;
						if (count == 0) break;
						if (count < 0) return false;
						ssize_t offset = 0;
						while (offset < count)
						{
							auto written = write(output, buffer + offset, count - offset);
							if (written == -1 && errno == EINTR) continue;
							if (written <= 0) return false;
							offset += written;
						}
					}
					if (fchmod(output, original.st_mode & 07777) != 0) return false;
					auto namesSize = flistxattr(input, nullptr, 0);
					if (namesSize < 0 && errno != ENOTSUP) return false;
					if (namesSize > 0)
					{
						Array<char> names(namesSize);
						namesSize = flistxattr(input, &names[0], names.Count());
						if (namesSize < 0) return false;
						for (ssize_t offset = 0; offset < namesSize;)
						{
							auto name = &names[offset];
							offset += strlen(name) + 1;
							auto size = fgetxattr(input, name, nullptr, 0);
							if (size < 0) return false;
							Array<char> value(size > 0 ? size : 1);
							if (fgetxattr(input, name, &value[0], size) != size || fsetxattr(output, name, &value[0], size, 0) != 0) return false;
						}
					}
					const timespec times[] = { original.st_atim, original.st_mtim };
					return futimens(output, times) == 0;
#endif
				};
				auto copied = copy();
				if (close(input) != 0) copied = false;
				if (close(output) != 0) copied = false;
				return copied;
			}

			bool FileDelete(const FilePath& filePath) const override
			{
				AString path = wtoa(filePath.GetFullPath());
				return unlink(path.Buffer()) == 0;
			}

			bool FileRename(const FilePath& filePath, const WString& newName) const override
			{
				AString oldFileName = wtoa(filePath.GetFullPath());
				AString newFileName = wtoa((filePath.GetFolder() / newName).GetFullPath());
				return rename(oldFileName.Buffer(), newFileName.Buffer()) == 0;
			}

			// Folder operations implementation
			bool GetFolders(const FilePath& folderPath, collections::List<Folder>& folders) const override
			{
				DIR *dir;
				AString searchPath = wtoa(folderPath.GetFullPath());

				if ((dir = opendir(searchPath.Buffer())) == NULL)
				{
					return false;
				}

				struct dirent* entry;
				while ((entry = readdir(dir)) != NULL)
				{
					WString childName = atow(AString(entry->d_name));
					FilePath childFullPath = folderPath / childName;
					if (childName != L"." && childName != L".." && childFullPath.IsFolder())
					{
						folders.Add(Folder(childFullPath));
					}
				}

				if (closedir(dir) != 0)
				{
					return false;
				}

				return true;
			}

			bool GetFiles(const FilePath& folderPath, collections::List<File>& files) const override
			{
				DIR* dir;
				AString searchPath = wtoa(folderPath.GetFullPath());

				if ((dir = opendir(searchPath.Buffer())) == NULL)
				{
					return false;
				}

				struct dirent* entry;
				while ((entry = readdir(dir)) != NULL)
				{
					FilePath childFullPath = folderPath / (atow(AString(entry->d_name)));
					if (childFullPath.IsFile())
					{
						files.Add(File(childFullPath));
					}
				}

				if (closedir(dir) != 0)
				{
					return false;
				}

				return true;
			}

			bool CreateFolder(const FilePath& folderPath) const override
			{
				AString path = wtoa(folderPath.GetFullPath());
				return mkdir(path.Buffer(), 0777) == 0;
			}

			bool DeleteFolder(const FilePath& folderPath) const override
			{
				AString path = wtoa(folderPath.GetFullPath());
				return rmdir(path.Buffer()) == 0;
			}

			bool FolderRename(const FilePath& folderPath, const WString& newName) const override
			{
				AString oldFileName = wtoa(folderPath.GetFullPath());
				AString newFileName = wtoa((folderPath.GetFolder() / newName).GetFullPath());
				return rename(oldFileName.Buffer(), newFileName.Buffer()) == 0;
			}

			Ptr<stream::IFileStreamImpl> GetFileStreamImpl(const WString& fileName, stream::FileStream::AccessRight accessRight) const override
			{
				return stream::CreateOSFileStreamImpl(fileName, accessRight);
			}
		};

/***********************************************************************
Global FileSystem Implementation
***********************************************************************/

		IFileSystemImpl* GetOSFileSystemImpl()
		{
			static LinuxFileSystemImpl osFileSystemImpl;
			return &osFileSystemImpl;
		}
	}
}

#endif
