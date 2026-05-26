#include "HttpServer.Windows.h"

namespace vl::inter_process
{

using namespace vl::collections;

/***********************************************************************
HttpServerConnection
***********************************************************************/

void HttpServerConnection::OnCancelCurrentHttpRequestForPendingRequest()
{
	if (httpPendingRequestId != HTTP_NULL_ID)
	{
		if (!server)
		{
			httpPendingRequestId = HTTP_NULL_ID;
			return;
		}
		ULONG result = HttpCancelHttpRequest(
			server->httpRequestQueue,
			httpPendingRequestId,
			NULL);
		CHECK_ERROR(
			result == NO_ERROR || result == ERROR_CONNECTION_INVALID || result == ERROR_OPERATION_ABORTED,
			L"HttpCancelHttpRequest failed for canceling outdated /Request.");
		httpPendingRequestId = HTTP_NULL_ID;
	}
}

void HttpServerConnection::OnNewHttpRequestForPendingRequest(HTTP_REQUEST_ID httpRequestId)
{
	OnCancelCurrentHttpRequestForPendingRequest();
	httpPendingRequestId = httpRequestId;
	if (pendingRequestsToSend.Count() > 0)
	{
		auto pendingRequest = pendingRequestsToSend[0];
		pendingRequestsToSend.RemoveAt(0);
		ULONG result = HttpServer::SendResponse(server->httpRequestQueue, httpPendingRequestId, pendingRequest);
		CHECK_ERROR(result == NO_ERROR, L"HttpSendHttpResponse failed for responding /Request.");
		httpPendingRequestId = HTTP_NULL_ID;
	}
}

WString HttpServerConnection::SubmitResponse(PHTTP_REQUEST pRequest)
{
	ULONG bodyLength = 0;
	ULONG bodyReceived = 0;
	{
		auto& headerContentType = pRequest->Headers.KnownHeaders[HttpHeaderContentType];
		CHECK_ERROR(headerContentType.pRawValue != NULL, L"/Response missing Content-Type header.");
		CHECK_ERROR(
			strncmp((const char*)headerContentType.pRawValue, "application/json; charset=utf8", headerContentType.RawValueLength) == 0,
			L"/Response Content-Type header must be \"application/json; charset=utf8\".");
	}
	{
		auto& headerContentLength = pRequest->Headers.KnownHeaders[HttpHeaderContentLength];
		CHECK_ERROR(headerContentLength.pRawValue != NULL, L"/Response missing Content-Type header.");
		bodyLength = (ULONG)atoi(headerContentLength.pRawValue);
	}
	CHECK_ERROR(pRequest->Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS, L"/Response must contain body data.");

	Array<char8_t> bodyBuffer(bodyLength + 1);
	ZeroMemory(&bodyBuffer[0], bodyBuffer.Count() * sizeof(char8_t));
	{
		ULONG result = NO_ERROR;
		result = HttpReceiveRequestEntityBody(
			server->httpRequestQueue,
			pRequest->RequestId,
			HTTP_RECEIVE_REQUEST_ENTITY_BODY_FLAG_FILL_BUFFER,
			&bodyBuffer[0],
			bodyLength,
			&bodyReceived,
			NULL);
		CHECK_ERROR(result == NO_ERROR, L"HttpReceiveRequestEntityBody.");
	}

	SPIN_LOCK(pendingRequestLock)
	{
		submittingResponse = true;
	}

	try
	{
		SPIN_LOCK(lockQueuedStrings)
		{
			U8String bodyUtf8 = U8String::Unmanaged(&bodyBuffer[0]);
			if (callback)
			{
				callback->OnReadString(u8tow(bodyUtf8));
			}
			else
			{
				queuedStrings.Add(u8tow(bodyUtf8));
			}
		}
	}
	catch (...)
	{
		SPIN_LOCK(pendingRequestLock)
		{
			submittingResponse = false;
			responsesToSubmit.Clear();
		}
		throw;
	}

	WString responseToClient;
	SPIN_LOCK(pendingRequestLock)
	{
		submittingResponse = false;
		if (responsesToSubmit.Count() > 0)
		{
			responseToClient = responsesToSubmit[0];
			responsesToSubmit.RemoveAt(0);
			while (responsesToSubmit.Count() > 0)
			{
				pendingRequestsToSend.Add(responsesToSubmit[0]);
				responsesToSubmit.RemoveAt(0);
			}
		}
		else if (pendingRequestsToSend.Count() > 0)
		{
			responseToClient = pendingRequestsToSend[0];
			pendingRequestsToSend.RemoveAt(0);
		}
	}
	return responseToClient;
}

void HttpServerConnection::InstallCallback(INetworkProtocolCallback* _callback)
{
	CHECK_ERROR(_callback, L"HttpServerConnection::InstallCallback needs a valid INetworkProtocolCallback.");
	_callback->OnInstalled(this);

	List<WString> strings;
	SPIN_LOCK(lockQueuedStrings)
	{
		callback = _callback;
		strings = std::move(queuedStrings);
	}
	for (const auto& str : strings)
	{
		_callback->OnReadString(str);
	}
}

void HttpServerConnection::BeginReadingLoopUnsafe()
{
	// Do nothing, HttpServer automatically handles this.
}

void HttpServerConnection::SendString(const WString& str)
{
	SPIN_LOCK(pendingRequestLock)
	{
		if (submittingResponse)
		{
			responsesToSubmit.Add(str);
		}
		else if (httpPendingRequestId != HTTP_NULL_ID)
		{
			ULONG result = HttpServer::SendResponse(server->httpRequestQueue, httpPendingRequestId, str);
			if (result == NO_ERROR)
			{
				httpPendingRequestId = HTTP_NULL_ID;
			}
			else if (result == ERROR_CONNECTION_INVALID || result == ERROR_OPERATION_ABORTED)
			{
				httpPendingRequestId = HTTP_NULL_ID;
				pendingRequestsToSend.Add(str);
			}
			else
			{
				CHECK_FAIL(L"HttpSendHttpResponse failed for responding /Request.");
			}
		}
		else
		{
			pendingRequestsToSend.Add(str);
		}
	}
}

void HttpServerConnection::Stop()
{
	auto holding = Ptr(this);
	if (server)
	{
		SPIN_LOCK(server->lockConnections)
		{
			server->connections.Remove(guid);
		}
		SPIN_LOCK(pendingRequestLock)
		{
			OnCancelCurrentHttpRequestForPendingRequest();
		}
		if (callback)
		{
			callback->OnDisconnected();
		}
	}
}

WString HttpServerConnection::GenerateNewGuid()
{
	RPC_STATUS status = -1;
	UUID guid;
	status = UuidCreate(&guid);
	CHECK_ERROR(status == RPC_S_OK, L"UuidCreate failed.");

	RPC_WSTR guidString = nullptr;
	status = UuidToString(&guid, &guidString);
	CHECK_ERROR(status == RPC_S_OK, L"UuidToString failed.");

	WString result = guidString;
	status = RpcStringFree(&guidString);
	CHECK_ERROR(status == RPC_S_OK, L"RpcStringFree failed.");
	return result;
}

/***********************************************************************
HttpServer (ListenToHttpRequest)
***********************************************************************/

void HttpServer::OnHttpConnectionBrokenUnsafe()
{
	if (state == State::Running)
	{
		SPIN_LOCK(lockConnections)
		{
			state = State::Stopping;
			for (auto connection : connections.Values())
			{
				connection->server = nullptr;
			}

			for (auto connection : connections.Values())
			{
				connection->Stop();
			}
			connections.Clear();
		}
	}
}

void HttpServer::OnHttpRequestReceivedUnsafe(PHTTP_REQUEST pRequest)
{
	if (state == State::Stopping)
	{
		Send404Response(httpRequestQueue, pRequest->RequestId, "Server is stopping");
		return;
	}

	bool isValidRequest = wcsncmp(pRequest->CookedUrl.pAbsPath, urlRequestPrefix.Buffer(), urlRequestPrefix.Length()) == 0;
	bool isValidResponse = wcsncmp(pRequest->CookedUrl.pAbsPath, urlResponsePrefix.Buffer(), urlResponsePrefix.Length()) == 0;

	auto FindExistingConnection = [=, this](const WString& guid)->Ptr<HttpServerConnection>
	{
		SPIN_LOCK(lockConnections)
		{
			vint index = connections.Keys().IndexOf(guid);
			if (index == -1)
			{
				Send404Response(httpRequestQueue, pRequest->RequestId, "Unknown connection guid");
			}
			else
			{
				return connections.Values()[index];
			}
		}
		return {};
	};

	if (pRequest->Verb == HttpVerbGET && pRequest->CookedUrl.pAbsPath == urlConnect)
	{
		auto newGuid = HttpServerConnection::GenerateNewGuid();
		auto connection = Ptr(new HttpServerConnection);
		connection->server = this;
		connection->guid = newGuid;
		SPIN_LOCK(lockConnections)
		{
			connections.Add(newGuid, connection);
		}
		auto result = OnClientConnected(connection.Obj());
		if (result == WaitForClientResult::Reject)
		{
			SPIN_LOCK(lockConnections)
			{
				connections.Remove(newGuid);
			}
			connection->server = nullptr;
			Send404Response(httpRequestQueue, pRequest->RequestId, "Connection rejected");
		}
		else
		{
			auto completeUrlRequest = WString::Unmanaged(HttpServerUrl_Request) + L"/" + newGuid;
			auto completeUrlResponse = WString::Unmanaged(HttpServerUrl_Response) + L"/" + newGuid;
			SendResponse(httpRequestQueue, pRequest->RequestId, completeUrlRequest + L";" + completeUrlResponse);
		}
	}
	else if (pRequest->Verb == HttpVerbPOST && isValidRequest)
	{
		auto guid = WString::Unmanaged(pRequest->CookedUrl.pAbsPath + urlRequestPrefix.Length());
		if (auto connection = FindExistingConnection(guid))
		{
			SPIN_LOCK(connection->pendingRequestLock)
			{
				connection->OnNewHttpRequestForPendingRequest(pRequest->RequestId);
			}
		}
	}
	else if (pRequest->Verb == HttpVerbPOST && isValidResponse)
	{
		auto guid = WString::Unmanaged(pRequest->CookedUrl.pAbsPath + urlResponsePrefix.Length());
		if (auto connection = FindExistingConnection(guid))
		{
			auto responseToClient = connection->SubmitResponse(pRequest);
			auto result = SendResponse(httpRequestQueue, pRequest->RequestId, responseToClient);
			CHECK_ERROR(
				result == NO_ERROR || result == ERROR_CONNECTION_INVALID || result == ERROR_OPERATION_ABORTED,
				L"HttpSendHttpResponse failed for responding /Response."
				);
		}
	}
	else if (pRequest->Verb == HttpVerbOPTIONS && (isValidRequest || isValidResponse))
	{
		SendOptionsResponse(httpRequestQueue, pRequest->RequestId);
	}
	else
	{
		Send404Response(httpRequestQueue, pRequest->RequestId, "Unknown URL");
	}
	ListenToHttpRequest();
}

ULONG HttpServer::ListenToHttpRequest_Init(OVERLAPPED* overlapped)
{
	ZeroMemory(&bufferRequest[0], bufferRequest.Count());

	ULONG result = HttpReceiveHttpRequest(
		httpRequestQueue,
		HTTP_NULL_ID,
		0,
		(PHTTP_REQUEST)&bufferRequest[0],
		(ULONG)bufferRequest.Count(),
		NULL,
		overlapped);

	return result;
}

ULONG HttpServer::ListenToHttpRequest_InitMoreData(ULONG* bytesReturned)
{
	HTTP_REQUEST_ID httpRequestIdReading = ((PHTTP_REQUEST)&bufferRequest[0])->RequestId;
	ZeroMemory(&bufferRequest[0], bufferRequest.Count());

	ULONG result = HttpReceiveHttpRequest(
		httpRequestQueue,
		httpRequestIdReading,
		0,
		(PHTTP_REQUEST)&bufferRequest[0],
		(ULONG)bufferRequest.Count(),
		bytesReturned,
		NULL);

	return result;
}

ULONG HttpServer::ListenToHttpRequest_OverlappedMoreData(vint expectedBufferSize)
{
	HTTP_REQUEST_ID httpRequestIdReading = ((PHTTP_REQUEST)&bufferRequest[0])->RequestId;
	bufferRequest.Resize(expectedBufferSize);
	ZeroMemory(&bufferRequest[0], bufferRequest.Count());

	ULONG bytesReturned = 0;
	ULONG result = HttpReceiveHttpRequest(
		httpRequestQueue,
		httpRequestIdReading,
		0,
		(PHTTP_REQUEST)&bufferRequest[0],
		(ULONG)bufferRequest.Count(),
		&bytesReturned,
		NULL);

	return result;
}

void HttpServer::ListenToHttpRequest()
{
	if (state == State::Stopping) return;

	ResetEvent(hEventRequest);
	ZeroMemory(&overlappedRequest, sizeof(overlappedRequest));
	overlappedRequest.hEvent = hEventRequest;

	ZeroMemory(&bufferRequest[0], sizeof(HTTP_REQUEST));
	PHTTP_REQUEST pRequest = (PHTTP_REQUEST)&bufferRequest[0];

	ULONG result = ListenToHttpRequest_Init(&overlappedRequest);
	if (result == ERROR_CONNECTION_INVALID || result == ERROR_OPERATION_ABORTED)
	{
		OnHttpConnectionBrokenUnsafe();
		return;
	}

	if (result == NO_ERROR)
	{
		OnHttpRequestReceivedUnsafe(pRequest);
		return;
	}

	if (result == ERROR_MORE_DATA)
	{
		ULONG bytesReturned = 0;
		result = ListenToHttpRequest_InitMoreData(&bytesReturned);
		if (result == ERROR_CONNECTION_INVALID || result == ERROR_OPERATION_ABORTED)
		{
			OnHttpConnectionBrokenUnsafe();
			return;
		}
		CHECK_ERROR(result == ERROR_MORE_DATA, L"HttpReceiveHttpRequest(#1) failed on unexpected result.");

		result = ListenToHttpRequest_OverlappedMoreData((vint)bytesReturned);
		if (result == ERROR_CONNECTION_INVALID || result == ERROR_OPERATION_ABORTED)
		{
			OnHttpConnectionBrokenUnsafe();
			return;
		}
		CHECK_ERROR(result == NO_ERROR, L"HttpReceiveHttpRequest(#2) failed on unexpected result.");

		PHTTP_REQUEST pRequest = (PHTTP_REQUEST)&bufferRequest[0];
		OnHttpRequestReceivedUnsafe(pRequest);
		return;
	}

	CHECK_ERROR(result == ERROR_IO_PENDING, L"HttpReceiveHttpRequest(#3) failed on unexpected result.");

	BOOL waitResult = RegisterWaitForSingleObject(
		&hWaitHandleRequest,
		hEventRequest,
		[](PVOID lpParameter, BOOLEAN TimerOrWaitFired)
		{
			auto self = (HttpServer*)lpParameter;
			struct PendingCallbackScope
			{
				HttpServer* server;

				PendingCallbackScope(HttpServer* _server)
					: server(_server)
				{
					server->BeginPendingCallback();
				}

				~PendingCallbackScope()
				{
					server->EndPendingCallback();
				}
			} pendingCallbackScope(self);

			auto waitHandle = std::atomic_ref<HANDLE>(self->hWaitHandleRequest).exchange(INVALID_HANDLE_VALUE);
			if (waitHandle != INVALID_HANDLE_VALUE)
			{
				UnregisterWait(waitHandle);
			}

			DWORD read = 0;
			BOOL result = GetOverlappedResult(self->httpRequestQueue, &self->overlappedRequest, &read, FALSE);
			if (result == TRUE)
			{
				PHTTP_REQUEST pRequest = (PHTTP_REQUEST)&self->bufferRequest[0];
				self->OnHttpRequestReceivedUnsafe(pRequest);
			}
			else
			{
				DWORD error = GetLastError();
				if (error == ERROR_CONNECTION_INVALID || error == ERROR_OPERATION_ABORTED)
				{
					self->OnHttpConnectionBrokenUnsafe();
					return;
				}
				CHECK_ERROR(error == ERROR_MORE_DATA, L"GetOverlappedResult(#4) failed on unexpected GetLastError.");
				CHECK_ERROR(self->bufferRequest.Count() < (vint)read, L"GetOverlappedResult(#5) failed on unexpected read size.");

				ULONG result = self->ListenToHttpRequest_OverlappedMoreData((vint)read);
				if (result == ERROR_CONNECTION_INVALID || result == ERROR_OPERATION_ABORTED)
				{
					self->OnHttpConnectionBrokenUnsafe();
					return;
				}
				CHECK_ERROR(result == NO_ERROR, L"HttpReceiveHttpRequest(#6) failed on unexpected result.");

				PHTTP_REQUEST pRequest = (PHTTP_REQUEST)&self->bufferRequest[0];
				self->OnHttpRequestReceivedUnsafe(pRequest);
			}
		},
		this,
		INFINITE,
		WT_EXECUTEONLYONCE);
	CHECK_ERROR(waitResult, L"RegisterWaitForSingleObject failed for HttpReceiveHttpRequest.");
	if (state == State::Stopping)
	{
		auto waitHandle = std::atomic_ref<HANDLE>(hWaitHandleRequest).exchange(INVALID_HANDLE_VALUE);
		if (waitHandle != INVALID_HANDLE_VALUE)
		{
			UnregisterWaitEx(waitHandle, INVALID_HANDLE_VALUE);
		}
	}
}

void HttpServer::BeginPendingCallback()
{
	if (pendingCallbacks++ == 0)
	{
		eventPendingCallbacks.Unsignal();
	}
}

void HttpServer::EndPendingCallback()
{
	if (--pendingCallbacks == 0)
	{
		eventPendingCallbacks.Signal();
	}
}

/***********************************************************************
HttpServer (Writing)
***********************************************************************/

void HttpServer::Send404Response(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, PCSTR reason)
{
	ULONG bytesSent = 0;
	HTTP_RESPONSE httpResponse;
	ZeroMemory(&httpResponse, sizeof(httpResponse));

	httpResponse.StatusCode = 404;
	httpResponse.pReason = reason;

	static const char headerACAOName[] = "Access-Control-Allow-Origin";
	static HTTP_UNKNOWN_HEADER unknownHeaders[] = { {
		sizeof(headerACAOName) - 1,
		1,
		headerACAOName,
		"*"
	} };
	httpResponse.Headers.UnknownHeaderCount = sizeof(unknownHeaders) / sizeof(HTTP_UNKNOWN_HEADER);
	httpResponse.Headers.pUnknownHeaders = unknownHeaders;

	ULONG result = NO_ERROR;
	result = HttpSendHttpResponse(
		httpRequestQueue,
		requestId,
		0,
		&httpResponse,
		NULL,
		&bytesSent,
		NULL,
		0,
		NULL,
		NULL);
	CHECK_ERROR(result == NO_ERROR, L"HttpSendHttpResponse failed (404).");
}

void HttpServer::SendOptionsResponse(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId)
{
	ULONG bytesSent = 0;
	HTTP_RESPONSE httpResponse;
	ZeroMemory(&httpResponse, sizeof(httpResponse));

	httpResponse.StatusCode = 200;

	static const char headerACAOName[] = "Access-Control-Allow-Origin";
	static const char headerACAMName[] = "Access-Control-Allow-Methods";
	static const char headerACAMValue[] = "POST, OPTIONS";
	static const char headerACAHName[] = "Access-Control-Allow-Headers";
	static const char headerACAHValue[] = "Content-Type";
	static HTTP_UNKNOWN_HEADER unknownHeaders[] = { {
		sizeof(headerACAOName) - 1,
		1,
		headerACAOName,
		"*"
	},{
		sizeof(headerACAMName) - 1,
		sizeof(headerACAMValue) - 1,
		headerACAMName,
		headerACAMValue
	},{
		sizeof(headerACAHName) - 1,
		sizeof(headerACAHValue) - 1,
		headerACAHName,
		headerACAHValue
	} };
	httpResponse.Headers.UnknownHeaderCount = sizeof(unknownHeaders) / sizeof(HTTP_UNKNOWN_HEADER);
	httpResponse.Headers.pUnknownHeaders = unknownHeaders;

	ULONG result = NO_ERROR;
	result = HttpSendHttpResponse(
		httpRequestQueue,
		requestId,
		0,
		&httpResponse,
		NULL,
		&bytesSent,
		NULL,
		0,
		NULL,
		NULL);
	CHECK_ERROR(result == NO_ERROR, L"HttpSendHttpResponse failed (OPTIONS).");
}

ULONG HttpServer::SendResponse(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, const WString& str)
{
	ULONG bytesSent = 0;
	HTTP_RESPONSE httpResponse;
	HTTP_DATA_CHUNK httpResponseBody;
	ZeroMemory(&httpResponse, sizeof(httpResponse));
	ZeroMemory(&httpResponseBody, sizeof(httpResponseBody));

	httpResponse.StatusCode = 200;
	httpResponse.pReason = "OK";

	U8String body = wtou8(str);
	if (body.Length() > 0)
	{
		httpResponse.EntityChunkCount = 1;
		httpResponse.pEntityChunks = &httpResponseBody;
		httpResponseBody.DataChunkType = HttpDataChunkFromMemory;
		httpResponseBody.FromMemory.pBuffer = (PVOID)body.Buffer();
		httpResponseBody.FromMemory.BufferLength = (ULONG)body.Length();
	}

	static const char headerContentType[] = "application/json; charset=utf8";
	httpResponse.Headers.KnownHeaders[HttpHeaderContentType].pRawValue = headerContentType;
	httpResponse.Headers.KnownHeaders[HttpHeaderContentType].RawValueLength = sizeof(headerContentType) - 1;

	static const char headerACAOName[] = "Access-Control-Allow-Origin";
	static HTTP_UNKNOWN_HEADER unknownHeaders[] = { {
		sizeof(headerACAOName) - 1,
		1,
		headerACAOName,
		"*"
	}};
	httpResponse.Headers.UnknownHeaderCount = sizeof(unknownHeaders) / sizeof(HTTP_UNKNOWN_HEADER);
	httpResponse.Headers.pUnknownHeaders = unknownHeaders;

	ULONG result = NO_ERROR;
	result = HttpSendHttpResponse(
		httpRequestQueue,
		requestId,
		0,
		&httpResponse,
		NULL,
		&bytesSent,
		NULL,
		0,
		NULL,
		NULL);
	return result;
}

/***********************************************************************
HttpServer
***********************************************************************/

HttpServer::HttpServer(const WString _baseUrl, vint port)
	: bufferRequest(HttpBodyInitSize)
	, baseUrl(_baseUrl)
{
	urlConnect = baseUrl + HttpServerUrl_Connect;
	urlRequestPrefix = baseUrl + HttpServerUrl_Request + L"/";
	urlResponsePrefix = baseUrl + HttpServerUrl_Response + L"/";

	hEventRequest = CreateEvent(NULL, TRUE, TRUE, NULL);
	CHECK_ERROR(hEventRequest != NULL, L"HttpServer initialization failed on CreateEvent(hEventRequest).");
	CHECK_ERROR(eventPendingCallbacks.CreateManualUnsignal(true), L"HttpServer initialization failed on eventPendingCallbacks.CreateManualUnsignal.");

	{
		ULONG result = NO_ERROR;

		result = HttpInitialize(
			HTTPAPI_VERSION_2,
			HTTP_INITIALIZE_SERVER,
			NULL);
		CHECK_ERROR(result == NO_ERROR, L"HttpInitialize failed.");

		result = HttpCreateRequestQueue(
			HTTPAPI_VERSION_2,
			NULL,
			NULL,
			0,
			&httpRequestQueue);
		CHECK_ERROR(result == NO_ERROR, L"HttpCreateRequestQueue failed.");

		result = HttpCreateServerSession(
			HTTPAPI_VERSION_2,
			&httpSessionId,
			0);
		CHECK_ERROR(result == NO_ERROR, L"HttpCreateServerSession failed.");

		result = HttpCreateUrlGroup(
			httpSessionId,
			&httpUrlGroupId,
			0);
		CHECK_ERROR(result == NO_ERROR, L"HttpCreateUrlGroup failed.");
	}
	{
		ULONG result = NO_ERROR;

		result = HttpAddUrlToUrlGroup(
			httpUrlGroupId,
			(WString::Unmanaged(L"http://localhost:") + itow(port) + baseUrl + WString::Unmanaged(HttpServerUrl_Connect)).Buffer(),
			0,
			0);
		CHECK_ERROR(result == NO_ERROR, L"HttpAddUrlToUrlGroup failed (urlConnect).");

		result = HttpAddUrlToUrlGroup(
			httpUrlGroupId,
			(WString::Unmanaged(L"http://localhost:") + itow(port) + baseUrl + WString::Unmanaged(HttpServerUrl_Request)).Buffer(),
			0,
			0);
		CHECK_ERROR(result == NO_ERROR, L"HttpAddUrlToUrlGroup failed (urlRequest).");

		result = HttpAddUrlToUrlGroup(
			httpUrlGroupId,
			(WString::Unmanaged(L"http://localhost:") + itow(port) + baseUrl + WString::Unmanaged(HttpServerUrl_Response)).Buffer(),
			0,
			0);
		CHECK_ERROR(result == NO_ERROR, L"HttpAddUrlToUrlGroup failed (urlResponse).");
	}
	{
		ULONG result = NO_ERROR;

		HTTP_BINDING_INFO bindingInfo;
		ZeroMemory(&bindingInfo, sizeof(bindingInfo));
		bindingInfo.Flags.Present = 1;
		bindingInfo.RequestQueueHandle = httpRequestQueue;

		result = HttpSetUrlGroupProperty(
			httpUrlGroupId,
			HttpServerBindingProperty,
			&bindingInfo,
			sizeof(bindingInfo));
		CHECK_ERROR(result == NO_ERROR, L"HttpSetUrlGroupProperty failed (HttpServerBindingProperty).");
	}
}

HttpServer::~HttpServer()
{
	Stop();
	CloseHandle(hEventRequest);
}

WaitForClientResult HttpServer::OnClientConnected(INetworkProtocolConnection* connection)
{
	return WaitForClientResult::Accept;
}

void HttpServer::Start()
{
	CHECK_ERROR(state == State::Ready, L"HttpServer can only be started once.");
	state = State::Running;
	ListenToHttpRequest();
}

void HttpServer::Stop()
{
	state = State::Stopping;
	auto waitHandle = std::atomic_ref<HANDLE>(hWaitHandleRequest).exchange(INVALID_HANDLE_VALUE);
	if (waitHandle != INVALID_HANDLE_VALUE)
	{
		UnregisterWaitEx(waitHandle, INVALID_HANDLE_VALUE);
	}
	eventPendingCallbacks.Wait();

	List<Ptr<HttpServerConnection>> stoppingConnections;
	SPIN_LOCK(lockConnections)
	{
		for (auto connection : connections.Values())
		{
			stoppingConnections.Add(connection);
		}
		connections.Clear();
	}
	for (auto connection : stoppingConnections)
	{
		SPIN_LOCK(connection->pendingRequestLock)
		{
			connection->OnCancelCurrentHttpRequestForPendingRequest();
		}
		connection->server = nullptr;
	}
	for (auto connection : stoppingConnections)
	{
		if (connection->callback)
		{
			connection->callback->OnDisconnected();
		}
	}

	if (httpRequestQueue != INVALID_HANDLE_VALUE)
	{
		HttpCloseUrlGroup(httpUrlGroupId);
		HttpCloseServerSession(httpSessionId);
		HttpCloseRequestQueue(httpRequestQueue);
		httpRequestQueue = INVALID_HANDLE_VALUE;

		HttpTerminate(
			HTTP_INITIALIZE_SERVER,
			NULL);
	}
}

bool HttpServer::IsStopped()
{
	return state == State::Stopping;
}

}
