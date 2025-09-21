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

		feature_injection::FeatureInjection<IFileSystemImpl>& GetFileSystemInjection()
		{
			static feature_injection::FeatureInjection<IFileSystemImpl> injection(GetOSFileSystemImpl());
			return injection;
		}

		void InjectFileSystemImpl(IFileSystemImpl* impl)
		{
			GetFileSystemInjection().Inject(impl);
		}

		IFileSystemImpl* GetFileSystemImpl()
		{
			return GetFileSystemInjection().Get();
		}

		void EjectFileSystemImpl(IFileSystemImpl* impl)
		{
			if (impl == nullptr)
			{
				GetFileSystemInjection().EjectAll();
			}
			else
			{
				GetFileSystemInjection().Eject(impl);
			}
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