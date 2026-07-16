#include "../../../Source/InterProcess/AsyncSocket/AsyncSocket_HttpServerApi.h"
#include "../../../Source/FileSystem.h"

#if defined VCZH_MSVC
#define _WINSOCKAPI_
#include <Windows.h>
#elif defined VCZH_GCC
#include <sys/stat.h>
#endif

using namespace vl;
using namespace vl::collections;
using namespace vl::console;
using namespace vl::filesystem;
using namespace vl::inter_process::async_tcp_socket;

namespace
{
	HttpField CreateTextField(const WString& name, const WString& value)
	{
#define ERROR_MESSAGE_PREFIX L"MiniHttpServer::CreateTextField(const WString&, const WString&)#"
		HttpField field;
		field.name = name;
		field.value.Resize(value.Length());
		for (vint i = 0; i < value.Length(); i++)
		{
			CHECK_ERROR(value[i] <= 0x7F, ERROR_MESSAGE_PREFIX L"HTTP response metadata must be ASCII.");
			field.value[i] = (vuint8_t)value[i];
		}
#undef ERROR_MESSAGE_PREFIX
		return field;
	}

	bool IsFileSystemLink(const FilePath& filePath)
	{
#if defined VCZH_MSVC
		auto attributes = GetFileAttributesW(filePath.GetFullPath().Buffer());
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#elif defined VCZH_GCC
		auto path = wtoa(filePath.GetFullPath());
		struct stat info;
		return lstat(path.Buffer(), &info) == 0 && S_ISLNK(info.st_mode);
#endif
	}

	Ptr<HttpResponse> CreateStatusResponse(vint statusCode, const WString& reason)
	{
		auto response = Ptr(new HttpResponse);
		response->statusCode = statusCode;
		response->reason = reason;
		response->headers.Add(CreateTextField(L"Content-Type", L"text/plain; charset=utf-8"));
		return response;
	}

	class PhysicalFolderHttpServer : public SocketHttpServerApi
	{
	private:
		FilePath						root;

		bool TryResolveFile(const WString& relativePath, FilePath& filePath)
		{
			if (relativePath.Length() == 0 || relativePath[0] != L'/') return false;

			List<WString> components;
			WString component;
			bool trailingSeparator = relativePath == L"/";
			for (vint i = 1; i < relativePath.Length(); i++)
			{
				auto c = relativePath[i];
				if (c == 0 || c == L':') return false;
				if (c == L'/' || c == L'\\')
				{
					if (component.Length() == 0) return false;
					if (component == L"." || component == L"..") return false;
					components.Add(component);
					component = WString::Empty;
					trailingSeparator = true;
				}
				else
				{
					component += WString::FromChar(c);
					trailingSeparator = false;
				}
			}
			if (component.Length() > 0)
			{
				if (component == L"." || component == L"..") return false;
				components.Add(component);
			}
			if (components.Count() == 0 || trailingSeparator)
			{
				components.Add(L"index.html");
			}

			auto resolved = root;
			for (auto&& item : components)
			{
				resolved = resolved / item;
				if (IsFileSystemLink(resolved)) return false;
			}
			auto relative = root.GetRelativePathFor(resolved);
			auto delimiter = FilePath::GetPathDelimiter();
			if (relative.Length() == 0 || relative[0] == L'/' || relative[0] == L'\\') return false;
			if (relative.Length() >= 2 && relative[1] == L':') return false;
			if (relative == L"..") return false;
			if (relative.Length() >= 3 && relative[0] == L'.' && relative[1] == L'.' && relative[2] == delimiter) return false;

			filePath = resolved;
			return true;
		}

		WString GetContentType(const FilePath& filePath)
		{
			auto name = filePath.GetName();
			vint dot = -1;
			for (vint i = 0; i < name.Length(); i++)
			{
				if (name[i] == L'.') dot = i;
			}
			auto extension = dot == -1 ? WString::Empty : name.Right(name.Length() - dot);
			if (extension == L".html") return L"text/html; charset=utf-8";
			if (extension == L".css") return L"text/css; charset=utf-8";
			if (extension == L".js") return L"text/javascript; charset=utf-8";
			if (extension == L".json") return L"application/json; charset=utf-8";
			if (extension == L".svg") return L"image/svg+xml";
			return L"application/octet-stream";
		}

	protected:
		void OnHttpRequestReceived(Ptr<SocketHttpRequestContext> context) override
		{
#define ERROR_MESSAGE_PREFIX L"MiniHttpServer::PhysicalFolderHttpServer::OnHttpRequestReceived(Ptr<SocketHttpRequestContext>)#"
			auto method = context->GetRequest()->method;
			if (method != L"GET" && method != L"HEAD")
			{
				auto response = CreateStatusResponse(405, L"Method Not Allowed");
				response->headers.Add(CreateTextField(L"Allow", L"GET, HEAD"));
				context->Respond(response);
				return;
			}

			FilePath filePath;
			if (!TryResolveFile(context->GetRelativePath(), filePath) || !filePath.IsFile())
			{
				context->Respond(CreateStatusResponse(404, L"Not Found"));
				return;
			}

			stream::FileStream fileStream(filePath.GetFullPath(), stream::FileStream::ReadOnly);
			if (!fileStream.IsAvailable())
			{
				context->Respond(CreateStatusResponse(404, L"Not Found"));
				return;
			}
			auto fileSize = fileStream.Size();
			CHECK_ERROR(fileSize >= 0, ERROR_MESSAGE_PREFIX L"Failed to query the response file size.");
			if (fileSize > HttpBodySizeLimit)
			{
				context->Respond(CreateStatusResponse(413, L"Payload Too Large"));
				return;
			}

			auto response = Ptr(new HttpResponse);
			response->statusCode = 200;
			response->reason = L"OK";
			response->headers.Add(CreateTextField(L"Content-Type", GetContentType(filePath)));
			if (fileSize > 0)
			{
				HttpBodyChunk chunk;
				chunk.data.Resize((vint)fileSize);
				vint offset = 0;
				while (offset < fileSize)
				{
					auto read = fileStream.Read(&chunk.data[offset], (vint)fileSize - offset);
					CHECK_ERROR(read > 0, ERROR_MESSAGE_PREFIX L"Failed to read the complete response file.");
					offset += read;
				}
				response->body.chunks.Add(std::move(chunk));
			}
			context->Respond(response);
#undef ERROR_MESSAGE_PREFIX
		}

	public:
		PhysicalFolderHttpServer(const WString& urlPrefix, const FilePath& _root)
			: SocketHttpServerApi(urlPrefix, true)
			, root(_root)
		{
		}

		~PhysicalFolderHttpServer()
		{
			Stop();
		}
	};

	vint RunServer(const WString& websiteArgument, const WString& assetsArgument)
	{
		FilePath websiteRoot = websiteArgument;
		FilePath assetsRoot = assetsArgument;
		if (!Folder(websiteRoot).Exists())
		{
			Console::WriteLine(L"Website folder does not exist: " + websiteRoot.GetFullPath());
			return 1;
		}
		if (!Folder(assetsRoot).Exists())
		{
			Console::WriteLine(L"Assets folder does not exist: " + assetsRoot.GetFullPath());
			return 1;
		}

		PhysicalFolderHttpServer website(L"http://localhost:8888", websiteRoot);
		PhysicalFolderHttpServer assets(L"http://localhost:8889/Assets", assetsRoot);
		website.Start();
		assets.Start();
		Console::WriteLine(L"Website ready: " + website.GetUrlPrefix());
		Console::WriteLine(L"Assets ready: " + assets.GetUrlPrefix());
		Console::WriteLine(L"Press Enter to stop.");
		Console::TryRead();
		assets.Stop();
		website.Stop();
		return 0;
	}
}

#if defined VCZH_MSVC
int wmain(int argc, wchar_t* argv[])
#elif defined VCZH_GCC
int main(int argc, char* argv[])
#endif
{
	vint result = 1;
	try
	{
		if (argc != 3)
		{
			Console::WriteLine(L"Usage: MiniHttpServer <WebsiteFolder> <AssetsFolder>");
		}
		else
		{
#if defined VCZH_MSVC
			result = RunServer(argv[1], argv[2]);
#elif defined VCZH_GCC
			result = RunServer(atow(argv[1]), atow(argv[2]));
#endif
		}
	}
	catch (const Exception& exception)
	{
		Console::WriteLine(L"Error: " + exception.Message());
	}
	catch (const Error& error)
	{
		Console::WriteLine(L"Error: " + WString(error.Description()));
	}
	catch (...)
	{
		Console::WriteLine(L"Error: Unknown application failure.");
	}

#ifdef VCZH_GCC
	ThreadPoolLite::Stop(false);
#endif
	ThreadLocalStorage::DisposeStorages();
	FinalizeGlobalStorage();
#if defined VCZH_MSVC && defined VCZH_CHECK_MEMORY_LEAKS
	_CrtDumpMemoryLeaks();
#endif
	return (int)result;
}
