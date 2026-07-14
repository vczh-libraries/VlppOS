#include "HttpClientApi.Windows.h"

#ifndef VCZH_MSVC
static_assert(false, "Do not build this file for non-Windows applications.");
#endif

#pragma comment(lib, "WinHttp.lib")

namespace vl::inter_process::windows_http
{

using namespace vl::collections;

/***********************************************************************
HttpRequest
***********************************************************************/

void HttpRequest::SetBodyUtf8(const WString& bodyString)
{
	vint utf8Size = WideCharToMultiByte(CP_UTF8, 0, bodyString.Buffer(), (int)bodyString.Length(), NULL, 0, NULL, NULL);
	body.Resize(utf8Size);
	if (utf8Size > 0)
	{
		WideCharToMultiByte(CP_UTF8, 0, bodyString.Buffer(), (int)bodyString.Length(), &body[0], (int)utf8Size, NULL, NULL);
	}
}

/***********************************************************************
HttpResponse
***********************************************************************/

WString HttpResponse::GetBodyUtf8() const
{
	if (body.Count() == 0)
	{
		return WString::Empty;
	}

	vint utf16Size = MultiByteToWideChar(CP_UTF8, 0, &body[0], (int)body.Count(), NULL, 0);
	Array<wchar_t> utf16(utf16Size + 1);
	ZeroMemory(&utf16[0], utf16.Count() * sizeof(wchar_t));
	if (utf16Size > 0)
	{
		MultiByteToWideChar(CP_UTF8, 0, &body[0], (int)body.Count(), &utf16[0], (int)utf16Size);
	}
	return &utf16[0];
}

/***********************************************************************
HttpClientApi
***********************************************************************/

HttpError HttpClientApi::MakeError(const WString& operation, DWORD errorCode)
{
	HttpError error;
	error.operation = operation;
	error.errorCode = errorCode;
	error.message = operation + L" failed with Windows error " + itow((vint)errorCode) + L".";
	return error;
}

vint HttpClientApi::HexValue(wchar_t c)
{
	if (L'0' <= c && c <= L'9') return c - L'0';
	if (L'a' <= c && c <= L'f') return c - L'a' + 10;
	if (L'A' <= c && c <= L'F') return c - L'A' + 10;
	return -1;
}

bool HttpClientApi::IsStopping()
{
	bool result = false;
	SPIN_LOCK(lockActiveRequests)
	{
		result = stopping;
	}
	return result;
}

void HttpClientApi::BeginPendingCallback()
{
	if (pendingCallbacks++ == 0)
	{
		eventPendingCallbacks.Unsignal();
	}
}

void HttpClientApi::EndPendingCallback()
{
	if (--pendingCallbacks == 0)
	{
		eventPendingCallbacks.Signal();
	}
}

void HttpClientApi::AttachRequestUnsafe(Ptr<HttpRequestContext> context)
{
	activeRequests.Add(context);
}

void HttpClientApi::RemoveRequestUnsafe(Ptr<HttpRequestContext> context)
{
	for (vint index = 0; index < activeRequests.Count(); index++)
	{
		if (activeRequests[index] == context)
		{
			activeRequests.RemoveAt(index);
			return;
		}
	}
}

void HttpClientApi::CloseRequest(Ptr<HttpRequestContext> context)
{
	HINTERNET httpRequest = NULL;
	SPIN_LOCK(context->lockContext)
	{
		if (!context->closing)
		{
			context->closing = true;
			httpRequest = context->httpRequest;
		}
	}
	if (httpRequest)
	{
		WinHttpCloseHandle(httpRequest);
	}
}

void HttpClientApi::OnRequestHandleClosing(Ptr<HttpRequestContext> context)
{
	SPIN_LOCK(lockActiveRequests)
	{
		RemoveRequestUnsafe(context);
	}
	EndPendingCallback();
}

void HttpClientApi::CompleteRequest(Ptr<HttpRequestContext> context, HttpResponse&& response)
{
	Func<void(Variant<HttpResponse, HttpError>)> callback;
	bool stoppingNow = IsStopping();
	bool invokeCallback = false;
	SPIN_LOCK(context->lockContext)
	{
		if (!context->completed)
		{
			context->completed = true;
			callback = context->callback;
			invokeCallback = !stoppingNow;
		}
	}

	if (invokeCallback && callback)
	{
		CloseRequest(context);
		callback(Variant<HttpResponse, HttpError>(std::move(response)));
	}
	else
	{
		CloseRequest(context);
	}
}

void HttpClientApi::CompleteRequest(Ptr<HttpRequestContext> context, HttpError&& error)
{
	Func<void(Variant<HttpResponse, HttpError>)> callback;
	bool stoppingNow = IsStopping();
	bool invokeCallback = false;
	SPIN_LOCK(context->lockContext)
	{
		if (!context->completed)
		{
			context->completed = true;
			callback = context->callback;
			invokeCallback = !stoppingNow;
		}
	}

	if (invokeCallback && callback)
	{
		CloseRequest(context);
		callback(Variant<HttpResponse, HttpError>(std::move(error)));
	}
	else
	{
		CloseRequest(context);
	}
}

void HttpClientApi::CompleteRequestWithLastError(Ptr<HttpRequestContext> context, const WString& operation, DWORD errorCode)
{
	if (errorCode == ERROR_INVALID_HANDLE && IsStopping() && !context->keepAliveOnStop)
	{
		return;
	}
	CompleteRequest(context, MakeError(operation, errorCode));
}

void CALLBACK HttpClientApi::HttpStatusCallback(HINTERNET httpRequest, DWORD_PTR contextValue, DWORD status, LPVOID statusInformation, DWORD statusInformationLength)
{
	if (!contextValue) return;
	auto contextPtr = reinterpret_cast<Ptr<HttpClientApi::HttpRequestContext>*>(contextValue);
	auto context = *contextPtr;
	auto self = context->api;

	switch (status)
	{
	case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
		{
			if (self->IsStopping() && !context->keepAliveOnStop) return;

			BOOL httpResult = WinHttpReceiveResponse(httpRequest, NULL);
			DWORD lastError = GetLastError();
			if (httpResult == FALSE)
			{
				self->CompleteRequestWithLastError(context, L"WinHttpReceiveResponse", lastError);
			}
		}
		break;
	case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
		{
			if (self->IsStopping() && !context->keepAliveOnStop) return;

			DWORD statusCode = 0;
			DWORD dwordLength = sizeof(DWORD);
			BOOL httpResult = WinHttpQueryHeaders(
				httpRequest,
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX,
				&statusCode,
				&dwordLength,
				WINHTTP_NO_HEADER_INDEX);
			DWORD lastError = GetLastError();
			if (httpResult == FALSE)
			{
				self->CompleteRequestWithLastError(context, L"WinHttpQueryHeaders(status)", lastError);
				return;
			}
			context->response.statusCode = statusCode;

			DWORD headerLength = 0;
			httpResult = WinHttpQueryHeaders(
				httpRequest,
				WINHTTP_QUERY_CONTENT_TYPE,
				WINHTTP_HEADER_NAME_BY_INDEX,
				NULL,
				&headerLength,
				WINHTTP_NO_HEADER_INDEX);
			lastError = GetLastError();
			if (httpResult == FALSE && lastError == ERROR_INSUFFICIENT_BUFFER)
			{
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
				if (httpResult == FALSE)
				{
					self->CompleteRequestWithLastError(context, L"WinHttpQueryHeaders(content-type)", lastError);
					return;
				}
				context->response.contentType = &headerBuffer[0];
			}
			else if (httpResult == FALSE && lastError != ERROR_WINHTTP_HEADER_NOT_FOUND)
			{
				self->CompleteRequestWithLastError(context, L"WinHttpQueryHeaders(content-type)", lastError);
				return;
			}

			headerLength = 0;
			httpResult = WinHttpQueryHeaders(
				httpRequest,
				WINHTTP_QUERY_SET_COOKIE,
				WINHTTP_HEADER_NAME_BY_INDEX,
				NULL,
				&headerLength,
				WINHTTP_NO_HEADER_INDEX);
			lastError = GetLastError();
			if (httpResult == FALSE && lastError == ERROR_INSUFFICIENT_BUFFER)
			{
				Array<wchar_t> headerBuffer(headerLength / sizeof(wchar_t) + 1);
				ZeroMemory(&headerBuffer[0], headerBuffer.Count() * sizeof(wchar_t));
				httpResult = WinHttpQueryHeaders(
					httpRequest,
					WINHTTP_QUERY_SET_COOKIE,
					WINHTTP_HEADER_NAME_BY_INDEX,
					&headerBuffer[0],
					&headerLength,
					WINHTTP_NO_HEADER_INDEX);
				lastError = GetLastError();
				if (httpResult == FALSE)
				{
					self->CompleteRequestWithLastError(context, L"WinHttpQueryHeaders(cookie)", lastError);
					return;
				}
				context->response.cookie = &headerBuffer[0];
			}
			else if (httpResult == FALSE && lastError != ERROR_WINHTTP_HEADER_NOT_FOUND)
			{
				self->CompleteRequestWithLastError(context, L"WinHttpQueryHeaders(cookie)", lastError);
				return;
			}

			context->bodyBufferWriting = 0;
			httpResult = WinHttpQueryDataAvailable(httpRequest, NULL);
			lastError = GetLastError();
			if (httpResult == FALSE)
			{
				self->CompleteRequestWithLastError(context, L"WinHttpQueryDataAvailable", lastError);
			}
		}
		break;
	case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
		{
			if (self->IsStopping() && !context->keepAliveOnStop) return;
			CHECK_ERROR(statusInformationLength == sizeof(DWORD), L"WinHttpQueryDataAvailable returned an unexpected payload.");
			DWORD dataAvailable = *(PDWORD)statusInformation;

			if (dataAvailable == 0)
			{
				context->response.body.Resize(context->bodyBufferWriting);
				self->CompleteRequest(context, std::move(context->response));
				return;
			}

			context->bodyBufferWritingAvailable = dataAvailable;
			DWORD bufferSize = context->bodyBufferWriting + dataAvailable + 1;
			if (context->response.body.Count() < (vint)bufferSize)
			{
				context->response.body.Resize((bufferSize + HttpRespondBodyStep - 1) / HttpRespondBodyStep * HttpRespondBodyStep);
			}

			BOOL httpResult = WinHttpReadData(
				httpRequest,
				&context->response.body[context->bodyBufferWriting],
				dataAvailable,
				NULL);
			DWORD lastError = GetLastError();
			if (httpResult == FALSE)
			{
				self->CompleteRequestWithLastError(context, L"WinHttpReadData", lastError);
			}
		}
		break;
	case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
		{
			if (self->IsStopping() && !context->keepAliveOnStop) return;
			if (context->bodyBufferWritingAvailable != statusInformationLength)
			{
				self->CompleteRequest(context, MakeError(L"WinHttpReadData", ERROR_INVALID_DATA));
				return;
			}
			context->bodyBufferWriting += context->bodyBufferWritingAvailable;

			BOOL httpResult = WinHttpQueryDataAvailable(httpRequest, NULL);
			DWORD lastError = GetLastError();
			if (httpResult == FALSE)
			{
				self->CompleteRequestWithLastError(context, L"WinHttpQueryDataAvailable", lastError);
			}
		}
		break;
	case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
		{
			if (self->IsStopping() && !context->keepAliveOnStop) return;

			auto asyncResult = reinterpret_cast<WINHTTP_ASYNC_RESULT*>(statusInformation);
			DWORD errorCode = asyncResult ? asyncResult->dwError : ERROR_WINHTTP_INTERNAL_ERROR;
			HttpError error = MakeError(L"WinHTTP async request", errorCode);
			if (asyncResult)
			{
				error.message += L" Operation code: " + itow((vint)asyncResult->dwResult) + L".";
			}
			self->CompleteRequest(context, std::move(error));
		}
		break;
	case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
		self->OnRequestHandleClosing(context);
		delete contextPtr;
		break;
	}
}

HttpClientApi::HttpClientApi(const WString& _server, vint _port)
	: server(_server)
	, port(_port)
{
	CHECK_ERROR(eventPendingCallbacks.CreateManualUnsignal(true), L"HttpClientApi initialization failed on eventPendingCallbacks.CreateManualUnsignal.");

	httpSession = WinHttpOpen(
		L"vl::inter_process::windows_http::HttpClientApi",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		WINHTTP_FLAG_ASYNC);
	CHECK_ERROR(httpSession != NULL, L"WinHttpOpen failed.");

	httpConnection = WinHttpConnect(
		httpSession,
		server.Buffer(),
		(INTERNET_PORT)port,
		0);
	CHECK_ERROR(httpConnection != NULL, L"WinHttpConnect failed.");
}

HttpClientApi::~HttpClientApi()
{
	Stop();
}

void HttpClientApi::HttpQuery(const HttpRequest& request, Func<void(Variant<HttpResponse, HttpError>)> callback)
{
	bool rejected = false;
	{
		SPIN_LOCK(lockActiveRequests)
		{
			if (stopping)
			{
				rejected = true;
			}
			else
			{
				BeginPendingCallback();
			}
		}
	}
	if (rejected)
	{
		if (callback)
		{
			callback(Variant<HttpResponse, HttpError>(MakeError(L"HttpClientApi::HttpQuery", ERROR_OPERATION_ABORTED)));
		}
		return;
	}

	BOOL httpResult = FALSE;
	DWORD lastError = 0;
	List<LPCWSTR> acceptTypes;
	for (vint i = 0; i < request.acceptTypes.Count(); i++)
	{
		acceptTypes.Add(request.acceptTypes.Get(i).Buffer());
	}
	acceptTypes.Add(nullptr);

	auto method = request.method == WString::Empty ? WString::Unmanaged(L"GET") : request.method;
	auto httpRequest = WinHttpOpenRequest(
		httpConnection,
		method.Buffer(),
		request.query.Buffer(),
		NULL,
		WINHTTP_NO_REFERER,
		&acceptTypes[0],
		(request.secure ? WINHTTP_FLAG_SECURE : 0) | WINHTTP_FLAG_REFRESH);
	lastError = GetLastError();
	if (httpRequest == NULL)
	{
		EndPendingCallback();
		if (callback)
		{
			callback(Variant<HttpResponse, HttpError>(MakeError(L"WinHttpOpenRequest", lastError)));
		}
		return;
	}

	httpResult = WinHttpSetTimeouts(
		httpRequest,
		(int)request.resolveTimeout,
		(int)request.connectTimeout,
		(int)request.sendTimeout,
		(int)request.receiveTimeout);
	lastError = GetLastError();
	if (httpResult == FALSE)
	{
		EndPendingCallback();
		WinHttpCloseHandle(httpRequest);
		if (callback)
		{
			callback(Variant<HttpResponse, HttpError>(MakeError(L"WinHttpSetTimeouts", lastError)));
		}
		return;
	}

	auto contextPtr = new Ptr<HttpRequestContext>(new HttpRequestContext);
	auto context = *contextPtr;
	context->api = this;
	context->httpRequest = httpRequest;
	context->callback = callback;
	context->keepAliveOnStop = request.keepAliveOnStop;

	if (request.body.Count() > 0)
	{
		context->requestBody.Resize(request.body.Count());
		memcpy(&context->requestBody[0], &request.body.Get(0), request.body.Count());
	}

	auto failBeforeCallbackInstalled = [&](const WString& operation, DWORD errorCode)
	{
		WinHttpCloseHandle(httpRequest);
		delete contextPtr;
		EndPendingCallback();
		if (callback)
		{
			callback(Variant<HttpResponse, HttpError>(MakeError(operation, errorCode)));
		}
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
		failBeforeCallbackInstalled(L"WinHttpSetOption(WINHTTP_OPTION_CONTEXT_VALUE)", lastError);
		return;
	}

	if (request.username != WString::Empty && request.password != WString::Empty)
	{
		httpResult = WinHttpSetCredentials(
			httpRequest,
			WINHTTP_AUTH_TARGET_SERVER,
			WINHTTP_AUTH_SCHEME_BASIC,
			request.username.Buffer(),
			request.password.Buffer(),
			NULL);
		lastError = GetLastError();
		if (httpResult == FALSE)
		{
			failBeforeCallbackInstalled(L"WinHttpSetCredentials", lastError);
			return;
		}
	}
	if (request.contentType != WString::Empty)
	{
		httpResult = WinHttpAddRequestHeaders(
			httpRequest,
			(L"Content-Type: " + request.contentType).Buffer(),
			-1,
			WINHTTP_ADDREQ_FLAG_REPLACE | WINHTTP_ADDREQ_FLAG_ADD);
		lastError = GetLastError();
		if (httpResult == FALSE)
		{
			failBeforeCallbackInstalled(L"WinHttpAddRequestHeaders(content-type)", lastError);
			return;
		}
	}
	if (request.cookie != WString::Empty)
	{
		httpResult = WinHttpAddRequestHeaders(
			httpRequest,
			(L"Cookie: " + request.cookie).Buffer(),
			-1,
			WINHTTP_ADDREQ_FLAG_REPLACE | WINHTTP_ADDREQ_FLAG_ADD);
		lastError = GetLastError();
		if (httpResult == FALSE)
		{
			failBeforeCallbackInstalled(L"WinHttpAddRequestHeaders(cookie)", lastError);
			return;
		}
	}

	for (vint i = 0; i < request.extraHeaders.Count(); i++)
	{
		WString key = request.extraHeaders.Keys()[i];
		WString value = request.extraHeaders.Values().Get(i);
		httpResult = WinHttpAddRequestHeaders(
			httpRequest,
			(key + L": " + value).Buffer(),
			-1,
			WINHTTP_ADDREQ_FLAG_REPLACE | WINHTTP_ADDREQ_FLAG_ADD);
		lastError = GetLastError();
		if (httpResult == FALSE)
		{
			failBeforeCallbackInstalled(L"WinHttpAddRequestHeaders(extra)", lastError);
			return;
		}
	}

	bool failedBeforeSend = false;
	WString failedOperation;
	DWORD failedError = 0;
	SPIN_LOCK(lockActiveRequests)
	{
		if (stopping)
		{
			failedBeforeSend = true;
			failedOperation = L"HttpClientApi::HttpQuery";
			failedError = ERROR_OPERATION_ABORTED;
		}
		else
		{
			auto previousCallback = WinHttpSetStatusCallback(
				httpRequest,
				HttpStatusCallback,
				WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
				NULL);
			lastError = GetLastError();
			if (previousCallback == WINHTTP_INVALID_STATUS_CALLBACK)
			{
				failedBeforeSend = true;
				failedOperation = L"WinHttpSetStatusCallback";
				failedError = lastError;
			}
			else
			{
				AttachRequestUnsafe(context);
			}
		}
	}
	if (failedBeforeSend)
	{
		failBeforeCallbackInstalled(failedOperation, failedError);
		return;
	}

	DWORD requestBodyLength = (DWORD)context->requestBody.Count();
	LPVOID requestBodyBuffer = requestBodyLength == 0 ? WINHTTP_NO_REQUEST_DATA : (LPVOID)&context->requestBody[0];
	httpResult = WinHttpSendRequest(
		httpRequest,
		WINHTTP_NO_ADDITIONAL_HEADERS,
		0,
		requestBodyBuffer,
		requestBodyLength,
		requestBodyLength,
		contextValue);
	lastError = GetLastError();
	if (httpResult == FALSE)
	{
		CompleteRequestWithLastError(context, L"WinHttpSendRequest", lastError);
	}
}

void HttpClientApi::Stop()
{
	if (httpSession == NULL) return;

	List<HINTERNET> stoppingRequests;
	SPIN_LOCK(lockActiveRequests)
	{
		stopping = true;
		for (auto&& context : activeRequests)
		{
			HINTERNET httpRequest = NULL;
			SPIN_LOCK(context->lockContext)
			{
				if (!context->keepAliveOnStop && !context->closing)
				{
					context->closing = true;
					httpRequest = context->httpRequest;
				}
			}
			if (httpRequest)
			{
				stoppingRequests.Add(httpRequest);
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

	SPIN_LOCK(lockActiveRequests)
	{
		activeRequests.Clear();
	}

	httpConnection = NULL;
	httpSession = NULL;
}

WString HttpClientApi::UrlEncodeQuery(const WString& query)
{
	vint utf8Size = WideCharToMultiByte(CP_UTF8, 0, query.Buffer(), (int)query.Length(), NULL, 0, NULL, NULL);
	Array<char> utf8(utf8Size);
	if (utf8Size > 0)
	{
		WideCharToMultiByte(CP_UTF8, 0, query.Buffer(), (int)query.Length(), &utf8[0], (int)utf8Size, NULL, NULL);
	}

	Array<wchar_t> encoded(utf8Size * 3 + 1);
	ZeroMemory(&encoded[0], encoded.Count() * sizeof(wchar_t));
	wchar_t* writing = &encoded[0];
	for (vint i = 0; i < utf8Size; i++)
	{
		unsigned char x = (unsigned char)utf8[i];
		if ((L'a' <= x && x <= L'z') || (L'A' <= x && x <= L'Z') || (L'0' <= x && x <= L'9'))
		{
			writing[0] = x;
			writing += 1;
		}
		else
		{
			writing[0] = L'%';
			writing[1] = L"0123456789ABCDEF"[x / 16];
			writing[2] = L"0123456789ABCDEF"[x % 16];
			writing += 3;
		}
	}

	return &encoded[0];
}

WString HttpClientApi::UrlDecodeQuery(const WString& query)
{
	List<char> utf8;
	for (vint i = 0; i < query.Length(); i++)
	{
		wchar_t c = query[i];
		if (c == L'%' && i + 2 < query.Length())
		{
			vint high = HexValue(query[i + 1]);
			vint low = HexValue(query[i + 2]);
			if (high != -1 && low != -1)
			{
				utf8.Add((char)(high * 16 + low));
				i += 2;
				continue;
			}
		}

		if (c == L'+')
		{
			utf8.Add(' ');
		}
		else if (c <= 0x7F)
		{
			utf8.Add((char)c);
		}
		else
		{
			wchar_t single[] = { c, 0 };
			vint utf8Size = WideCharToMultiByte(CP_UTF8, 0, single, 1, NULL, 0, NULL, NULL);
			if (utf8Size > 0)
			{
				Array<char> singleUtf8(utf8Size);
				WideCharToMultiByte(CP_UTF8, 0, single, 1, &singleUtf8[0], (int)utf8Size, NULL, NULL);
				for (vint j = 0; j < utf8Size; j++)
				{
					utf8.Add(singleUtf8[j]);
				}
			}
		}
	}

	if (utf8.Count() == 0)
	{
		return WString::Empty;
	}

	vint utf16Size = MultiByteToWideChar(CP_UTF8, 0, &utf8[0], (int)utf8.Count(), NULL, 0);
	Array<wchar_t> utf16(utf16Size + 1);
	ZeroMemory(&utf16[0], utf16.Count() * sizeof(wchar_t));
	if (utf16Size > 0)
	{
		MultiByteToWideChar(CP_UTF8, 0, &utf8[0], (int)utf8.Count(), &utf16[0], (int)utf16Size);
	}
	return &utf16[0];
}

}
