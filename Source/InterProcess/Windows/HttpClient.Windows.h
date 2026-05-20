/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpClient

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_HTTPCLIENT
#define VCZH_INTERPROCESS_WINDOWS_HTTPCLIENT

#include "NetworkProtocol.Windows.h"

namespace vl::inter_process
{

class HttpClient : public Object, public virtual INetworkProtocolConnection, public virtual INetworkProtocolClient
{
protected:

	enum class State
	{
		Ready,
		WaitForServerConnection,
		Running,
		Stopping,
	};

	State											state = State::Ready;
	INetworkProtocolCallback*						callback = nullptr;
	WString											baseUrl;

	HINTERNET										httpSession = NULL;
	HINTERNET										httpConnection = NULL;
	WString											urlConnect;
	WString											urlRequest;
	WString											urlResponse;

	atomic_vint										pendingCallbacks = 0;
	atomic_vint										createdRequestIds = 0;
	EventObject										eventPendingCallbacks;
	void											BeginPendingCallback();
	void											EndPendingCallback();

/***********************************************************************
HttpClient (Reading)
***********************************************************************/

protected:
	static constexpr vint32_t						HttpRespondBodyStep = 65536;

	void											RaiseErrorUnsafe(WString errorMessage);
public:

	void											BeginReadingLoopUnsafe() override;

/***********************************************************************
HttpClient (WaitForServer)
***********************************************************************/

protected:

	EventObject										eventWaitForServer;

public:
	
	INetworkProtocolConnection*						GetConnection() override;
	void											WaitForServer() override;
	ClientStatus									GetStatus() override;

/***********************************************************************
HttpClient (Writing)
***********************************************************************/

protected:
	enum class HttpRequestType
	{
		Connect,
		Request,
		Response,
	};

	class HttpResponseReading : public Object
	{
	public:
		collections::Array<char8_t>					bodyBuffer;
		DWORD										bodyBufferWriting = 0;
		DWORD										bodyBufferWritingAvailable = 0;
	};

	class HttpRequestContext : public Object
	{
	public:
		HttpClient*									client = nullptr;
		HttpRequestType								requestType = HttpRequestType::Connect;
		HINTERNET									httpRequest = NULL;
		vint										requestId = 0;
		U8String									requestBody;
		Ptr<HttpResponseReading>					responseReading;
		WString										connectResponse;
		WString										connectError;
		bool										connectCompleted = false;
	};

	class HttpActiveRequest
	{
	public:
		HINTERNET									httpRequest = NULL;
		vint										requestId = -1;
		HttpRequestType								requestType = HttpRequestType::Connect;
	};

	SpinLock										httpActiveRequestsLock;
	collections::List<HttpActiveRequest>			httpActiveRequests;

	vint											FindActiveRequestUnsafe(HINTERNET httpRequest, vint requestId);
	void											AttachRequestUnsafe(HINTERNET httpRequest, vint requestId, HttpRequestType requestType);
	void											CloseRequest(HINTERNET httpRequest, vint requestId = 0);
	void											OnRequestHandleClosing(HINTERNET httpRequest, vint requestId = 0);
	Ptr<HttpRequestContext>							SendHttpRequest(HttpRequestType requestType, const wchar_t* method, const WString& url, const WString& body);
	void											OnHttpRequestBodyReceived(Ptr<HttpRequestContext> context);

public:

	void											SendString(const WString& str) override;

/***********************************************************************
HttpClient
***********************************************************************/

public:
	HttpClient(const WString _baseUrl, vint port);
	~HttpClient();

	void											InstallCallback(INetworkProtocolCallback* _callback) override;
	void											Stop() override;
};

}

#endif
