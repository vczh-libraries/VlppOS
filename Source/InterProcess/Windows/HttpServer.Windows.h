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

constexpr const wchar_t* HttpServerUrl = L"localhost:8888";

/*
* GET: /Request
* To connect and initialize the server.
* Returns available URLs.
* 
* It can only be called once, all subsequence calls will be rejected.
*/
constexpr const wchar_t* HttpServerUrl_Connect = L"/GacUIRemoting/Connect";

/*
* POST: /Request/GUID
* Client should always maintain a living request on the server.
* 
* Returns only when a request is issued.
* It will be pending or timeout if no request is issued.
* If a request is issued but no living request available, it waits.
*/
constexpr const wchar_t* HttpServerUrl_Request = L"/GacUIRemoting/Request";

/*
* POST: /Response/GUID
* To send responses or events to the server.
* Returns nothing.
*/
constexpr const wchar_t* HttpServerUrl_Response = L"/GacUIRemoting/Response";

class HttpServer : public INetworkProtocol
{
	static constexpr vint32_t						HttpBodyInitSize = 1024;

protected:
	INetworkProtocolCoreCallback*					callback = nullptr;
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
	HttpServer();
	~HttpServer();

	void											Stop();

	void											InstallCallback(INetworkProtocolCallback* _callback) override;
};

}

#endif