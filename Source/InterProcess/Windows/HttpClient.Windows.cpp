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
		fatal = callback->OnLocalError(errorMessage, fatal) || fatal;
	}
	if (fatal)
	{
		// HttpClientApi::Stop waits for WinHTTP callbacks and cannot run from
		// this completion callback. Publishing Stopping suppresses every retry;
		// an explicit or owning Stop drains the physical API after unwinding.
		SPIN_LOCK(lockState)
		{
			state = State::Stopping;
		}
		eventWaitForServer.Signal();
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

bool HttpClient::SendHttpRequest(HttpRequestType requestType, const WString& url, const WString& body, vint attempt, Ptr<ResponseUploadState> responseUploadState)
{
	Ptr<HttpClientApi> api;
	{
		SPIN_LOCK(lockState)
		{
			if (state == State::Stopping)
			{
				if (requestType != HttpRequestType::Response || !drainResponseUploads) return false;
			}
			switch (requestType)
			{
			case HttpRequestType::Connect:
				CHECK_ERROR(state == State::WaitForServerConnection, L"/Connect can only be called when client is waiting for the server.");
				break;
			case HttpRequestType::Request:
				CHECK_ERROR(state == State::Running, L"/Request can only be called when client is running.");
				break;
			case HttpRequestType::Response:
				CHECK_ERROR(state == State::Running || (state == State::Stopping && drainResponseUploads), L"/Response can only be called when client is running.");
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

	Func<void()> requestSentCallback;
	if (responseUploadState)
	{
		requestSentCallback = [this, responseUploadState]()
		{
			if (responseUploadState->advanced.exchange(1) == 0)
			{
				OnHttpResponseRequestSent();
			}
		};
	}

	api->HttpQuery(request, [this, requestType, body, attempt, responseUploadState](Variant<HttpResponse, HttpError> result)
	{
		if (responseUploadState)
		{
			OnHttpResponseRequestCompleted(body, attempt, responseUploadState, std::move(result));
		}
		else
		{
			OnHttpRequestCompleted(requestType, body, attempt, responseUploadState, std::move(result));
		}
	}, requestSentCallback);
	return true;
}

bool HttpClient::OnHttpRequestFailed(HttpRequestType requestType, const WString& body, vint attempt, Ptr<ResponseUploadState> responseUploadState, const WString& errorMessage)
{
	if (IsStopping())
	{
		if (responseUploadState && responseUploadState->advanced.exchange(1) == 0)
		{
			OnHttpResponseRequestSent();
		}
		return true;
	}

	bool retrying = false;
	switch (requestType)
	{
	case HttpRequestType::Connect:
		{
			bool fatal = attempt >= HttpRequestMaxAttempts;
			RaiseLocalError(errorMessage, fatal);
			if (!fatal && !IsStopping())
			{
				retrying = SendHttpRequest(HttpRequestType::Connect, urlConnect, WString::Empty, attempt + 1);
			}
		}
		break;
	case HttpRequestType::Request:
		RaiseLocalError(errorMessage, false);
		if (!IsStopping())
		{
			retrying = SendHttpRequest(HttpRequestType::Request, urlRequest, WString::Empty, attempt + 1);
		}
		break;
	case HttpRequestType::Response:
		{
			bool fatal = attempt >= HttpRequestMaxAttempts;
			RaiseLocalError(errorMessage, fatal);
			if (!fatal && !IsStopping())
			{
				retrying = SendHttpRequest(HttpRequestType::Response, urlResponse, body, attempt + 1, responseUploadState);
			}
		}
		break;
	}
	return !retrying;
}

bool HttpClient::OnHttpRequestCompleted(HttpRequestType requestType, WString body, vint attempt, Ptr<ResponseUploadState> responseUploadState, Variant<HttpResponse, HttpError> result)
{
	if (auto error = result.TryGet<HttpError>())
	{
		switch (requestType)
		{
		case HttpRequestType::Connect:
			return OnHttpRequestFailed(requestType, body, attempt, responseUploadState, L"/Connect failed: " + error->message);
		case HttpRequestType::Request:
			return OnHttpRequestFailed(requestType, body, attempt, responseUploadState, L"/Request failed: " + error->message);
		case HttpRequestType::Response:
			return OnHttpRequestFailed(requestType, body, attempt, responseUploadState, L"/Response failed: " + error->message);
		}
	}

	auto&& response = result.Get<HttpResponse>();
	if (response.statusCode != 200)
	{
		switch (requestType)
		{
		case HttpRequestType::Connect:
			return OnHttpRequestFailed(requestType, body, attempt, responseUploadState, WString::Unmanaged(L"/Connect returned status code: ") + itow(response.statusCode) + L".");
		case HttpRequestType::Request:
			return OnHttpRequestFailed(requestType, body, attempt, responseUploadState, WString::Unmanaged(L"/Request returned status code: ") + itow(response.statusCode) + L", another renderer may have connected to the core.");
		case HttpRequestType::Response:
			return OnHttpRequestFailed(requestType, body, attempt, responseUploadState, WString::Unmanaged(L"/Response returned status code: ") + itow(response.statusCode) + L", another renderer may have connected to the core.");
		}
	}

	if (response.contentType != HttpNetworkProtocolContentType)
	{
		switch (requestType)
		{
		case HttpRequestType::Connect:
			return OnHttpRequestFailed(requestType, body, attempt, responseUploadState, L"/Connect response did not return content type: application/json; charset=utf8.");
		case HttpRequestType::Request:
			return OnHttpRequestFailed(requestType, body, attempt, responseUploadState, L"/Request response did not return content type: application/json; charset=utf8.");
		case HttpRequestType::Response:
			return OnHttpRequestFailed(requestType, body, attempt, responseUploadState, L"/Response response did not return content type: application/json; charset=utf8.");
		}
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
	return true;
}

void HttpClient::OnHttpResponseRequestCompleted(WString body, vint attempt, Ptr<ResponseUploadState> responseUploadState, Variant<HttpResponse, HttpError> result)
{
	auto completion = Ptr(new ResponseCompletion(std::move(body), attempt, responseUploadState, std::move(result)));
	bool drain = false;
	SPIN_LOCK(lockResponseCompletions)
	{
		CHECK_ERROR(!pendingResponseCompletions.Keys().Contains(responseUploadState->sequence), L"HttpClient received duplicate /Response completion sequence.");
		pendingResponseCompletions.Add(responseUploadState->sequence, completion);
		if (!responseCompletionDraining)
		{
			responseCompletionDraining = true;
			drain = true;
		}
	}
	if (drain)
	{
		DrainHttpResponseCompletions();
	}
}

void HttpClient::DrainHttpResponseCompletions()
{
	while (true)
	{
		Ptr<ResponseCompletion> completion;
		SPIN_LOCK(lockResponseCompletions)
		{
			auto index = pendingResponseCompletions.Keys().IndexOf(nextResponseCompletion);
			if (index == -1)
			{
				responseCompletionDraining = false;
				return;
			}
			completion = pendingResponseCompletions.Values()[index];
			pendingResponseCompletions.Remove(nextResponseCompletion);
		}

		if (OnHttpRequestCompleted(
			HttpRequestType::Response,
			std::move(completion->body),
			completion->attempt,
			completion->responseUploadState,
			std::move(completion->result)))
		{
			nextResponseCompletion++;
		}
	}
}

void HttpClient::SendString(const WString& str)
{
	WString body;
	Ptr<ResponseUploadState> responseUploadState;
	bool send = false;
	SPIN_LOCK(lockState)
	{
		if (state == State::Stopping) return;
		CHECK_ERROR(state == State::Running, L"HttpClient::SendString can only be called when the client is running.");
		queuedResponseBodies.Add(str);
		if (!responseUploadPending)
		{
			eventResponseUploadsCompleted.Unsignal();
			responseUploadPending = true;
			body = queuedResponseBodies[0];
			queuedResponseBodies.RemoveAt(0);
			responseUploadState = Ptr(new ResponseUploadState);
			responseUploadState->sequence = nextResponseSequence++;
			send = true;
		}
	}
	if (send)
	{
		SendHttpRequest(HttpRequestType::Response, urlResponse, body, 1, responseUploadState);
	}
}

void HttpClient::OnHttpResponseRequestSent()
{
	WString body;
	Ptr<ResponseUploadState> responseUploadState;
	bool send = false;
	SPIN_LOCK(lockState)
	{
		responseUploadPending = false;
		if (state == State::Stopping && !drainResponseUploads)
		{
			queuedResponseBodies.Clear();
			eventResponseUploadsCompleted.Signal();
			return;
		}
		if (queuedResponseBodies.Count() > 0)
		{
			responseUploadPending = true;
			body = queuedResponseBodies[0];
			queuedResponseBodies.RemoveAt(0);
			responseUploadState = Ptr(new ResponseUploadState);
			responseUploadState->sequence = nextResponseSequence++;
			send = true;
		}
		else
		{
			eventResponseUploadsCompleted.Signal();
		}
	}
	if (send)
	{
		SendHttpRequest(HttpRequestType::Response, urlResponse, body, 1, responseUploadState);
	}
}

/***********************************************************************
HttpClient
***********************************************************************/

HttpClient::HttpClient(const WString _baseUrl, vint port)
	: baseUrl(_baseUrl)
{
	CHECK_ERROR(eventWaitForServer.CreateAutoUnsignal(false), L"HttpClient initialization failed on eventWaitForServer.CreateAutoUnsignal.");
	CHECK_ERROR(eventResponseUploadsCompleted.CreateManualUnsignal(true), L"HttpClient initialization failed on eventResponseUploadsCompleted.CreateManualUnsignal.");

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
	bool waitForResponseUploads = false;
	bool first = false;
	{
		SPIN_LOCK(lockState)
		{
			if (httpClientApi && !stopDrainingStarted)
			{
				state = State::Stopping;
				drainResponseUploads = true;
				stopDrainingStarted = true;
				waitForResponseUploads = responseUploadPending;
				first = true;
			}
			else
			{
				state = State::Stopping;
			}
		}
	}

	eventWaitForServer.Signal();
	if (!first) return;
	if (waitForResponseUploads)
	{
		eventResponseUploadsCompleted.Wait();
	}
	{
		SPIN_LOCK(lockState)
		{
			drainResponseUploads = false;
			queuedResponseBodies.Clear();
			responseUploadPending = false;
			stoppingApi = httpClientApi;
			httpClientApi = nullptr;
			notifyDisconnected = true;
		}
	}
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
