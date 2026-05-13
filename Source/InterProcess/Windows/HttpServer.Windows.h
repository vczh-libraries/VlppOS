/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpServer

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_HTTPSERVER
#define VCZH_INTERPROCESS_WINDOWS_HTTPSERVER

#include "NetworkProtocol.Windows.h"

namespace vl::inter_process
{

class HttpServer : public INetworkProtocol
{
	static constexpr vint32_t						HttpBodyInitSize = 1024;

protected:
	INetworkProtocolCallback*						callback = nullptr;
	WString											baseUrl;
	WString											urlConnect;
	WString											urlRequest;
	WString											urlResponse;

	HANDLE											httpRequestQueue = INVALID_HANDLE_VALUE;
	HTTP_SERVER_SESSION_ID							httpSessionId = HTTP_NULL_ID;
	HTTP_URL_GROUP_ID								httpUrlGroupId = HTTP_NULL_ID;

/***********************************************************************
HttpServer (ListenToHttpRequest)
***********************************************************************/

protected:

	enum class State
	{
		Ready,
		WaitForClientConnection,
		Running,
		Stopping,
	};

	State											state = State::Ready;

	collections::Array<BYTE>						bufferRequest;
	HANDLE											hWaitHandleRequest = INVALID_HANDLE_VALUE;
	OVERLAPPED										overlappedRequest;
	HANDLE											hEventRequest = INVALID_HANDLE_VALUE;

	void											OnHttpConnectionBrokenUnsafe();
	void											OnHttpRequestReceivedUnsafe(PHTTP_REQUEST pRequest);
	ULONG											ListenToHttpRequest_Init(OVERLAPPED* overlapped);
	ULONG											ListenToHttpRequest_InitMoreData(ULONG* bytesReturned);
	ULONG											ListenToHttpRequest_OverlappedMoreData(vint expectedBufferSize);
	void											ListenToHttpRequest();

/***********************************************************************
HttpServer (WaitForClient)
***********************************************************************/

protected:
	HANDLE											hEventWaitForClient = INVALID_HANDLE_VALUE;

	void											GenerateNewUrls();
	void											SendConnectResponse(PHTTP_REQUEST pRequest);

public:

	void											WaitForClient();

/***********************************************************************
HttpServer (BeginReadingLoopUnsafe)
***********************************************************************/

protected:

	void											SubmitResponse(PHTTP_REQUEST pRequest);

public:
	void											BeginReadingLoopUnsafe() override;

/***********************************************************************
HttpServer (Writing)
***********************************************************************/

protected:
	using PendingRequestObject = Nullable<collections::Pair<WString, WString>>;

	SpinLock										pendingRequestLock;
	HTTP_REQUEST_ID									httpPendingRequestId = HTTP_NULL_ID;
	PendingRequestObject							pendingRequestToSend;

	static void										Send404Response(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, PCSTR reason);
	static void										SendOptionsResponse(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId);
	static ULONG									SendResponse(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, const WString& channelName, const WString& str);

	// All following functions must be called inside SPIN_LOCK(pendingRequestLock)
	void											OnCancelCurrentHttpRequestForPendingRequest();
	void											OnNewHttpRequestForPendingRequest(HTTP_REQUEST_ID httpRequestId);
public:

	void											SendString(const WString& channelName, const WString& str) override;

/***********************************************************************
HttpServer
***********************************************************************/

public:
	HttpServer(const WString _baseUrl, vint port);
	~HttpServer();

	void											Stop();

	void											InstallCallback(INetworkProtocolCallback* _callback) override;
};

}

#endif