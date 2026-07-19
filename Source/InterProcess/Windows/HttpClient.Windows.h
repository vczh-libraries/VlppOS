/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpClient

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_HTTPCLIENT
#define VCZH_INTERPROCESS_WINDOWS_HTTPCLIENT

#include "HttpClientApi.Windows.h"

namespace vl::inter_process::windows_http
{

class HttpClient : public Object, public virtual INetworkProtocolConnection, public virtual INetworkProtocolClient
{
protected:
	static constexpr vint							HttpRequestMaxAttempts = 3;

	enum class State
	{
		Ready,
		WaitForServerConnection,
		Running,
		Stopping,
	};

	class StopState : public Object
	{
	public:
		SpinLock									lock;
		EventObject								eventWaitForServer;
		EventObject								eventSchedulingFinished;
		EventObject								eventCallbacksFinished;
		EventObject								eventCallbackChanged;
		EventObject								eventFinished;
		Ptr<HttpClientApi>							stoppingApi;
		INetworkProtocolCallback*					disconnectedCallback = nullptr;
		vint									activeCallbacks = 0;
		bool									started = false;
		bool									scheduling = false;
		bool									executorClaimed = false;
		bool									abortRequests = false;
		bool									callbacksClosed = false;
		bool									suppressDisconnected = false;
		bool									disconnectDelivering = false;
		bool									finished = false;

		StopState();
	};

	State											state = State::Ready;
	INetworkProtocolCallback*						callback = nullptr;
	WString											baseUrl;
	Ptr<HttpClientApi>								httpClientApi;
	WString											urlConnect;
	WString											urlRequest;
	WString											urlResponse;
	SpinLock										lockState;
	Ptr<StopState>									stopState;

/***********************************************************************
HttpClient (Reading)
***********************************************************************/

protected:
	void											RaiseLocalError(WString errorMessage, bool fatal);
	bool											IsStopping();
	void											StopCore(bool callbackReentrant);
	void											StopFromCallback();
	static void									CompleteStop(Ptr<StopState> state);
	static bool									BeginHttpCallback(Ptr<StopState> state);
	static void									EndHttpCallback(Ptr<StopState> state);
	vint											CurrentHttpCallbackDepth();
public:

	void											BeginReadingLoopUnsafe() override;

/***********************************************************************
HttpClient (WaitForServer)
***********************************************************************/

protected:

	SpinLock										lockConnectResult;
	bool											connectCompleted = false;
	WString											connectResponse;
	WString											connectError;
	void											CompleteConnectRequest(const WString& response, const WString& error);

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

	bool											SendHttpRequest(HttpRequestType requestType, const WString& url, const WString& body, vint attempt = 1);
	void											OnHttpRequestCompleted(HttpRequestType requestType, WString body, vint attempt, Variant<HttpResponse, HttpError> result);
	void											OnHttpRequestFailed(HttpRequestType requestType, const WString& body, vint attempt, const WString& errorMessage, bool remoteUnavailable = false);

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
