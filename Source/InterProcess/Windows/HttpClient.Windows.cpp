#include "HttpClient.Windows.h"

namespace vl::inter_process
{

/***********************************************************************
HttpClient (Reading)
***********************************************************************/

void HttpClient::RaiseErrorUnsafe(WString errorMessage)
{
	if (callback)
	{
		callback->OnReadError(errorMessage);
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
	SendHttpRequest(HttpRequestType::Request, L"POST", urlRequest, WString::Empty);
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
	if (!SendHttpRequest(HttpRequestType::Connect, L"GET", urlConnect, WString::Empty))
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

bool HttpClient::SendHttpRequest(HttpRequestType requestType, const wchar_t* method, const WString& url, const WString& body)
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

	HttpRequest request;
	request.method = method;
	request.query = url;
	request.acceptTypes.Add(JsonContentType);
	if (requestType == HttpRequestType::Response)
	{
		request.contentType = JsonContentType;
		request.keepAliveOnStop = true;
	}
	if (body.Length() > 0)
	{
		request.contentType = JsonContentType;
		request.SetBodyUtf8(body);
	}

	api->HttpQuery(request, [this, requestType](Variant<HttpResponse, HttpError> result)
	{
		OnHttpRequestCompleted(requestType, std::move(result));
	});
	return true;
}

void HttpClient::OnHttpRequestCompleted(HttpRequestType requestType, Variant<HttpResponse, HttpError> result)
{
	if (auto error = result.TryGet<HttpError>())
	{
		switch (requestType)
		{
		case HttpRequestType::Connect:
			CompleteConnectRequest(WString::Empty, error->message);
			break;
		case HttpRequestType::Request:
			if (!IsStopping())
			{
				RaiseErrorUnsafe(L"/Request failed: " + error->message);
			}
			break;
		case HttpRequestType::Response:
			if (!IsStopping())
			{
				RaiseErrorUnsafe(L"/Response failed: " + error->message);
			}
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
			CompleteConnectRequest(WString::Empty, WString::Unmanaged(L"/Connect returned status code: ") + itow(response.statusCode) + L".");
			break;
		case HttpRequestType::Request:
			if (!IsStopping())
			{
				RaiseErrorUnsafe(WString::Unmanaged(L"/Request returned status code: ") + itow(response.statusCode) + L", another renderer may have connected to the core.");
			}
			break;
		case HttpRequestType::Response:
			if (!IsStopping())
			{
				RaiseErrorUnsafe(WString::Unmanaged(L"/Response returned status code: ") + itow(response.statusCode) + L", another renderer may have connected to the core.");
			}
			break;
		}
		return;
	}

	if (response.contentType != JsonContentType)
	{
		switch (requestType)
		{
		case HttpRequestType::Connect:
			CompleteConnectRequest(WString::Empty, L"HTTP response did not return content type: application/json; charset=utf8.");
			break;
		case HttpRequestType::Request:
			if (!IsStopping())
			{
				CHECK_FAIL(L"HTTP response did not return content type: application/json; charset=utf8.");
			}
			break;
		case HttpRequestType::Response:
			if (!IsStopping())
			{
				CHECK_FAIL(L"HTTP response did not return content type: application/json; charset=utf8.");
			}
			break;
		}
		return;
	}

	auto body = response.GetBodyUtf8();
	switch (requestType)
	{
	case HttpRequestType::Connect:
		CompleteConnectRequest(body, WString::Empty);
		break;
	case HttpRequestType::Request:
		if (!IsStopping())
		{
			BeginReadingLoopUnsafe();
			if (body.Length() > 0 && callback)
			{
				callback->OnReadString(body);
			}
		}
		break;
	case HttpRequestType::Response:
		if (!IsStopping() && body.Length() > 0 && callback)
		{
			callback->OnReadString(body);
		}
		break;
	}
}

void HttpClient::SendString(const WString& str)
{
	SendHttpRequest(HttpRequestType::Response, L"POST", urlResponse, str);
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
	callback = _callback;
	CHECK_ERROR(callback, L"HttpClient::InstallCallback needs a valid INetworkProtocolCallback.");
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
