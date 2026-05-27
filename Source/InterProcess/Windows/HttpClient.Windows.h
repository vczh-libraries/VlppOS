/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpClient

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_HTTPCLIENT
#define VCZH_INTERPROCESS_WINDOWS_HTTPCLIENT

#include "HttpClientApi.Windows.h"

namespace vl::inter_process
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

	State											state = State::Ready;
	INetworkProtocolCallback*						callback = nullptr;
	WString											baseUrl;
	Ptr<HttpClientApi>								httpClientApi;
	WString											urlConnect;
	WString											urlRequest;
	WString											urlResponse;
	SpinLock										lockState;

/***********************************************************************
HttpClient (Reading)
***********************************************************************/

protected:
	static constexpr const wchar_t*					JsonContentType = L"application/json; charset=utf8";

	void											RaiseLocalError(WString errorMessage, bool fatal);
	bool											IsStopping();
public:

	void											BeginReadingLoopUnsafe() override;

/***********************************************************************
HttpClient (WaitForServer)
***********************************************************************/

protected:

	EventObject										eventWaitForServer;
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

	bool											SendHttpRequest(HttpRequestType requestType, const wchar_t* method, const WString& url, const WString& body, vint attempt = 1);
	void											OnHttpRequestCompleted(HttpRequestType requestType, WString body, vint attempt, Variant<HttpResponse, HttpError> result);
	void											OnHttpRequestFailed(HttpRequestType requestType, const WString& body, vint attempt, const WString& errorMessage);

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
