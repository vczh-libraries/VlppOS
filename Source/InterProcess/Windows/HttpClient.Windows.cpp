#include "HttpClient.Windows.h"

namespace vl::inter_process::windows_http
{

/***********************************************************************
HttpClient (Reading)
***********************************************************************/

void HttpClient::RaiseLocalError(WString errorMessage, bool fatal)
{
	if (callback)
	{
		callback->OnLocalError(errorMessage, fatal);
	}
	if (fatal)
	{
		Stop();
	}
}

bool HttpClient::IsStopping()
{
	bool result = false;
	SPIN_LOCK(lockState)
	{
		result = state == State::Stopping;
	}
	return result;
}

void HttpClient::BeginReadingLoopUnsafe()
{
	SendHttpRequest(HttpRequestType::Request, urlRequest, WString::Empty);
}

/***********************************************************************
HttpClient (WaitForServer)
***********************************************************************/

INetworkProtocolConnection* HttpClient::GetConnection()
{
	return this;
}

void HttpClient::CompleteConnectRequest(const WString& response, const WString& error)
{
	SPIN_LOCK(lockConnectResult)
	{
		connectResponse = response;
		connectError = error;
		connectCompleted = true;
	}
	eventWaitForServer.Signal();
}

void HttpClient::WaitForServer()
{
	{
		SPIN_LOCK(lockState)
		{
			if (state == State::Stopping) return;
			CHECK_ERROR(state == State::Ready, L"WaitForServer can only be called once.");
			state = State::WaitForServerConnection;
		}
	}

	{
		SPIN_LOCK(lockConnectResult)
		{
			connectCompleted = false;
			connectResponse = WString::Empty;
			connectError = WString::Empty;
		}
	}

	eventWaitForServer.Unsignal();
	if (!SendHttpRequest(HttpRequestType::Connect, urlConnect, WString::Empty))
	{
		return;
	}

	eventWaitForServer.Wait();
	if (IsStopping()) return;

	WString body;
	WString error;
	bool completed = false;
	{
		SPIN_LOCK(lockConnectResult)
		{
			body = connectResponse;
			error = connectError;
			completed = connectCompleted;
		}
	}

	CHECK_ERROR(completed, L"/Connect did not complete.");
	CHECK_ERROR(error == WString::Empty, L"/Connect failed.");

	vint separatorIndex = body.IndexOf(L';');
	CHECK_ERROR(separatorIndex != -1, L"/Connect response body is not in the correct format: requestUrl;responseUrl.");
	urlRequest = baseUrl + body.Left(separatorIndex);
	urlResponse = baseUrl + body.Right(body.Length() - separatorIndex - 1);
	{
		SPIN_LOCK(lockState)
		{
			if (state == State::Stopping) return;
			state = State::Running;
		}
	}

	if (callback)
	{
		callback->OnConnected();
	}
}

ClientStatus HttpClient::GetStatus()
{
	ClientStatus result = ClientStatus::Disconnected;
	SPIN_LOCK(lockState)
	{
		switch (state)
		{
		case State::Ready:
			result = ClientStatus::Ready;
			break;
		case State::WaitForServerConnection:
			result = ClientStatus::WaitingForServer;
			break;
		case State::Running:
			result = ClientStatus::Connected;
			break;
		default:
			result = ClientStatus::Disconnected;
			break;
		}
	}
	return result;
}

/***********************************************************************
HttpClient (Writing)
***********************************************************************/

bool HttpClient::SendHttpRequest(HttpRequestType requestType, const WString& url, const WString& body, vint attempt)
{
	Ptr<HttpClientApi> api;
	{
		SPIN_LOCK(lockState)
		{
			if (state == State::Stopping) return false;
			switch (requestType)
			{
			case HttpRequestType::Connect:
				CHECK_ERROR(state == State::WaitForServerConnection, L"/Connect can only be called when client is waiting for the server.");
				break;
			case HttpRequestType::Request:
				CHECK_ERROR(state == State::Running, L"/Request can only be called when client is running.");
				break;
			case HttpRequestType::Response:
				CHECK_ERROR(state == State::Running, L"/Response can only be called when client is running.");
				break;
			}
			api = httpClientApi;
		}
	}

	if (!api) return false;

	HttpRequest encodedBody;
	if (requestType == HttpRequestType::Response)
	{
		encodedBody.SetBodyUtf8(body);
	}

	HttpRequest request;
	switch (requestType)
	{
	case HttpRequestType::Connect:
		request = CreateHttpNetworkProtocolConnectRequest(url);
		break;
	case HttpRequestType::Request:
		request = CreateHttpNetworkProtocolReceiveRequest(url);
		request.receiveTimeout = 0;
		break;
	case HttpRequestType::Response:
		request = CreateHttpNetworkProtocolSendRequest(url, encodedBody.body);
		request.keepAliveOnStop = true;
		break;
	}

	api->HttpQuery(request, [this, requestType, body, attempt](Variant<HttpResponse, HttpError> result)
	{
		OnHttpRequestCompleted(requestType, body, attempt, std::move(result));
	});
	return true;
}

void HttpClient::OnHttpRequestFailed(HttpRequestType requestType, const WString& body, vint attempt, const WString& errorMessage)
{
	if (IsStopping()) return;

	switch (requestType)
	{
	case HttpRequestType::Connect:
		{
			bool fatal = attempt >= HttpRequestMaxAttempts;
			RaiseLocalError(errorMessage, fatal);
			if (!fatal && !IsStopping())
			{
				SendHttpRequest(HttpRequestType::Connect, urlConnect, WString::Empty, attempt + 1);
			}
		}
		break;
	case HttpRequestType::Request:
		SendHttpRequest(HttpRequestType::Request, urlRequest, WString::Empty, attempt + 1);
		break;
	case HttpRequestType::Response:
		{
			bool fatal = attempt >= HttpRequestMaxAttempts;
			RaiseLocalError(errorMessage, fatal);
			if (!fatal && !IsStopping())
			{
				SendHttpRequest(HttpRequestType::Response, urlResponse, body, attempt + 1);
			}
		}
		break;
	}
}

void HttpClient::OnHttpRequestCompleted(HttpRequestType requestType, WString body, vint attempt, Variant<HttpResponse, HttpError> result)
{
	if (auto error = result.TryGet<HttpError>())
	{
		switch (requestType)
		{
		case HttpRequestType::Connect:
			OnHttpRequestFailed(requestType, body, attempt, L"/Connect failed: " + error->message);
			break;
		case HttpRequestType::Request:
			OnHttpRequestFailed(requestType, body, attempt, L"/Request failed: " + error->message);
			break;
		case HttpRequestType::Response:
			OnHttpRequestFailed(requestType, body, attempt, L"/Response failed: " + error->message);
			break;
		}
		return;
	}

	auto&& response = result.Get<HttpResponse>();
	if (response.statusCode != 200)
	{
		switch (requestType)
		{
		case HttpRequestType::Connect:
			OnHttpRequestFailed(requestType, body, attempt, WString::Unmanaged(L"/Connect returned status code: ") + itow(response.statusCode) + L".");
			break;
		case HttpRequestType::Request:
			OnHttpRequestFailed(requestType, body, attempt, WString::Unmanaged(L"/Request returned status code: ") + itow(response.statusCode) + L", another renderer may have connected to the core.");
			break;
		case HttpRequestType::Response:
			OnHttpRequestFailed(requestType, body, attempt, WString::Unmanaged(L"/Response returned status code: ") + itow(response.statusCode) + L", another renderer may have connected to the core.");
			break;
		}
		return;
	}

	if (response.contentType != HttpNetworkProtocolContentType)
	{
		switch (requestType)
		{
		case HttpRequestType::Connect:
			OnHttpRequestFailed(requestType, body, attempt, L"/Connect response did not return content type: application/json; charset=utf8.");
			break;
		case HttpRequestType::Request:
			OnHttpRequestFailed(requestType, body, attempt, L"/Request response did not return content type: application/json; charset=utf8.");
			break;
		case HttpRequestType::Response:
			OnHttpRequestFailed(requestType, body, attempt, L"/Response response did not return content type: application/json; charset=utf8.");
			break;
		}
		return;
	}

	auto responseBody = response.GetBodyUtf8();
	switch (requestType)
	{
	case HttpRequestType::Connect:
		CompleteConnectRequest(responseBody, WString::Empty);
		break;
	case HttpRequestType::Request:
		if (!IsStopping())
		{
			BeginReadingLoopUnsafe();
			if (responseBody.Length() > 0 && callback)
			{
				callback->OnReadString(responseBody);
			}
		}
		break;
	case HttpRequestType::Response:
		if (!IsStopping() && responseBody.Length() > 0 && callback)
		{
			callback->OnReadString(responseBody);
		}
		break;
	}
}

void HttpClient::SendString(const WString& str)
{
	SendHttpRequest(HttpRequestType::Response, urlResponse, str);
}

/***********************************************************************
HttpClient
***********************************************************************/

HttpClient::HttpClient(const WString _baseUrl, vint port)
	: baseUrl(_baseUrl)
{
	CHECK_ERROR(eventWaitForServer.CreateAutoUnsignal(false), L"HttpClient initialization failed on eventWaitForServer.CreateAutoUnsignal.");

	httpClientApi = Ptr(new HttpClientApi(L"localhost", port));
	urlConnect = baseUrl + HttpServerUrl_Connect;
}

HttpClient::~HttpClient()
{
	Stop();
}

void HttpClient::InstallCallback(INetworkProtocolCallback* _callback)
{
	CHECK_ERROR(!callback || !_callback, L"HttpClient::InstallCallback only accepts one callback at a time.");
	callback = _callback;
	if (!callback) return;
	callback->OnInstalled(this);
}

void HttpClient::Stop()
{
	Ptr<HttpClientApi> stoppingApi;
	bool notifyDisconnected = false;
	{
		SPIN_LOCK(lockState)
		{
			if (httpClientApi)
			{
				state = State::Stopping;
				stoppingApi = httpClientApi;
				httpClientApi = nullptr;
				notifyDisconnected = true;
			}
			else
			{
				state = State::Stopping;
			}
		}
	}

	eventWaitForServer.Signal();
	if (stoppingApi)
	{
		stoppingApi->Stop();
	}

	if (notifyDisconnected && callback)
	{
		callback->OnDisconnected();
	}
}

}
