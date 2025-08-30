/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "FileSystem.h"

namespace vl
{
	namespace filesystem
	{
		extern IFileSystemImpl* GetOSFileSystemImpl();

		IFileSystemImpl* injectedFileSystemImpl = nullptr;

		void InjectFileSystemImpl(IFileSystemImpl* impl)
		{
			injectedFileSystemImpl = impl;
		}

		IFileSystemImpl* GetFileSystemImpl()
		{
			return injectedFileSystemImpl ? injectedFileSystemImpl : GetOSFileSystemImpl();
		}

/***********************************************************************
FilePath
***********************************************************************/

		void FilePath::Initialize()
		{
			GetFileSystemImpl()->Initialize(fullPath);
		}

		bool FilePath::IsFile() const
		{
			return GetFileSystemImpl()->IsFile(fullPath);
		}

		bool FilePath::IsFolder() const
		{
			return GetFileSystemImpl()->IsFolder(fullPath);
		}

		bool FilePath::IsRoot() const
		{
			return GetFileSystemImpl()->IsRoot(fullPath);
		}

		WString FilePath::GetRelativePathFor(const FilePath& _filePath) const
		{
			return GetFileSystemImpl()->GetRelativePathFor(fullPath, _filePath.GetFullPath());
		}

/***********************************************************************
File
***********************************************************************/

		bool File::Delete() const
		{
			return GetFileSystemImpl()->FileDelete(filePath);
		}

		bool File::Rename(const WString& newName) const
		{
			return GetFileSystemImpl()->FileRename(filePath, newName);
		}

/***********************************************************************
Folder
***********************************************************************/

		bool Folder::GetFolders(collections::List<Folder>& folders) const
		{
			return GetFileSystemImpl()->GetFolders(filePath, folders);
		}

		bool Folder::GetFiles(collections::List<File>& files) const
		{
			return GetFileSystemImpl()->GetFiles(filePath, files);
		}

		bool Folder::CreateNonRecursively() const
		{
			return GetFileSystemImpl()->CreateFolder(filePath);
		}

		bool Folder::DeleteNonRecursively() const
		{
			return GetFileSystemImpl()->DeleteFolder(filePath);
		}

		bool Folder::Rename(const WString& newName) const
		{
			return GetFileSystemImpl()->FolderRename(filePath, newName);
		}
	}
}