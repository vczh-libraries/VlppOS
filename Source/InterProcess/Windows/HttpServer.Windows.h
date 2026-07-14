/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpServer

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_HTTPSERVER
#define VCZH_INTERPROCESS_WINDOWS_HTTPSERVER

#include "HttpServerApi.Windows.h"

namespace vl::inter_process::windows_http
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

class HttpServer : public HttpServerApi, public virtual INetworkProtocolServer
{
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

/***********************************************************************
HttpServer (BeginReadingLoopUnsafe)
***********************************************************************/

protected:


/***********************************************************************
HttpServer (HttpServerApi)
***********************************************************************/

protected:

	void											OnHttpRequestReceived(PHTTP_REQUEST pRequest) override;
	void											OnHttpServerStopping() override;

/***********************************************************************
HttpServer
***********************************************************************/

public:
	HttpServer(const WString _baseUrl, vint port);
	~HttpServer();
	
	WaitForClientResult								OnClientConnected(INetworkProtocolConnection* connection) override;
	void											Start() override;
	void											Stop() override;
	bool											IsStopped() override;
};

}

#endif
