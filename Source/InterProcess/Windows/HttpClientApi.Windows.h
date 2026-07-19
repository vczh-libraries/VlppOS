/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpClientApi

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_HTTPCLIENTAPI
#define VCZH_INTERPROCESS_WINDOWS_HTTPCLIENTAPI

#include "NetworkProtocol.Windows.h"

namespace vl::inter_process::windows_http
{

/// <summary>A Windows-only async HTTP client for a single host and port.</summary>
class HttpClientApi : public Object
{
	static constexpr vint32_t							HttpRespondBodyStep = 65536;

	class HttpRequestContext : public Object
	{
	public:
		HttpClientApi*									api = nullptr;
		HINTERNET										httpRequest = NULL;
		Func<void(Variant<HttpResponse, HttpError>)>	callback;
		collections::Array<char>						requestBody;
		HttpResponse									response;
		DWORD											bodyBufferWriting = 0;
		DWORD											bodyBufferWritingAvailable = 0;
		bool											keepAliveOnStop = false;
		bool											completed = false;
		bool											closing = false;
		SpinLock										lockContext;
	};

	WString												server;
	vint												port = 0;
	HINTERNET											httpSession = NULL;
	HINTERNET											httpConnection = NULL;

	SpinLock											lockActiveRequests;
	collections::List<Ptr<HttpRequestContext>>			activeRequests;
	bool												stopping = false;
	atomic_vint											pendingCallbacks = 0;
	EventObject											eventPendingCallbacks;

	static void CALLBACK								HttpStatusCallback(HINTERNET httpRequest, DWORD_PTR context, DWORD status, LPVOID statusInformation, DWORD statusInformationLength);
	static HttpError									MakeError(const WString& operation, DWORD errorCode);

	bool												IsStopping();
	void												BeginPendingCallback();
	void												EndPendingCallback();
	void												AttachRequestUnsafe(Ptr<HttpRequestContext> context);
	void												RemoveRequestUnsafe(Ptr<HttpRequestContext> context);
	void												CloseRequest(Ptr<HttpRequestContext> context);
	void												OnRequestHandleClosing(Ptr<HttpRequestContext> context);
	void												CompleteRequest(Ptr<HttpRequestContext> context, HttpResponse&& response);
	void												CompleteRequest(Ptr<HttpRequestContext> context, HttpError&& error);
	void												CompleteRequestWithLastError(Ptr<HttpRequestContext> context, const WString& operation, DWORD errorCode);

public:
	HttpClientApi(const WString& _server, vint _port);
	~HttpClientApi();

	HttpClientApi(const HttpClientApi&) = delete;
	HttpClientApi(HttpClientApi&&) = delete;
	HttpClientApi& operator=(const HttpClientApi&) = delete;
	HttpClientApi& operator=(HttpClientApi&&) = delete;

	void												HttpQuery(const HttpRequest& request, Func<void(Variant<HttpResponse, HttpError>)> callback);
	void												AbortRequests();
	void												Stop(bool keepAliveRequests = true);

	static WString										UrlEncodeQuery(const WString& query);
	static WString										UrlDecodeQuery(const WString& query);
};

}

#endif
