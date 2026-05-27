#include "HttpServerApi.Windows.h"

#ifndef VCZH_MSVC
static_assert(false, "Do not build this file for non-Windows applications.");
#endif

#pragma comment(lib, "Httpapi.lib")

namespace vl::inter_process
{

using namespace vl::collections;

/***********************************************************************
HttpServerApi (ListenToHttpRequest)
***********************************************************************/

void HttpServerApi::OnHttpConnectionBrokenUnsafe()
{
	if (state == State::Running)
	{
		state = State::Stopping;
		OnHttpServerStopping();
	}
}

void HttpServerApi::OnHttpRequestReceivedUnsafe(PHTTP_REQUEST pRequest)
{
	if (state == State::Stopping)
	{
		SendResponse(httpRequestQueue, pRequest->RequestId, { 404, L"Server is stopping" });
		return;
	}

	if (respondToOptions && pRequest->Verb == HttpVerbOPTIONS)
	{
		SendOptionsResponse(httpRequestQueue, pRequest->RequestId);
	}
	else
	{
		OnHttpRequestReceived(pRequest);
	}
	ListenToHttpRequest();
}

ULONG HttpServerApi::ListenToHttpRequest_Init(OVERLAPPED* overlapped)
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

ULONG HttpServerApi::ListenToHttpRequest_InitMoreData(ULONG* bytesReturned)
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

ULONG HttpServerApi::ListenToHttpRequest_OverlappedMoreData(vint expectedBufferSize)
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

void HttpServerApi::ListenToHttpRequest()
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
			auto self = (HttpServerApi*)lpParameter;
			struct PendingCallbackScope
			{
				HttpServerApi* server;

				PendingCallbackScope(HttpServerApi* _server)
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

void HttpServerApi::BeginPendingCallback()
{
	if (pendingCallbacks++ == 0)
	{
		eventPendingCallbacks.Unsignal();
	}
}

void HttpServerApi::EndPendingCallback()
{
	if (--pendingCallbacks == 0)
	{
		eventPendingCallbacks.Signal();
	}
}

void HttpServerApi::OnHttpServerStopping()
{
}

HANDLE HttpServerApi::GetHttpRequestQueue() const
{
	return httpRequestQueue;
}

/***********************************************************************
HttpServerApi (Writing)
***********************************************************************/

void HttpServerApi::SendOptionsResponse(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId)
{
	ULONG bytesSent = 0;
	HTTP_RESPONSE httpResponse;
	ZeroMemory(&httpResponse, sizeof(httpResponse));

	httpResponse.StatusCode = 200;

	static const char headerACAOName[] = "Access-Control-Allow-Origin";
	static const char headerACAMName[] = "Access-Control-Allow-Methods";
	static const char headerACAMValue[] = "GET, POST, OPTIONS";
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

ULONG HttpServerApi::SendResponse(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, const HttpServerResponse& response)
{
	ULONG bytesSent = 0;
	HTTP_RESPONSE httpResponse;
	HTTP_DATA_CHUNK httpResponseBody;
	ZeroMemory(&httpResponse, sizeof(httpResponse));
	ZeroMemory(&httpResponseBody, sizeof(httpResponseBody));

	httpResponse.StatusCode = (USHORT)response.statusCode;

	U8String reasonUtf8;
	if (response.reason != WString::Empty)
	{
		reasonUtf8 = wtou8(response.reason);
		httpResponse.pReason = (PCSTR)reasonUtf8.Buffer();
		httpResponse.ReasonLength = (USHORT)reasonUtf8.Length();
	}

	U8String bodyUtf8;
	if (response.body != WString::Empty)
	{
		bodyUtf8 = wtou8(response.body);
		httpResponse.EntityChunkCount = 1;
		httpResponse.pEntityChunks = &httpResponseBody;
		httpResponseBody.DataChunkType = HttpDataChunkFromMemory;
		httpResponseBody.FromMemory.pBuffer = (PVOID)bodyUtf8.Buffer();
		httpResponseBody.FromMemory.BufferLength = (ULONG)bodyUtf8.Length();
	}

	U8String contentTypeUtf8;
	if (response.contentType != WString::Empty)
	{
		contentTypeUtf8 = wtou8(response.contentType);
		httpResponse.Headers.KnownHeaders[HttpHeaderContentType].pRawValue = (PCSTR)contentTypeUtf8.Buffer();
		httpResponse.Headers.KnownHeaders[HttpHeaderContentType].RawValueLength = (USHORT)contentTypeUtf8.Length();
	}

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
	return result;
}

/***********************************************************************
HttpServerApi
***********************************************************************/

HttpServerApi::HttpServerApi(const WString& _urlPrefix, bool _respondToOptions)
	: bufferRequest(HttpRequestBufferInitSize)
	, urlPrefix(_urlPrefix)
	, respondToOptions(_respondToOptions)
{
	hEventRequest = CreateEvent(NULL, TRUE, TRUE, NULL);
	CHECK_ERROR(hEventRequest != NULL, L"HttpServerApi initialization failed on CreateEvent(hEventRequest).");
	CHECK_ERROR(eventPendingCallbacks.CreateManualUnsignal(true), L"HttpServerApi initialization failed on eventPendingCallbacks.CreateManualUnsignal.");

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

	result = HttpAddUrlToUrlGroup(
		httpUrlGroupId,
		urlPrefix.Buffer(),
		0,
		0);
	CHECK_ERROR(result == NO_ERROR, L"HttpAddUrlToUrlGroup failed.");

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

HttpServerApi::~HttpServerApi()
{
	Stop();
	CloseHandle(hEventRequest);
}

void HttpServerApi::Start()
{
	CHECK_ERROR(state == State::Ready, L"HttpServerApi can only be started once.");
	state = State::Running;
	ListenToHttpRequest();
}

void HttpServerApi::Stop()
{
	state = State::Stopping;
	auto waitHandle = std::atomic_ref<HANDLE>(hWaitHandleRequest).exchange(INVALID_HANDLE_VALUE);
	if (waitHandle != INVALID_HANDLE_VALUE)
	{
		UnregisterWaitEx(waitHandle, INVALID_HANDLE_VALUE);
	}
	eventPendingCallbacks.Wait();

	OnHttpServerStopping();

	if (httpRequestQueue != INVALID_HANDLE_VALUE)
	{
		HttpCloseUrlGroup(httpUrlGroupId);
		HttpCloseServerSession(httpSessionId);
		HttpCloseRequestQueue(httpRequestQueue);
		httpRequestQueue = INVALID_HANDLE_VALUE;
		httpUrlGroupId = HTTP_NULL_ID;
		httpSessionId = HTTP_NULL_ID;

		HttpTerminate(
			HTTP_INITIALIZE_SERVER,
			NULL);
	}
}

bool HttpServerApi::IsStopped()
{
	return state == State::Stopping;
}

}
