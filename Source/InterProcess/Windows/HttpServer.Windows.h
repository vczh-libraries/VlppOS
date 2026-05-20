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

class HttpServer;

class HttpServerConnection : public Object, public virtual INetworkProtocolConnection
{
	friend class HttpServer;
protected:
	HttpServer*										server = nullptr;
	WString											guid;
	INetworkProtocolCallback*						callback = nullptr;

	SpinLock										lockQueuedStrings;
	collections::List<WString>						queuedStrings;

	SpinLock										pendingRequestLock;
	HTTP_REQUEST_ID									httpPendingRequestId = HTTP_NULL_ID;
	collections::List<WString>						pendingRequestsToSend;
	bool											submittingResponse = false;
	collections::List<WString>						responsesToSubmit;

	// All following functions must be called inside SPIN_LOCK(pendingRequestLock)
	void											OnCancelCurrentHttpRequestForPendingRequest();
	void											OnNewHttpRequestForPendingRequest(HTTP_REQUEST_ID httpRequestId);

	WString											SubmitResponse(PHTTP_REQUEST pRequest);

public:
	void											InstallCallback(INetworkProtocolCallback* _callback) override;
	void											BeginReadingLoopUnsafe() override;
	void											SendString(const WString& str) override;
	void											Stop() override;

	static WString									GenerateNewGuid();
};

class HttpServer : public Object, public virtual INetworkProtocolServer
{
	static constexpr vint32_t						HttpBodyInitSize = 1024;

	friend class HttpServerConnection;
	using ConnectionMap = collections::Dictionary<WString, Ptr<HttpServerConnection>>;
protected:
	WString											baseUrl;
	WString											urlConnect;
	WString											urlRequestPrefix;
	WString											urlResponsePrefix;

	// covers connections
	SpinLock										lockConnections;
	ConnectionMap									connections;

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
HttpServer (BeginReadingLoopUnsafe)
***********************************************************************/

protected:


/***********************************************************************
HttpServer (Writing)
***********************************************************************/

protected:

	static void										Send404Response(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, PCSTR reason);
	static void										SendOptionsResponse(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId);
	static ULONG									SendResponse(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, const WString& str);

/***********************************************************************
HttpServer
***********************************************************************/

public:
	HttpServer(const WString _baseUrl, vint port);
	~HttpServer();
	
	WaitForClientResult								OnClientConnected(INetworkProtocolConnection* connection) override;
	void											Stop() override;
	bool											IsStopped() override;
};

}

#endif
