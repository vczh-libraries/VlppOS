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
	namespace filesystem
	{
		using namespace collections;
		using namespace stream;

/***********************************************************************
FilePath
***********************************************************************/

		const wchar_t FilePath::Delimiter;

		void FilePath::Initialize()
		{
			{
				Array<wchar_t> buffer(fullPath.Length() + 1);
				wcscpy(&buffer[0], fullPath.Buffer());
				NormalizeDelimiters(buffer);
				fullPath = &buffer[0];
			}

			if (fullPath.Length() == 0)
				fullPath = WString::Unmanaged(L"/");

			if (fullPath[0] != Delimiter)
			{
				char buffer[PATH_MAX] = { 0 };
				getcwd(buffer, PATH_MAX);
				fullPath = atow(AString(buffer)) + WString::FromChar(Delimiter) + fullPath;
			}

			{
				collections::List<WString> components;
				GetPathComponents(fullPath, components);
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

				fullPath = ComponentsToPath(components);
			}

			TrimLastDelimiter(fullPath);
		}

		bool FilePath::IsFile()const
		{
			struct stat info;
			AString path = wtoa(fullPath);
			int result = stat(path.Buffer(), &info);
			if(result != 0) return false;
			else return S_ISREG(info.st_mode);
		}

		bool FilePath::IsFolder()const
		{
			struct stat info;
			AString path = wtoa(fullPath);
			int result = stat(path.Buffer(), &info);
			if(result != 0) return false;
			else return S_ISDIR(info.st_mode);
		}

		bool FilePath::IsRoot()const
		{
			return fullPath == L"/";
		}

		WString FilePath::GetRelativePathFor(const FilePath& _filePath) const
		{
			if (fullPath.Length() == 0 || _filePath.fullPath.Length() == 0 || fullPath[0] != _filePath.fullPath[0])
			{
				return _filePath.fullPath;
			}

			collections::List<WString> srcComponents, tgtComponents, resultComponents;
			GetPathComponents(IsFolder() ? fullPath : GetFolder().GetFullPath(), srcComponents);
			GetPathComponents(_filePath.fullPath, tgtComponents);

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

			return ComponentsToPath(resultComponents);
		}

/***********************************************************************
File
***********************************************************************/

		bool File::Delete()const
		{
			AString path = wtoa(filePath.GetFullPath());
			return unlink(path.Buffer()) == 0;
		}

		bool File::Rename(const WString& newName)const
		{
			AString oldFileName = wtoa(filePath.GetFullPath());
			AString newFileName = wtoa((filePath.GetFolder() / newName).GetFullPath());
			return rename(oldFileName.Buffer(), newFileName.Buffer()) == 0;
		}

/***********************************************************************
Folder
***********************************************************************/

		bool Folder::GetFolders(collections::List<Folder>& folders)const
		{
			if (!Exists()) return false;

			DIR *dir;
			AString searchPath = wtoa(filePath.GetFullPath());

			if ((dir = opendir(searchPath.Buffer())) == NULL)
			{
				return false;
			}

			struct dirent* entry;
			while ((entry = readdir(dir)) != NULL)
			{
				WString childName = atow(AString(entry->d_name));
				FilePath childFullPath = filePath / childName;
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

		bool Folder::GetFiles(collections::List<File>& files)const
		{
			if (!Exists()) return false;

			DIR* dir;
			AString searchPath = wtoa(filePath.GetFullPath());

			if ((dir = opendir(searchPath.Buffer())) == NULL)
			{
				return false;
			}

			struct dirent* entry;
			while ((entry = readdir(dir)) != NULL)
			{
				FilePath childFullPath = filePath / (atow(AString(entry->d_name)));
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

		bool Folder::CreateNonRecursively()const
		{
			AString path = wtoa(filePath.GetFullPath());
			return mkdir(path.Buffer(), 0777) == 0;
		}

		bool Folder::DeleteNonRecursively()const
		{
			AString path = wtoa(filePath.GetFullPath());
			return rmdir(path.Buffer()) == 0;
		}

		bool Folder::Rename(const WString& newName)const
		{
			AString oldFileName = wtoa(filePath.GetFullPath());
			AString newFileName = wtoa((filePath.GetFolder() / newName).GetFullPath());
			return rename(oldFileName.Buffer(), newFileName.Buffer()) == 0;
		}
	}
}
