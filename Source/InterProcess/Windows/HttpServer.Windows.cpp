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
	if (pendingRequestToSend)
	{
		ULONG result = HttpServer::SendResponse(server->httpRequestQueue, httpPendingRequestId, pendingRequestToSend.Value());
		CHECK_ERROR(result == NO_ERROR, L"HttpSendHttpResponse failed for responding /Request.");
		pendingRequestToSend.Reset();
	}
}

void HttpServerConnection::InstallCallback(INetworkProtocolCallback* _callback)
{
	callback = _callback;
	List<WString> queued;
	SPIN_LOCK(lockQueuedStrings)
	{
		CopyFrom(queued, queuedStrings);
		queuedStrings.Clear();
	}

	for (const auto& str : queued)
	{
		callback->OnReadString(str);
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
		if (httpPendingRequestId != HTTP_NULL_ID)
		{
			pendingRequestToSend.Reset();
			ULONG result = HttpServer::SendResponse(server->httpRequestQueue, httpPendingRequestId, str);
			if (result == NO_ERROR)
			{
				httpPendingRequestId = HTTP_NULL_ID;
			}
			else if (result == ERROR_CONNECTION_INVALID || result == ERROR_OPERATION_ABORTED)
			{
				httpPendingRequestId = HTTP_NULL_ID;
			}
			else
			{
				CHECK_FAIL(L"HttpSendHttpResponse failed for responding /Request.");
			}
		}
		else
		{
			pendingRequestToSend = str;
		}
	}
}

void HttpServerConnection::Stop()
{
	auto holding = Ptr(this);
	SPIN_LOCK(server->lockConnections)
	{
		server->connections.Remove(guid);
	}

	OnCancelCurrentHttpRequestForPendingRequest();
	if (callback)
	{
		callback->OnDisconnected();
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

	WString guid = guidString;
	status = RpcStringFree(&guidString);
	CHECK_ERROR(status == RPC_S_OK, L"RpcStringFree failed.");
	return guid;
}

/***********************************************************************
HttpServer (ListenToHttpRequest)
***********************************************************************/

void HttpServer::OnHttpConnectionBrokenUnsafe()
{
	switch (state)
	{
	case State::WaitForClientConnection:
		CHECK_FAIL(L"HTTP server stopped while waiting for client connection.");
		break;
	case State::Running:
		state = State::Stopping;
		callback->OnReadStoppedThreadUnsafe();
		break;
	default:
		CHECK_FAIL(L"Unexpected HTTP request.");
	}
}

void HttpServer::OnHttpRequestReceivedUnsafe(PHTTP_REQUEST pRequest)
{
	if (pRequest->Verb == HttpVerbGET && pRequest->CookedUrl.pAbsPath == urlConnect)
	{
		GenerateNewUrls();
		if (state == State::WaitForClientConnection)
		{
			state = State::Running;
			SetEvent(hEventWaitForClient);
		}
		else
		{
			SPIN_LOCK(pendingRequestLock)
			{
				OnCancelCurrentHttpRequestForPendingRequest();
				pendingRequestToSend.Reset();
			}
			callback->OnReconnectedUnsafe();
		}
		SendConnectResponse(pRequest);
	}
	else if (pRequest->Verb == HttpVerbPOST && pRequest->CookedUrl.pAbsPath == urlRequest)
	{
		SPIN_LOCK(pendingRequestLock)
		{
			OnNewHttpRequestForPendingRequest(pRequest->RequestId);
		}
	}
	else if (pRequest->Verb == HttpVerbPOST && pRequest->CookedUrl.pAbsPath == urlResponse)
	{
		SubmitResponse(pRequest);
		ULONG result = SendResponse(httpRequestQueue, pRequest->RequestId, WString::Empty, WString::Empty);
		CHECK_ERROR(result == NO_ERROR, L"HttpSendHttpResponse failed for responding /Response.");
	}
	else if (pRequest->Verb == HttpVerbOPTIONS && pRequest->CookedUrl.pAbsPath == urlResponse)
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

	RegisterWaitForSingleObject(
		&hWaitHandleRequest,
		hEventRequest,
		[](PVOID lpParameter, BOOLEAN TimerOrWaitFired)
		{
			auto self = (HttpServer*)lpParameter;
			UnregisterWait(self->hWaitHandleRequest);
			self->hWaitHandleRequest = INVALID_HANDLE_VALUE;

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
}

/***********************************************************************
HttpServer (WaitForClient)
***********************************************************************/

void HttpServer::SendConnectResponse(PHTTP_REQUEST pRequest)
{
	ULONG result = SendResponse(httpRequestQueue, pRequest->RequestId, urlRequest, urlResponse);
	CHECK_ERROR(result == NO_ERROR, L"HttpSendHttpResponse failed for establishing a connection.");
}

INetworkProtocolConnection* HttpServer::WaitForClient()
{
	CHECK_ERROR(state == State::Running, L"WaitForClient() cannot be called after Stop().");
	state = State::WaitForClientConnection;

	ResetEvent(hEventWaitForClient);
	ListenToHttpRequest();
	WaitForSingleObject(hEventWaitForClient, INFINITE);

	CHECK_ERROR(state == State::Running, L"WaitForClient() failed to connect to a client.");
}

/***********************************************************************
HttpServer (BeginReadingLoopUnsafe)
***********************************************************************/

void HttpServer::SubmitResponse(PHTTP_REQUEST pRequest)
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
			httpRequestQueue,
			pRequest->RequestId,
			HTTP_RECEIVE_REQUEST_ENTITY_BODY_FLAG_FILL_BUFFER,
			&bodyBuffer[0],
			bodyLength,
			&bodyReceived,
			NULL);
		CHECK_ERROR(result == NO_ERROR, L"HttpReceiveRequestEntityBody.");
	}
	{
		U8String bodyUtf8 = U8String::Unmanaged(&bodyBuffer[0]);
		vint channelNameLength = bodyUtf8.IndexOf(L';');
		CHECK_ERROR(channelNameLength != -1, L"/Response response body is not in the correct format: channelName;str.");
		callback->OnReadStringThreadUnsafe(u8tow(bodyUtf8.Left(channelNameLength)), u8tow(bodyUtf8.Right(bodyUtf8.Length() - channelNameLength - 1)));
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
	httpResponse.EntityChunkCount = 1;
	httpResponse.pEntityChunks = &httpResponseBody;

	U8String body = wtou8(str);
	httpResponseBody.DataChunkType = HttpDataChunkFromMemory;
	httpResponseBody.FromMemory.pBuffer = (PVOID)body.Buffer();
	httpResponseBody.FromMemory.BufferLength = (ULONG)body.Length();

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

	hEventWaitForClient = CreateEvent(NULL, TRUE, TRUE, NULL);
	CHECK_ERROR(hEventWaitForClient != NULL, L"HttpServer initialization failed on CreateEvent(hEventWaitForClient).");
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
	CloseHandle(hEventWaitForClient);
}

void HttpServer::Stop()
{
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

}