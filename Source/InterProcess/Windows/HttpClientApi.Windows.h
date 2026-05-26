/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpClientApi

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_HTTPCLIENTAPI
#define VCZH_INTERPROCESS_WINDOWS_HTTPCLIENTAPI

#include "NetworkProtocol.Windows.h"

namespace vl::inter_process
{

/// <summary>An http request.</summary>
class HttpRequest
{
	typedef collections::Array<char>					BodyBuffer;
	typedef collections::List<WString>					StringList;
	typedef collections::Dictionary<WString, WString>	HeaderMap;
public:
	/// <summary>Query of the request, like "/index.html".</summary>
	WString												query;
	/// <summary>Set to true if the request uses SSL, or https.</summary>
	bool												secure = false;
	/// <summary>User name to authorize. Set to empty if authorization is not needed.</summary>
	WString												username;
	/// <summary>Password to authorize. Set to empty if authorization is not needed.</summary>
	WString												password;
	/// <summary>HTTP method, like "GET", "POST", "PUT", "DELETE", etc.</summary>
	WString												method;
	/// <summary>Cookie. Set to empty if cookie is not needed.</summary>
	WString												cookie;
	/// <summary>Request body. This is a byte array.</summary>
	BodyBuffer											body;
	/// <summary>Content type, like "text/xml".</summary>
	WString												contentType;
	/// <summary>Accept type list, elements like "text/xml".</summary>
	StringList											acceptTypes;
	/// <summary>A dictionary to contain extra headers.</summary>
	HeaderMap											extraHeaders;
	/// <summary>Set to true to let this request finish when <see cref="HttpClientApi.Stop"/> is called.</summary>
	bool												keepAliveOnStop = false;

	HttpRequest() = default;
	void												SetBodyUtf8(const WString& bodyString);
};

/// <summary>A type representing an http response.</summary>
class HttpResponse
{
	typedef collections::Array<char>					BodyBuffer;
public:
	/// <summary>Status code, like 200.</summary>
	vint												statusCode = 0;
	/// <summary>Response body. This is a byte array.</summary>
	BodyBuffer											body;
	/// <summary>Returned cookie from the server.</summary>
	WString												cookie;
	/// <summary>Returned content type from the server.</summary>
	WString												contentType;

	HttpResponse() = default;
	WString												GetBodyUtf8() const;
};

/// <summary>A transport error reported by the underlying Windows HTTP API.</summary>
class HttpError
{
public:
	DWORD												errorCode = 0;
	WString												operation;
	WString												message;
};

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
	static vint											HexValue(wchar_t c);

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
	void												Stop();

	static WString										HttpEncodeQuery(const WString& query);
	static WString										HttpDecodeQuery(const WString& query);
};

extern WString											UrlEncodeQuery(const WString& query);

}

#endif
