#include "HttpClient.Windows.h"

namespace vl::inter_process::windows_http
{
	namespace
	{
		struct HttpClientCallbackFrame
		{
			HttpClient*						client;
			HttpClientCallbackFrame*		previous;
		};

		thread_local HttpClientCallbackFrame*	currentHttpClientCallback = nullptr;
		thread_local void*					currentHttpClientDisconnectState = nullptr;

		bool IsRemoteUnavailable(const HttpError& error)
		{
			switch (error.errorCode)
			{
			case ERROR_WINHTTP_TIMEOUT:
			case ERROR_WINHTTP_NAME_NOT_RESOLVED:
			case ERROR_WINHTTP_CANNOT_CONNECT:
			case ERROR_WINHTTP_CONNECTION_ERROR:
			case ERROR_WINHTTP_OPERATION_CANCELLED:
			case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
				return true;
			default:
				return false;
			}
		}
	}

HttpClient::StopState::StopState()
{
	CHECK_ERROR(eventWaitForServer.CreateAutoUnsignal(false), L"HttpClient initialization failed on eventWaitForServer.CreateAutoUnsignal.");
	CHECK_ERROR(eventSchedulingFinished.CreateManualUnsignal(false), L"HttpClient initialization failed on eventSchedulingFinished.CreateManualUnsignal.");
	CHECK_ERROR(eventCallbacksFinished.CreateManualUnsignal(true), L"HttpClient initialization failed on eventCallbacksFinished.CreateManualUnsignal.");
	CHECK_ERROR(eventCallbackChanged.CreateAutoUnsignal(false), L"HttpClient initialization failed on eventCallbackChanged.CreateAutoUnsignal.");
	CHECK_ERROR(eventFinished.CreateManualUnsignal(false), L"HttpClient initialization failed on eventFinished.CreateManualUnsignal.");
}

/***********************************************************************
HttpClient (Reading)
***********************************************************************/

void HttpClient::RaiseLocalError(WString errorMessage, bool fatal)
{
	if (fatal)
	{
		StopFromCallback();
	}
	if (callback)
	{
		callback->OnLocalError(errorMessage, fatal);
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

bool HttpClient::BeginHttpCallback(Ptr<StopState> state)
{
	SPIN_LOCK(state->lock)
	{
		if (state->callbacksClosed) return false;
		if (state->activeCallbacks++ == 0) state->eventCallbacksFinished.Unsignal();
	}
	return true;
}

void HttpClient::EndHttpCallback(Ptr<StopState> state)
{
	SPIN_LOCK(state->lock)
	{
		if (--state->activeCallbacks == 0) state->eventCallbacksFinished.Signal();
	}
	state->eventCallbackChanged.Signal();
}

vint HttpClient::CurrentHttpCallbackDepth()
{
	vint depth = 0;
	for (auto frame = currentHttpClientCallback; frame; frame = frame->previous)
	{
		if (frame->client == this) depth++;
	}
	return depth;
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
	stopState->eventWaitForServer.Signal();
}

void HttpClient::WaitForServer()
{
	auto waitingState = stopState;
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

	waitingState->eventWaitForServer.Unsignal();
	if (!SendHttpRequest(HttpRequestType::Connect, urlConnect, WString::Empty))
	{
		return;
	}

	waitingState->eventWaitForServer.Wait();
	{
		SPIN_LOCK(waitingState->lock)
		{
			if (waitingState->started) return;
		}
	}

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

	auto callbackState = stopState;
	api->HttpQuery(request, [this, callbackState, requestType, body, attempt](Variant<HttpResponse, HttpError> result)
	{
		if (!BeginHttpCallback(callbackState)) return;
		HttpClientCallbackFrame frame{ this, currentHttpClientCallback };
		currentHttpClientCallback = &frame;
		try
		{
			OnHttpRequestCompleted(requestType, body, attempt, std::move(result));
		}
		catch (...)
		{
			currentHttpClientCallback = frame.previous;
			EndHttpCallback(callbackState);
			throw;
		}
		currentHttpClientCallback = frame.previous;
		EndHttpCallback(callbackState);
	});
	return true;
}

void HttpClient::OnHttpRequestFailed(HttpRequestType requestType, const WString& body, vint attempt, const WString& errorMessage, bool remoteUnavailable)
{
	if (IsStopping()) return;

	switch (requestType)
	{
	case HttpRequestType::Connect:
		{
			bool fatal = attempt >= HttpRequestMaxAttempts;
			if (fatal)
			{
				RaiseLocalError(errorMessage, true);
			}
			else if (!IsStopping())
			{
				SendHttpRequest(HttpRequestType::Connect, urlConnect, WString::Empty, attempt + 1);
			}
		}
		break;
	case HttpRequestType::Request:
		if (attempt >= HttpRequestMaxAttempts)
		{
			if (remoteUnavailable)
			{
				StopFromCallback();
			}
			else
			{
				RaiseLocalError(errorMessage, true);
			}
		}
		else
		{
			SendHttpRequest(HttpRequestType::Request, urlRequest, WString::Empty, attempt + 1);
		}
		break;
	case HttpRequestType::Response:
		{
			bool fatal = attempt >= HttpRequestMaxAttempts;
			if (fatal && remoteUnavailable)
			{
				StopFromCallback();
			}
			else
			{
				if (fatal)
				{
					RaiseLocalError(errorMessage, true);
				}
				else if (!IsStopping())
				{
					SendHttpRequest(HttpRequestType::Response, urlResponse, body, attempt + 1);
				}
			}
		}
		break;
	}
}

void HttpClient::OnHttpRequestCompleted(HttpRequestType requestType, WString body, vint attempt, Variant<HttpResponse, HttpError> result)
{
	if (auto error = result.TryGet<HttpError>())
	{
		auto remoteUnavailable = IsRemoteUnavailable(*error);
		switch (requestType)
		{
		case HttpRequestType::Connect:
			OnHttpRequestFailed(requestType, body, attempt, L"/Connect failed: " + error->message);
			break;
		case HttpRequestType::Request:
			OnHttpRequestFailed(requestType, body, attempt, L"/Request failed: " + error->message, remoteUnavailable);
			break;
		case HttpRequestType::Response:
			OnHttpRequestFailed(requestType, body, attempt, L"/Response failed: " + error->message, remoteUnavailable);
			break;
		}
		return;
	}

	auto&& response = result.Get<HttpResponse>();
	if (response.statusCode == 404 && requestType != HttpRequestType::Connect)
	{
		StopFromCallback();
		return;
	}
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
			auto completionState = stopState;
			auto installedCallback = callback;
			BeginReadingLoopUnsafe();
			bool stopping = false;
			SPIN_LOCK(completionState->lock)
			{
				stopping = completionState->started;
			}
			if (!stopping && responseBody.Length() > 0 && installedCallback)
			{
				installedCallback->OnReadString(responseBody);
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
	stopState = Ptr(new StopState);
	httpClientApi = Ptr(new HttpClientApi(L"localhost", port));
	urlConnect = baseUrl + HttpServerUrl_Connect;
}

HttpClient::~HttpClient()
{
	auto stoppingState = stopState;
	auto callbackDepth = CurrentHttpCallbackDepth();
	auto callbackReentrant = callbackDepth > 0;
	if (callbackReentrant)
	{
		SPIN_LOCK(stoppingState->lock)
		{
			stoppingState->suppressDisconnected = true;
		}
	}
	StopCore(callbackReentrant);
	while (true)
	{
		bool callbacksDrained = false;
		SPIN_LOCK(stoppingState->lock)
		{
			callbacksDrained = stoppingState->activeCallbacks <= callbackDepth;
		}
		if (callbacksDrained) break;
		stoppingState->eventCallbackChanged.Wait();
	}
}

void HttpClient::InstallCallback(INetworkProtocolCallback* _callback)
{
	auto callbackState = stopState;
	bool waitForDisconnect = false;
	{
		SPIN_LOCK(lockState)
		{
			SPIN_LOCK(callbackState->lock)
			{
				CHECK_ERROR(!callback || !_callback, L"HttpClient::InstallCallback only accepts one callback at a time.");
				auto previousCallback = callback;
				callback = _callback;
				if (!_callback && callbackState->disconnectedCallback == previousCallback)
				{
					callbackState->disconnectedCallback = nullptr;
				}
				waitForDisconnect =
					!_callback &&
					callbackState->disconnectDelivering &&
					currentHttpClientDisconnectState != callbackState.Obj();
			}
		}
	}
	if (waitForDisconnect) callbackState->eventFinished.Wait();
	if (!_callback) return;
	_callback->OnInstalled(this);
}

void HttpClient::CompleteStop(Ptr<StopState> state)
{
	bool keepAliveRequests = true;
	Ptr<HttpClientApi> stoppingApi;
	{
		SPIN_LOCK(state->lock)
		{
			keepAliveRequests = !state->abortRequests;
			stoppingApi = state->stoppingApi;
		}
	}
	try
	{
		if (stoppingApi) stoppingApi->Stop(keepAliveRequests);
	}
	catch (...)
	{
	}

	{
		SPIN_LOCK(state->lock)
		{
			state->callbacksClosed = true;
		}
	}
	state->eventCallbacksFinished.Wait();

	INetworkProtocolCallback* disconnectedCallback = nullptr;
	{
		SPIN_LOCK(state->lock)
		{
			if (!state->suppressDisconnected && state->disconnectedCallback)
			{
				disconnectedCallback = state->disconnectedCallback;
				state->disconnectDelivering = true;
			}
		}
	}

	auto previousDisconnectState = currentHttpClientDisconnectState;
	currentHttpClientDisconnectState = state.Obj();
	try
	{
		if (disconnectedCallback) disconnectedCallback->OnDisconnected();
	}
	catch (...)
	{
	}
	currentHttpClientDisconnectState = previousDisconnectState;

	{
		SPIN_LOCK(state->lock)
		{
			state->disconnectDelivering = false;
			state->disconnectedCallback = nullptr;
			state->stoppingApi = nullptr;
			state->finished = true;
		}
	}
	state->eventFinished.Signal();
}

void HttpClient::StopCore(bool callbackReentrant)
{
	auto stoppingState = stopState;
	if (currentHttpClientDisconnectState == stoppingState.Obj()) return;

	bool first = false;
	bool schedule = false;
	bool execute = false;
	bool abort = false;
	Ptr<HttpClientApi> abortingApi;

	while (true)
	{
		bool waitForScheduling = false;
		bool waitForStop = false;
		{
			SPIN_LOCK(lockState)
			{
				SPIN_LOCK(stoppingState->lock)
				{
					if (stoppingState->finished) return;
					if (!stoppingState->started)
					{
						state = State::Stopping;
						stoppingState->stoppingApi = httpClientApi;
						httpClientApi = nullptr;
						stoppingState->disconnectedCallback = callback;
						stoppingState->callbacksClosed = true;
						stoppingState->started = true;
						stoppingState->executorClaimed = true;
						stoppingState->abortRequests = callbackReentrant;
						first = true;
						if (callbackReentrant)
						{
							stoppingState->scheduling = true;
							schedule = true;
						}
						else
						{
							execute = true;
						}
					}
					else if (callbackReentrant)
					{
						stoppingState->abortRequests = true;
						abortingApi = stoppingState->stoppingApi;
						abort = true;
					}
					else if (stoppingState->scheduling)
					{
						waitForScheduling = true;
					}
					else if (stoppingState->executorClaimed)
					{
						waitForStop = true;
					}
					else
					{
						stoppingState->executorClaimed = true;
						execute = true;
					}
				}
			}
		}

		if (first) stoppingState->eventWaitForServer.Signal();
		if (abort)
		{
			if (abortingApi) abortingApi->AbortRequests();
			return;
		}
		if (waitForScheduling)
		{
			stoppingState->eventSchedulingFinished.Wait();
			continue;
		}
		if (waitForStop)
		{
			stoppingState->eventFinished.Wait();
			return;
		}
		break;
	}

	if (schedule)
	{
		auto finalize = Func<void()>([stoppingState]()
		{
			CompleteStop(stoppingState);
		});
		bool queued = false;
		try
		{
			queued = ThreadPoolLite::Queue(finalize);
		}
		catch (...)
		{
		}
		if (!queued)
		{
			try
			{
				queued = Thread::CreateAndStart(finalize) != nullptr;
			}
			catch (...)
			{
			}
		}
		{
			SPIN_LOCK(stoppingState->lock)
			{
				stoppingState->scheduling = false;
				if (!queued) stoppingState->executorClaimed = false;
			}
		}
		stoppingState->eventSchedulingFinished.Signal();
		return;
	}

	if (execute) CompleteStop(stoppingState);
}

void HttpClient::StopFromCallback()
{
	StopCore(true);
}

void HttpClient::Stop()
{
	StopCore(CurrentHttpCallbackDepth() > 0);
}

}
