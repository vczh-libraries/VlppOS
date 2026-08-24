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

	class ResponseUploadState : public Object
	{
	public:
		atomic_vint								advanced = 0;
		vint									sequence = 0;
	};

	class ResponseCompletion : public Object
	{
	public:
		WString									body;
		vint									attempt = 0;
		Ptr<ResponseUploadState>						responseUploadState;
		Variant<HttpResponse, HttpError>				result;

		ResponseCompletion(WString _body, vint _attempt, Ptr<ResponseUploadState> _responseUploadState, Variant<HttpResponse, HttpError>&& _result)
			: body(std::move(_body))
			, attempt(_attempt)
			, responseUploadState(_responseUploadState)
			, result(std::move(_result))
		{
		}
	};

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

	collections::List<WString>						queuedResponseBodies;
	bool											responseUploadPending = false;
	bool											drainResponseUploads = false;
	bool											stopDrainingStarted = false;
	EventObject										eventResponseUploadsCompleted;
	vint											nextResponseSequence = 1;
	vint											nextResponseCompletion = 1;
	SpinLock										lockResponseCompletions;
	collections::Dictionary<vint, Ptr<ResponseCompletion>>	pendingResponseCompletions;
	bool											responseCompletionDraining = false;

	bool											SendHttpRequest(HttpRequestType requestType, const WString& url, const WString& body, vint attempt = 1, Ptr<ResponseUploadState> responseUploadState = {});
	bool											OnHttpRequestCompleted(HttpRequestType requestType, WString body, vint attempt, Ptr<ResponseUploadState> responseUploadState, Variant<HttpResponse, HttpError> result);
	bool											OnHttpRequestFailed(HttpRequestType requestType, const WString& body, vint attempt, Ptr<ResponseUploadState> responseUploadState, const WString& errorMessage);
	void											OnHttpResponseRequestSent();
	void											OnHttpResponseRequestCompleted(WString body, vint attempt, Ptr<ResponseUploadState> responseUploadState, Variant<HttpResponse, HttpError> result);
	void											DrainHttpResponseCompletions();

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
