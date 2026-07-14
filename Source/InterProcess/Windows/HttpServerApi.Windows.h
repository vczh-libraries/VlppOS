/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpServerApi

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_HTTPSERVERAPI
#define VCZH_INTERPROCESS_WINDOWS_HTTPSERVERAPI

#include "NetworkProtocol.Windows.h"

namespace vl::inter_process::windows_http
{

/// <summary>A response to be sent by <see cref="HttpServerApi"/>.</summary>
struct HttpServerResponse
{
	vint											statusCode = 200;
	WString											reason;
	WString											body;
	WString											contentType;
};

/// <summary>A Windows-only async HTTP server for a single URL prefix.</summary>
class HttpServerApi : public Object
{
	static constexpr vint32_t						HttpRequestBufferInitSize = 1024;

protected:
	enum class State
	{
		Ready,
		Running,
		Stopping,
	};

	WString											urlPrefix;
	bool											respondToOptions = false;

	HANDLE											httpRequestQueue = INVALID_HANDLE_VALUE;
	HTTP_SERVER_SESSION_ID							httpSessionId = HTTP_NULL_ID;
	HTTP_URL_GROUP_ID								httpUrlGroupId = HTTP_NULL_ID;

	State											state = State::Ready;

	collections::Array<BYTE>						bufferRequest;
	HANDLE											hWaitHandleRequest = INVALID_HANDLE_VALUE;
	OVERLAPPED										overlappedRequest;
	HANDLE											hEventRequest = INVALID_HANDLE_VALUE;
	EventObject										eventPendingCallbacks;
	atomic_vint										pendingCallbacks = 0;

	void											OnHttpConnectionBrokenUnsafe();
	void											OnHttpRequestReceivedUnsafe(PHTTP_REQUEST pRequest);
	ULONG											ListenToHttpRequest_Init(OVERLAPPED* overlapped);
	ULONG											ListenToHttpRequest_InitMoreData(ULONG* bytesReturned);
	ULONG											ListenToHttpRequest_OverlappedMoreData(vint expectedBufferSize);
	void											ListenToHttpRequest();
	void											BeginPendingCallback();
	void											EndPendingCallback();

	virtual void									OnHttpRequestReceived(PHTTP_REQUEST pRequest) = 0;
	virtual void									OnHttpServerStopping();

	static void										SendOptionsResponse(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId);

public:
	HttpServerApi(const WString& _urlPrefix, bool _respondToOptions);
	~HttpServerApi();

	HttpServerApi(const HttpServerApi&) = delete;
	HttpServerApi(HttpServerApi&&) = delete;
	HttpServerApi& operator=(const HttpServerApi&) = delete;
	HttpServerApi& operator=(HttpServerApi&&) = delete;

	void											Start();
	void											Stop();
	bool											IsStopped();
	HANDLE											GetHttpRequestQueue() const;

	Nullable<WString>								GetUtf8Body(PHTTP_REQUEST pRequest);
	static ULONG									SendResponse(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, const HttpServerResponse& response);
	static void										SendResponseUtf8(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, WString body);
};

}

#endif
