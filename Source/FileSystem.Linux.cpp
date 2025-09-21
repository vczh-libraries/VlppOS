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
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#ifndef VCZH_GCC
static_assert(false, "Do not build this file for Windows applications.");
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
		public:
			// FilePath operations implementation
			void Initialize(WString& fullPath) const override
			{
				{
					Array<wchar_t> buffer(fullPath.Length() + 1);
					wcscpy(&buffer[0], fullPath.Buffer());
					FilePath::NormalizeDelimiters(buffer);
					fullPath = &buffer[0];
				}

				if (fullPath.Length() == 0)
					fullPath = WString::Unmanaged(L"/");

				if (fullPath[0] != FilePath::Delimiter)
				{
					char buffer[PATH_MAX] = { 0 };
					getcwd(buffer, PATH_MAX);
					fullPath = atow(AString(buffer)) + WString::FromChar(FilePath::Delimiter) + fullPath;
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
				return fullPath == L"/";
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
