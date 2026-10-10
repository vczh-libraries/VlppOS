/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "FileSystem.h"

#if defined VCZH_WASM
#include "Stream/MemoryStream.h"
#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/val.h>

namespace vl::filesystem
{
	using namespace collections;
	using emscripten::val;

/***********************************************************************
OPFS JavaScript boundary (requires Asyncify)
***********************************************************************/

	EM_ASYNC_JS(emscripten::EM_VAL, OpfsGetHandle, (emscripten::EM_VAL path, bool directory, bool create), {
		try {
			const parts = Emval.toValue(path).split("/").filter(Boolean);
			const name = parts.pop();
			let parent = await navigator.storage.getDirectory();
			for (const part of parts) parent = await parent.getDirectoryHandle(part);
			if (name === undefined) return directory ? Emval.toHandle(parent) : 0;
			const handle = directory
				? await parent.getDirectoryHandle(name, { create })
				: await parent.getFileHandle(name, { create });
			return Emval.toHandle(handle);
		} catch { return 0; }
	});

	EM_ASYNC_JS(emscripten::EM_VAL, OpfsReadInfo, (emscripten::EM_VAL handle), {
		try {
			const entry = Emval.toValue(handle);
			if (entry.kind === "directory") return Emval.toHandle({ directory: true });
			const file = await entry.getFile();
			return Emval.toHandle({ directory: false, size: file.size, modified: file.lastModified });
		} catch { return 0; }
	});

	EM_ASYNC_JS(emscripten::EM_VAL, OpfsReadFile, (emscripten::EM_VAL handle), {
		try {
			const file = await Emval.toValue(handle).getFile();
			return Emval.toHandle(new Uint8Array(await file.arrayBuffer()));
		} catch { return 0; }
	});

	EM_JS(bool, OpfsCopyBytes, (emscripten::EM_VAL bytes, void* buffer), {
		try {
			HEAPU8.set(Emval.toValue(bytes), buffer);
			return true;
		} catch { return false; }
	});

	EM_ASYNC_JS(bool, OpfsWriteFile, (emscripten::EM_VAL handle, const void* buffer, vint length), {
		try {
			// Own the bytes before awaiting; Wasm memory can grow on another thread.
			const bytes = new Uint8Array(HEAPU8.subarray(buffer, buffer + length));
			const writer = await Emval.toValue(handle).createWritable();
			await writer.write(bytes);
			await writer.close();
			return true;
		} catch { return false; }
	});

	EM_ASYNC_JS(emscripten::EM_VAL, OpfsGetEntries, (emscripten::EM_VAL handle, bool directory), {
		try {
			const names = [];
			for await (const [name, child] of Emval.toValue(handle).entries()) {
				if ((child.kind === "directory") === !!directory) names.push(name);
			}
			return Emval.toHandle(names.sort());
		} catch { return 0; }
	});

	EM_ASYNC_JS(bool, OpfsRemoveEntry, (emscripten::EM_VAL parent, emscripten::EM_VAL name), {
		try {
			await Emval.toValue(parent).removeEntry(Emval.toValue(name));
			return true;
		} catch { return false; }
	});

	EM_ASYNC_JS(bool, OpfsRenameEntry, (emscripten::EM_VAL source, emscripten::EM_VAL parent, emscripten::EM_VAL oldName, emscripten::EM_VAL destination, emscripten::EM_VAL newName), {
		try {
			// Directory move is not portable in OPFS. Copy the tree before deleting it.
			async function copy(entry, target, name) {
				if (entry.kind === "file") {
					const file = await target.getFileHandle(name, { create: true });
					const writer = await file.createWritable();
					await writer.write(await entry.getFile());
					await writer.close();
				} else {
					const folder = await target.getDirectoryHandle(name, { create: true });
					for await (const [childName, child] of entry.entries()) await copy(child, folder, childName);
				}
			}
			await copy(Emval.toValue(source), Emval.toValue(destination), Emval.toValue(newName));
			await Emval.toValue(parent).removeEntry(Emval.toValue(oldName), { recursive: true });
			return true;
		} catch { return false; }
	});

	val ToOpfsString(const WString& text)
	{
		// Embind's UTF-16 adapter belongs only at the JavaScript boundary.
		auto converted = wtou16(text);
		return val(std::u16string(converted.Buffer(), converted.Length()));
	}

	WString FromOpfsString(const val& text)
	{
		auto converted = text.as<std::u16string>();
		return u16tow(U16String::CopyFrom(converted.data(), converted.size()));
	}

	template<typename T>
	T CompleteOpfsOperation(T result)
	{
		// Resume through a managed callback so returning pthreads become joinable.
		// EM_ASYNC_JS alone misses this in Emscripten 3.1.6 (upstream issue 17552).
		if (!emscripten_is_main_runtime_thread()) emscripten_sleep(0);
		return result;
	}

	val GetOpfsHandle(const FilePath& path, bool directory, bool create = false)
	{
		auto text = ToOpfsString(path.GetFullPath());
		auto handle = CompleteOpfsOperation(OpfsGetHandle(text.as_handle(), directory, create));
		return handle ? val::take_ownership(handle) : val::null();
	}

/***********************************************************************
OpfsFileStreamImpl
***********************************************************************/

	class OpfsFileStreamImpl : public Object, public virtual stream::IFileStreamImpl
	{
	private:
		FilePath						filePath;
		stream::FileStream::AccessRight	accessRight;
		val								handle = val::null();
		Ptr<stream::MemoryStream>		memory;

	public:
		OpfsFileStreamImpl(const WString& fileName, stream::FileStream::AccessRight _accessRight)
			: filePath(fileName)
			, accessRight(_accessRight)
		{
		}

		~OpfsFileStreamImpl()
		{
			Close();
		}

		bool Open() override
		{
#define ERROR_MESSAGE_PREFIX L"vl::filesystem::OpfsFileStreamImpl::Open()#"
			CHECK_ERROR(!memory, ERROR_MESSAGE_PREFIX L"Stream is already open.");
			handle = GetOpfsHandle(filePath, false, accessRight != stream::FileStream::ReadOnly);
			if (handle.isNull()) return false;
			auto opened = Ptr(new stream::MemoryStream);
			if (accessRight != stream::FileStream::WriteOnly)
			{
				auto result = CompleteOpfsOperation(OpfsReadFile(handle.as_handle()));
				if (!result) return false;
				auto bytes = val::take_ownership(result);
				auto size = bytes["length"].as<vint>();
				if (size > 0)
				{
					Array<vuint8_t> buffer(size);
					CHECK_ERROR(OpfsCopyBytes(bytes.as_handle(), &buffer[0]), ERROR_MESSAGE_PREFIX L"Failed to copy OPFS content.");
					opened->Write(&buffer[0], size);
					opened->SeekFromBegin(0);
				}
			}
			memory = opened;
			return true;
#undef ERROR_MESSAGE_PREFIX
		}

		void Close() override
		{
#define ERROR_MESSAGE_PREFIX L"vl::filesystem::OpfsFileStreamImpl::Close()#"
			if (memory)
			{
				if (accessRight != stream::FileStream::ReadOnly)
				{
					CHECK_ERROR(CompleteOpfsOperation(OpfsWriteFile(handle.as_handle(), memory->GetInternalBuffer(), (vint)memory->Size())), ERROR_MESSAGE_PREFIX L"Failed to write OPFS content.");
				}
				memory = nullptr;
				handle = val::null();
			}
#undef ERROR_MESSAGE_PREFIX
		}

		pos_t Position() const override { return memory ? memory->Position() : -1; }
		pos_t Size() const override { return memory ? memory->Size() : -1; }
		void Seek(pos_t size) override { memory->Seek(size); }
		void SeekFromBegin(pos_t size) override { memory->SeekFromBegin(size); }
		void SeekFromEnd(pos_t size) override { memory->SeekFromEnd(size); }

		vint Read(void* buffer, vint size) override
		{
#define ERROR_MESSAGE_PREFIX L"vl::filesystem::OpfsFileStreamImpl::Read(void*, vint)#"
			CHECK_ERROR(accessRight != stream::FileStream::WriteOnly, ERROR_MESSAGE_PREFIX L"Stream is not readable.");
			return memory->Read(buffer, size);
#undef ERROR_MESSAGE_PREFIX
		}

		vint Write(void* buffer, vint size) override
		{
#define ERROR_MESSAGE_PREFIX L"vl::filesystem::OpfsFileStreamImpl::Write(void*, vint)#"
			CHECK_ERROR(accessRight != stream::FileStream::ReadOnly, ERROR_MESSAGE_PREFIX L"Stream is not writable.");
			return memory->Write(buffer, size);
#undef ERROR_MESSAGE_PREFIX
		}

		vint Peek(void* buffer, vint size) override
		{
#define ERROR_MESSAGE_PREFIX L"vl::filesystem::OpfsFileStreamImpl::Peek(void*, vint)#"
			CHECK_ERROR(accessRight != stream::FileStream::WriteOnly, ERROR_MESSAGE_PREFIX L"Stream is not readable.");
			return memory->Peek(buffer, size);
#undef ERROR_MESSAGE_PREFIX
		}
	};

/***********************************************************************
OpfsFileSystemImpl
***********************************************************************/

	class OpfsFileSystemImpl : public feature_injection::FeatureImpl<IFileSystemImpl>
	{
	private:
		bool DeleteEntry(const FilePath& path, bool directory) const
		{
			if (path.IsRoot() || GetOpfsHandle(path, directory).isNull()) return false;
			auto parent = GetOpfsHandle(path.GetFolder(), true);
			auto name = ToOpfsString(path.GetName());
			return !parent.isNull() && CompleteOpfsOperation(OpfsRemoveEntry(parent.as_handle(), name.as_handle()));
		}

		bool RenameEntry(const FilePath& path, const WString& newName, bool directory) const
		{
			if (path.IsRoot()) return false;
			auto source = GetOpfsHandle(path, directory);
			if (source.isNull()) return false;
			auto target = path.GetFolder() / newName;
			if (target == path) return true;
			if (target.IsRoot() || target.IsFile() || target.IsFolder()) return false;
			auto prefix = path.GetFullPath() + L"/";
			if (directory && target.GetFullPath().Length() >= prefix.Length() && target.GetFullPath().Left(prefix.Length()) == prefix) return false;
			auto parent = GetOpfsHandle(path.GetFolder(), true);
			auto destination = GetOpfsHandle(target.GetFolder(), true);
			if (parent.isNull() || destination.isNull()) return false;
			auto oldText = ToOpfsString(path.GetName());
			auto newText = ToOpfsString(target.GetName());
			return CompleteOpfsOperation(OpfsRenameEntry(source.as_handle(), parent.as_handle(), oldText.as_handle(), destination.as_handle(), newText.as_handle()));
		}

	public:
		wchar_t GetPathDelimiter() const override { return L'/'; }
		const wchar_t* GetCompatibleDelimiters() const override { return L""; }

		WString ConcatPath(const WString& fullPath, const WString& relativePath) const override
		{
			if (relativePath.Length() > 0 && relativePath[0] == L'/') return relativePath;
			return fullPath + (IsRoot(fullPath) ? L"" : L"/") + relativePath;
		}

		void Initialize(WString& fullPath) const override
		{
			List<WString> components;
			vint begin = 0;
			for (vint i = 0; i <= fullPath.Length(); i++)
			{
				if (i < fullPath.Length() && fullPath[i] == L'\0') throw ArgumentException(L"Illegal path.");
				if (i < fullPath.Length() && fullPath[i] != L'/') continue;
				auto part = fullPath.Sub(begin, i - begin);
				begin = i + 1;
				if (part == L"..")
				{
					if (components.Count() == 0) throw ArgumentException(L"Illegal path.");
					components.RemoveAt(components.Count() - 1);
				}
				else if (part.Length() > 0 && part != L".")
				{
					components.Add(part);
				}
			}
			fullPath = L"";
			for (const auto& part : components) fullPath += L"/" + part;
			if (fullPath.Length() == 0) fullPath = L"/";
		}

		bool IsFile(const WString& fullPath) const override { return !GetOpfsHandle(FilePath(fullPath), false).isNull(); }
		bool IsFolder(const WString& fullPath) const override { return !GetOpfsHandle(FilePath(fullPath), true).isNull(); }
		bool IsRoot(const WString& fullPath) const override { return fullPath == L"/"; }

		WString GetRelativePathFor(const WString& fromPath, const WString& toPath) const override
		{
			List<WString> source, target, result;
			FilePath::GetPathComponents(IsFolder(fromPath) ? fromPath : FilePath(fromPath).GetFolder().GetFullPath(), source);
			FilePath::GetPathComponents(toPath, target);
			vint common = 0;
			while (common < source.Count() && common < target.Count() && source[common] == target[common]) common++;
			for (vint i = common; i < source.Count(); i++) result.Add(L"..");
			for (vint i = common; i < target.Count(); i++) result.Add(target[i]);
			return FilePath::ComponentsToPath(result);
		}

		FileInfo GetFileInfo(const FilePath& path) const override
		{
			auto handle = GetOpfsHandle(path, false);
			if (handle.isNull()) handle = GetOpfsHandle(path, true);
			CHECK_ERROR(!handle.isNull(), L"vl::filesystem::OpfsFileSystemImpl::GetFileInfo()#Entry does not exist.");
			auto result = CompleteOpfsOperation(OpfsReadInfo(handle.as_handle()));
			CHECK_ERROR(result, L"vl::filesystem::OpfsFileSystemImpl::GetFileInfo()#Cannot read metadata.");
			auto data = val::take_ownership(result);
			FileInfo info;
			info.isDirectory = data["directory"].as<bool>();
			info.canRead = true;
			info.canWrite = true;
			if (!info.isDirectory)
			{
				info.size = static_cast<vuint64_t>(data["size"].as<double>());
				auto modified = data["modified"].as<double>();
				if (modified >= 0) info.lastModifiedTime = DateTime::FromOSInternal(static_cast<vuint64_t>(modified));
			}
			return info;
		}

		bool FileCopy(const FilePath&, const FilePath&) const override
		{
			// OPFS cannot set file timestamps, so it cannot preserve copy metadata.
			return false;
		}

		bool FileDelete(const FilePath& path) const override { return DeleteEntry(path, false); }
		bool FileRename(const FilePath& path, const WString& newName) const override { return RenameEntry(path, newName, false); }
		bool DeleteFolder(const FilePath& path) const override { return DeleteEntry(path, true); }
		bool FolderRename(const FilePath& path, const WString& newName) const override { return RenameEntry(path, newName, true); }

		bool CreateFolder(const FilePath& path) const override
		{
			if (path.IsRoot() || path.IsFile() || path.IsFolder()) return false;
			return !GetOpfsHandle(path, true, true).isNull();
		}

		bool GetFolders(const FilePath& path, List<Folder>& folders) const override
		{
			auto handle = GetOpfsHandle(path, true);
			if (handle.isNull()) return false;
			auto result = CompleteOpfsOperation(OpfsGetEntries(handle.as_handle(), true));
			if (!result) return false;
			auto names = val::take_ownership(result);
			for (vint i = 0; i < names["length"].as<vint>(); i++) folders.Add(Folder(path / FromOpfsString(names[i])));
			return true;
		}

		bool GetFiles(const FilePath& path, List<File>& files) const override
		{
			auto handle = GetOpfsHandle(path, true);
			if (handle.isNull()) return false;
			auto result = CompleteOpfsOperation(OpfsGetEntries(handle.as_handle(), false));
			if (!result) return false;
			auto names = val::take_ownership(result);
			for (vint i = 0; i < names["length"].as<vint>(); i++) files.Add(File(path / FromOpfsString(names[i])));
			return true;
		}

		Ptr<stream::IFileStreamImpl> GetFileStreamImpl(const WString& fileName, stream::FileStream::AccessRight accessRight) const override
		{
			return Ptr(new OpfsFileStreamImpl(fileName, accessRight));
		}
	};

/***********************************************************************
Global FileSystem Implementation
***********************************************************************/

	IFileSystemImpl* GetOSFileSystemImpl()
	{
		static OpfsFileSystemImpl osFileSystemImpl;
		return &osFileSystemImpl;
	}
}
#endif
