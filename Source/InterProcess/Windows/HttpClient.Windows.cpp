#include "HttpClient.Windows.h"

namespace vl::inter_process
{

using namespace vl::collections;

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

void HttpClient::WaitForServer()
{
	if (state == State::Stopping) return;
	CHECK_ERROR(state == State::Ready, L"WaitForServer can only be called once.");
	state = State::WaitForServerConnection;
	eventWaitForServer.Unsignal();
	auto context = SendHttpRequest(HttpRequestType::Connect, L"GET", urlConnect, WString::Empty);
	if (!context) return;

	eventWaitForServer.Wait();
	if (state == State::Stopping) return;

	CHECK_ERROR(context->connectCompleted, L"/Connect did not complete.");
	CHECK_ERROR(context->connectError == WString::Empty, L"/Connect failed.");

	auto body = context->connectResponse;
	vint separatorIndex = body.IndexOf(L';');
	CHECK_ERROR(separatorIndex != -1, L"/Connect response body is not in the correct format: requestUrl;responseUrl.");
	urlRequest = baseUrl + body.Left(separatorIndex);
	urlResponse = baseUrl + body.Right(body.Length() - separatorIndex - 1);
	state = State::Running;

	if (callback)
	{
		callback->OnConnected();
	}
}

ClientStatus HttpClient::GetStatus()
{
	switch (state)
	{
	case State::Ready:
		return ClientStatus::Ready;
	case State::WaitForServerConnection:
		return ClientStatus::WaitingForServer;
	case State::Running:
		return ClientStatus::Connected;
	default:
		return ClientStatus::Disconnected;
	}
}

/***********************************************************************
HttpClient (Writing)
***********************************************************************/

void HttpClient::OnHttpRequestBodyReceived(Ptr<HttpRequestContext> context)
{
	auto reading = context->responseReading;
	WString body;
	if (reading->bodyBufferWriting > 0)
	{
		reading->bodyBuffer[reading->bodyBufferWriting] = 0;
		U8String bodyUtf8 = U8String::Unmanaged(&reading->bodyBuffer[0]);
		body = u8tow(bodyUtf8);
	}

	switch (context->requestType)
	{
	case HttpRequestType::Connect:
		context->connectResponse = body;
		context->connectCompleted = true;
		CloseRequest(context->httpRequest, context->requestId);
		eventWaitForServer.Signal();
		break;
	case HttpRequestType::Request:
		CloseRequest(context->httpRequest, context->requestId);
		BeginReadingLoopUnsafe();
		if (body.Length() > 0 && callback)
		{
			callback->OnReadString(body);
		}
		break;
	case HttpRequestType::Response:
		CloseRequest(context->httpRequest, context->requestId);
		if (state != State::Stopping && body.Length() > 0 && callback)
		{
			callback->OnReadString(body);
		}
		break;
	}
}

Ptr<HttpClient::HttpRequestContext> HttpClient::SendHttpRequest(HttpRequestType requestType, const wchar_t* method, const WString& url, const WString& body)
{
	SPIN_LOCK(httpActiveRequestsLock)
	{
		if (state == State::Stopping) return {};
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
		BeginPendingCallback();
	}

	DWORD lastError = 0;
	BOOL httpResult = FALSE;

	LPCWSTR acceptTypes[] = { L"application/json; charset=utf8", NULL };
	HINTERNET httpRequest = WinHttpOpenRequest(
		httpConnection,
		method,
		url.Buffer(),
		NULL,
		WINHTTP_NO_REFERER,
		acceptTypes,
		WINHTTP_FLAG_REFRESH);
	lastError = GetLastError();
	if (httpRequest == NULL)
	{
		EndPendingCallback();
		if (lastError == ERROR_INVALID_HANDLE)
		{
			CHECK_ERROR(state == State::Stopping, L"WinHttpOpenRequest failed with ERROR_INVALID_HANDLE but client is not stopping.");
			return {};
		}
		CHECK_FAIL(L"WinHttpOpenRequest failed.");
	}

	auto contextPtr = new Ptr<HttpRequestContext>(new HttpRequestContext);
	auto context = *contextPtr;
	context->client = this;
	context->requestType = requestType;
	context->httpRequest = httpRequest;
	context->requestId = requestType == HttpRequestType::Response ? ++createdRequestIds : 0;
	context->requestBody = wtou8(body);
	context->responseReading = Ptr(new HttpResponseReading);

	auto failBeforeSend = [&]()
	{
		WinHttpCloseHandle(httpRequest);
		delete contextPtr;
		EndPendingCallback();
	};

	DWORD_PTR contextValue = reinterpret_cast<DWORD_PTR>(contextPtr);
	httpResult = WinHttpSetOption(
		httpRequest,
		WINHTTP_OPTION_CONTEXT_VALUE,
		&contextValue,
		sizeof(contextValue));
	lastError = GetLastError();
	if (httpResult == FALSE)
	{
		failBeforeSend();
		if (lastError == ERROR_INVALID_HANDLE)
		{
			CHECK_ERROR(state == State::Stopping, L"WinHttpSetOption(WINHTTP_OPTION_CONTEXT_VALUE) failed with ERROR_INVALID_HANDLE but client is not stopping.");
			return {};
		}
		CHECK_FAIL(L"WinHttpSetOption(WINHTTP_OPTION_CONTEXT_VALUE) failed.");
	}

	if (requestType == HttpRequestType::Response)
	{
		httpResult = WinHttpAddRequestHeaders(
			httpRequest,
			L"Content-Type: application/json; charset=utf8",
			-1,
			WINHTTP_ADDREQ_FLAG_ADD);
		lastError = GetLastError();
		if (httpResult == FALSE)
		{
			failBeforeSend();
			if (lastError == ERROR_INVALID_HANDLE)
			{
				CHECK_ERROR(state == State::Stopping, L"WinHttpAddRequestHeaders failed with ERROR_INVALID_HANDLE but client is not stopping.");
				return {};
			}
			CHECK_FAIL(L"WinHttpAddRequestHeaders failed.");
		}
	}

	auto httpCallback = (WINHTTP_STATUS_CALLBACK)[](HINTERNET httpRequest, DWORD_PTR dwContext, DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD dwStatusInformationLength) -> void
	{
		if (!dwContext) return;
		auto contextPtr = reinterpret_cast<Ptr<HttpClient::HttpRequestContext>*>(dwContext);
		auto context = *contextPtr;
		auto self = context->client;
		auto requestId = context->requestId;

		auto completeConnectWithError = [=](const WString& error)
		{
			context->connectError = error;
			context->connectCompleted = true;
			self->CloseRequest(httpRequest, requestId);
			self->eventWaitForServer.Signal();
		};

		switch (dwInternetStatus)
		{
		case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
			{
				if (self->state == State::Stopping && context->requestType != HttpRequestType::Response) return;
				DWORD lastError = 0;
				BOOL httpResult = WinHttpReceiveResponse(httpRequest, NULL);
				lastError = GetLastError();
				if (lastError == ERROR_INVALID_HANDLE)
				{
					CHECK_ERROR(self->state == State::Stopping, L"WinHttpReceiveResponse failed with ERROR_INVALID_HANDLE but client is not stopping.");
					return;
				}
				CHECK_ERROR(httpResult == TRUE, L"WinHttpReceiveResponse failed.");
			}
			break;
		case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
			{
				if (self->state == State::Stopping && context->requestType != HttpRequestType::Response) return;
				DWORD lastError = 0;
				DWORD statusCode = 0;
				DWORD dwordLength = sizeof(DWORD);
				BOOL httpResult = WinHttpQueryHeaders(
					httpRequest,
					WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					WINHTTP_HEADER_NAME_BY_INDEX,
					&statusCode,
					&dwordLength,
					WINHTTP_NO_HEADER_INDEX);
				lastError = GetLastError();
				if (lastError == ERROR_INVALID_HANDLE)
				{
					CHECK_ERROR(self->state == State::Stopping, L"WinHttpQueryHeaders failed with ERROR_INVALID_HANDLE but client is not stopping.");
					return;
				}
				CHECK_ERROR(httpResult == TRUE, L"WinHttpQueryHeaders failed to retrieve status code.");

				if (statusCode != 200)
				{
					switch (context->requestType)
					{
					case HttpRequestType::Connect:
						completeConnectWithError(WString::Unmanaged(L"/Connect returned status code: ") + itow(statusCode) + L".");
						break;
					case HttpRequestType::Request:
						self->CloseRequest(httpRequest, requestId);
						self->RaiseErrorUnsafe(WString::Unmanaged(L"/Request returned status code: ") + itow(statusCode) + L", another renderer may have connected to the core.");
						break;
					case HttpRequestType::Response:
						self->CloseRequest(httpRequest, requestId);
						self->RaiseErrorUnsafe(WString::Unmanaged(L"/Response returned status code: ") + itow(statusCode) + L", another renderer may have connected to the core.");
						break;
					}
					return;
				}

				DWORD headerLength = 0;
				httpResult = WinHttpQueryHeaders(
					httpRequest,
					WINHTTP_QUERY_CONTENT_TYPE,
					WINHTTP_HEADER_NAME_BY_INDEX,
					NULL,
					&headerLength,
					WINHTTP_NO_HEADER_INDEX);
				lastError = GetLastError();
				if (lastError == ERROR_INVALID_HANDLE)
				{
					CHECK_ERROR(self->state == State::Stopping, L"WinHttpQueryHeaders failed with ERROR_INVALID_HANDLE but client is not stopping.");
					return;
				}
				if (!(httpResult == FALSE && lastError == ERROR_INSUFFICIENT_BUFFER))
				{
					if (context->requestType == HttpRequestType::Connect)
					{
						completeConnectWithError(L"WinHttpQueryHeaders failed to retrieve content type.");
						return;
					}
					CHECK_FAIL(L"WinHttpQueryHeaders failed to retrieve content type.");
				}

				Array<wchar_t> headerBuffer(headerLength / sizeof(wchar_t) + 1);
				ZeroMemory(&headerBuffer[0], headerBuffer.Count() * sizeof(wchar_t));

				httpResult = WinHttpQueryHeaders(
					httpRequest,
					WINHTTP_QUERY_CONTENT_TYPE,
					WINHTTP_HEADER_NAME_BY_INDEX,
					&headerBuffer[0],
					&headerLength,
					WINHTTP_NO_HEADER_INDEX);
				lastError = GetLastError();
				if (lastError == ERROR_INVALID_HANDLE)
				{
					CHECK_ERROR(self->state == State::Stopping, L"WinHttpQueryHeaders failed with ERROR_INVALID_HANDLE but client is not stopping.");
					return;
				}
				CHECK_ERROR(httpResult == TRUE, L"WinHttpQueryHeaders failed to retrieve content-type.");

				const wchar_t* header = &headerBuffer[0];
				if (wcscmp(header, L"application/json; charset=utf8") != 0)
				{
					if (context->requestType == HttpRequestType::Connect)
					{
						completeConnectWithError(L"HTTP response did not return content type: application/json; charset=utf8.");
						return;
					}
					CHECK_FAIL(L"HTTP response did not return content type: application/json; charset=utf8.");
				}

				auto reading = context->responseReading;
				reading->bodyBufferWriting = 0;
				httpResult = WinHttpQueryDataAvailable(
					httpRequest,
					NULL);
				lastError = GetLastError();
				if (lastError == ERROR_INVALID_HANDLE)
				{
					CHECK_ERROR(self->state == State::Stopping, L"WinHttpQueryDataAvailable failed with ERROR_INVALID_HANDLE but client is not stopping.");
					return;
				}
				CHECK_ERROR(httpResult == TRUE, L"WinHttpQueryDataAvailable failed.");
			}
			break;
		case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
			{
				DWORD dataAvailable = *(PDWORD)lpvStatusInformation;
				if (self->state == State::Stopping && context->requestType != HttpRequestType::Response) return;

				auto reading = context->responseReading;

				if (dataAvailable == 0)
				{
					self->OnHttpRequestBodyReceived(context);
					return;
				}

				reading->bodyBufferWritingAvailable = dataAvailable;
				DWORD bufferSize = reading->bodyBufferWriting + dataAvailable + 1;
				if (reading->bodyBuffer.Count() < (vint)bufferSize)
				{
					reading->bodyBuffer.Resize((bufferSize + HttpRespondBodyStep - 1) / HttpRespondBodyStep * HttpRespondBodyStep);
				}

				DWORD lastError = 0;
				BOOL httpResult = WinHttpReadData(
					httpRequest,
					&reading->bodyBuffer[reading->bodyBufferWriting],
					dataAvailable,
					NULL);
				lastError = GetLastError();
				if (lastError == ERROR_INVALID_HANDLE)
				{
					CHECK_ERROR(self->state == State::Stopping, L"WinHttpReadData failed with ERROR_INVALID_HANDLE but client is not stopping.");
					return;
				}
				CHECK_ERROR(httpResult == TRUE, L"WinHttpReadData failed.");
			}
			break;
		case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
			{
				if (self->state == State::Stopping && context->requestType != HttpRequestType::Response) return;

				auto reading = context->responseReading;

				CHECK_ERROR(
					reading->bodyBufferWritingAvailable == dwStatusInformationLength,
					L"WinHttpReadData failed to read all available data."
					);
				reading->bodyBufferWriting += reading->bodyBufferWritingAvailable;

				DWORD lastError = 0;
				BOOL httpResult = WinHttpQueryDataAvailable(
					httpRequest,
					NULL);
				lastError = GetLastError();
				if (lastError == ERROR_INVALID_HANDLE)
				{
					CHECK_ERROR(self->state == State::Stopping, L"WinHttpQueryDataAvailable failed with ERROR_INVALID_HANDLE but client is not stopping.");
					return;
				}
				CHECK_ERROR(httpResult == TRUE, L"WinHttpQueryDataAvailable failed.");
			}
			break;
		case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
			{
				if (self->state == State::Stopping && context->requestType != HttpRequestType::Response) return;
				switch (context->requestType)
				{
				case HttpRequestType::Connect:
					completeConnectWithError(L"/Connect canceled.");
					break;
				case HttpRequestType::Request:
					self->CloseRequest(httpRequest, requestId);
					self->RaiseErrorUnsafe(WString::Unmanaged(L"/Request canceled, another renderer may have connected to the core."));
					break;
				case HttpRequestType::Response:
					self->CloseRequest(httpRequest, requestId);
					if (self->state != State::Stopping)
					{
						self->RaiseErrorUnsafe(WString::Unmanaged(L"/Response canceled, another renderer may have connected to the core."));
					}
					break;
				}
			}
			break;
		case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
			if (context->requestType == HttpRequestType::Connect && !context->connectCompleted)
			{
				context->connectError = L"/Connect canceled.";
				context->connectCompleted = true;
				self->eventWaitForServer.Signal();
			}
			self->OnRequestHandleClosing(httpRequest, requestId);
			delete contextPtr;
			break;
		}
	};

	DWORD requestBodyLength = (DWORD)context->requestBody.Length();
	LPVOID requestBodyBuffer = requestBodyLength == 0 ? WINHTTP_NO_REQUEST_DATA : (LPVOID)context->requestBody.Buffer();
	DWORD_PTR callbackContext = reinterpret_cast<DWORD_PTR>(contextPtr);
	SPIN_LOCK(httpActiveRequestsLock)
	{
		if (state == State::Stopping)
		{
			failBeforeSend();
			return {};
		}
		else
		{
			WINHTTP_STATUS_CALLBACK previousCallback = WinHttpSetStatusCallback(
				httpRequest,
				httpCallback,
				WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
				NULL);
			lastError = GetLastError();
			if (previousCallback == WINHTTP_INVALID_STATUS_CALLBACK)
			{
				failBeforeSend();
				if (lastError == ERROR_INVALID_HANDLE)
				{
					CHECK_ERROR(state == State::Stopping, L"WinHttpSetStatusCallback failed with ERROR_INVALID_HANDLE but client is not stopping.");
					return {};
				}
				CHECK_FAIL(L"WinHttpSetStatusCallback failed.");
			}

			AttachRequestUnsafe(httpRequest, context->requestId, context->requestType);
		}
	}

	httpResult = WinHttpSendRequest(
		httpRequest,
		WINHTTP_NO_ADDITIONAL_HEADERS,
		0,
		requestBodyBuffer,
		requestBodyLength,
		requestBodyLength,
		callbackContext);
	lastError = GetLastError();

	if (httpResult == FALSE)
	{
		CloseRequest(httpRequest, context->requestId);
		if (lastError == ERROR_INVALID_HANDLE)
		{
			CHECK_ERROR(state == State::Stopping, L"WinHttpSendRequest failed with ERROR_INVALID_HANDLE but client is not stopping.");
			return {};
		}
		CHECK_FAIL(L"WinHttpSendRequest failed.");
	}

	return context;
}

void HttpClient::SendString(const WString& str)
{
	SendHttpRequest(HttpRequestType::Response, L"POST", urlResponse, str);
}

/***********************************************************************
HttpClient
***********************************************************************/

void HttpClient::BeginPendingCallback()
{
	if (pendingCallbacks++ == 0)
	{
		eventPendingCallbacks.Unsignal();
	}
}

void HttpClient::EndPendingCallback()
{
	if (--pendingCallbacks == 0)
	{
		eventPendingCallbacks.Signal();
	}
}

vint HttpClient::FindActiveRequestUnsafe(HINTERNET httpRequest, vint requestId)
{
	for (vint index = 0; index < httpActiveRequests.Count(); index++)
	{
		auto&& activeRequest = httpActiveRequests[index];
		if (activeRequest.requestId != -1 && activeRequest.httpRequest == httpRequest && activeRequest.requestId == requestId)
		{
			return index;
		}
	}
	return -1;
}

void HttpClient::AttachRequestUnsafe(HINTERNET httpRequest, vint requestId, HttpRequestType requestType)
{
	for (vint index = 0; index < httpActiveRequests.Count(); index++)
	{
		auto&& activeRequest = httpActiveRequests[index];
		if (activeRequest.requestId == -1)
		{
			activeRequest.httpRequest = httpRequest;
			activeRequest.requestId = requestId;
			activeRequest.requestType = requestType;
			return;
		}
	}

	HttpActiveRequest activeRequest;
	activeRequest.httpRequest = httpRequest;
	activeRequest.requestId = requestId;
	activeRequest.requestType = requestType;
	httpActiveRequests.Add(activeRequest);
}

void HttpClient::CloseRequest(HINTERNET httpRequest, vint requestId)
{
	bool closeRequest = false;
	SPIN_LOCK(httpActiveRequestsLock)
	{
		vint index = FindActiveRequestUnsafe(httpRequest, requestId);
		if (index != -1)
		{
			auto&& activeRequest = httpActiveRequests[index];
			activeRequest.httpRequest = NULL;
			activeRequest.requestId = -1;
			closeRequest = true;
		}
	}
	if (closeRequest)
	{
		WinHttpCloseHandle(httpRequest);
	}
}

void HttpClient::OnRequestHandleClosing(HINTERNET httpRequest, vint requestId)
{
	SPIN_LOCK(httpActiveRequestsLock)
	{
		vint index = FindActiveRequestUnsafe(httpRequest, requestId);
		if (index != -1)
		{
			auto&& activeRequest = httpActiveRequests[index];
			activeRequest.httpRequest = NULL;
			activeRequest.requestId = -1;
		}
	}
	EndPendingCallback();
}

HttpClient::HttpClient(const WString _baseUrl, vint port)
	: baseUrl(_baseUrl)
{
	DWORD lastError = 0;
	CHECK_ERROR(eventWaitForServer.CreateAutoUnsignal(false), L"HttpClient initialization failed on eventWaitForServer.CreateAutoUnsignal.");
	CHECK_ERROR(eventPendingCallbacks.CreateManualUnsignal(true), L"HttpClient initialization failed on eventPendingCallbacks.CreateManualUnsignal.");

	httpSession = WinHttpOpen(
		L"vl::inter_process::HttpClient",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		WINHTTP_FLAG_ASYNC);
	lastError = GetLastError();
	CHECK_ERROR(httpSession != NULL, L"WinHttpOpen failed.");

	httpConnection = WinHttpConnect(
		httpSession,
		L"localhost",
		(INTERNET_PORT)port,
		0);
	lastError = GetLastError();
	CHECK_ERROR(httpConnection != NULL, L"WinHttpConnect failed.");

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
	if (httpSession != NULL)
	{
		List<HINTERNET> stoppingRequests;
		SPIN_LOCK(httpActiveRequestsLock)
		{
			state = State::Stopping;
			for (vint i = 0; i < httpActiveRequests.Count(); i++)
			{
				auto&& activeRequest = httpActiveRequests[i];
				if (activeRequest.requestId != -1 && activeRequest.requestType != HttpRequestType::Response)
				{
					stoppingRequests.Add(activeRequest.httpRequest);
					activeRequest.httpRequest = NULL;
					activeRequest.requestId = -1;
				}
			}
		}
		for (auto httpRequest : stoppingRequests)
		{
			WinHttpCloseHandle(httpRequest);
		}

		eventPendingCallbacks.Wait();

		WinHttpCloseHandle(httpConnection);
		WinHttpSetStatusCallback(
			httpSession,
			NULL,
			WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
			NULL);
		WinHttpCloseHandle(httpSession);

		SPIN_LOCK(httpActiveRequestsLock)
		{
			httpActiveRequests.Clear();
		}

		httpConnection = NULL;
		httpSession = NULL;

		if (callback)
		{
			callback->OnDisconnected();
		}
	}
}

}
