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
	if (state == State::Stopping) return;
	CHECK_ERROR(state == State::Running, L"BeginReadingLoopUnsafe can only be called when client is running.");
	DWORD lastError = 0;
	BOOL httpResult = FALSE;

	LPCWSTR acceptTypes[] = { L"application/json; charset=utf8", NULL };
	HINTERNET httpRequest = WinHttpOpenRequest(
		httpConnection,
		L"POST",
		urlRequest.Buffer(),
		NULL,
		WINHTTP_NO_REFERER,
		acceptTypes,
		WINHTTP_FLAG_REFRESH);
	lastError = GetLastError();
	if (lastError == ERROR_INVALID_HANDLE)
	{
		CHECK_ERROR(state == State::Stopping, L"WinHttpOpenRequest failed with ERROR_INVALID_HANDLE but client is not stopping.");
		return;
	}
	CHECK_ERROR(httpRequest != NULL, L"WinHttpOpenRequest failed.");
	{
		auto self = this;
		WINHTTP_STATUS_CALLBACK previousCallback = WinHttpSetStatusCallback(
			httpRequest,
			(WINHTTP_STATUS_CALLBACK)[](HINTERNET httpRequest, DWORD_PTR dwContext, DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD dwStatusInformationLength) -> void
			{
				if (!dwContext) return;
				auto self = reinterpret_cast<HttpClient*>(dwContext);
				switch (dwInternetStatus)
				{
				case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
					{
						self->QueueCallback([=]()
						{
							if (self->state == State::Stopping) return;
							DWORD lastError = 0;
							BOOL httpResult = WinHttpReceiveResponse(httpRequest, NULL);
							lastError = GetLastError();
							if (lastError == ERROR_INVALID_HANDLE)
							{
								CHECK_ERROR(self->state == State::Stopping, L"WinHttpReceiveResponse failed with ERROR_INVALID_HANDLE but client is not stopping.");
								WinHttpCloseHandle(httpRequest);
								return;
							}
							CHECK_ERROR(httpResult == TRUE, L"WinHttpReceiveResponse failed.");
						});
					}
					break;
				case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
					{
						self->QueueCallback([=]()
						{
							if (self->state == State::Stopping) return;
							DWORD lastError = 0;
							DWORD statusCode = 0;
							DWORD dwordLength = sizeof(DWORD);
							BOOL httpResult = FALSE;
							{
								httpResult = WinHttpQueryHeaders(
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
									self->CloseRequest(httpRequest);
									self->RaiseErrorUnsafe(WString::Unmanaged(L"/Request returned status code: ") + itow(statusCode) + L", another renderer may have connected to the core.");
									return;
								}
							}
							{
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
								CHECK_ERROR(httpResult == FALSE && lastError == ERROR_INSUFFICIENT_BUFFER, L"WinHttpQueryHeaders failed to retrieve content type.");

								Array<wchar_t> headerBuffer(headerLength / 2 + 1);
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
								CHECK_ERROR(wcscmp(header, L"application/json; charset=utf8") == 0, L"/Request did not return content type: application/json; charset=utf8.");
							}
							{
								self->httpRespondBodyBufferWriting = 0;
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
						});
					}
					break;
				case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
					{
						DWORD dataAvailable = *(PDWORD)lpvStatusInformation;
						self->QueueCallback([=]()
						{
							if (self->state == State::Stopping) return;
							if (dataAvailable == 0)
							{
								if (self->callback)
								{
									self->httpRespondBodyBuffer[self->httpRespondBodyBufferWriting] = 0;
									U8String bodyUtf8 = U8String::Unmanaged(&self->httpRespondBodyBuffer[0]);
									self->callback->OnReadString(u8tow(bodyUtf8));
								}
								self->CloseRequest(httpRequest);
								self->BeginReadingLoopUnsafe();
								return;
							}

							self->httpRespondBodyBufferWritingAvailable = dataAvailable;
							DWORD bufferSize = self->httpRespondBodyBufferWriting + dataAvailable + 1;
							if (self->httpRespondBodyBuffer.Count() < (vint)bufferSize)
							{
								self->httpRespondBodyBuffer.Resize((bufferSize + HttpRespondBodyStep - 1) / HttpRespondBodyStep * HttpRespondBodyStep);
							}

							DWORD lastError = 0;
							BOOL httpResult = WinHttpReadData(
								httpRequest,
								&self->httpRespondBodyBuffer[self->httpRespondBodyBufferWriting],
								dataAvailable,
								NULL);
							lastError = GetLastError();
							if (lastError == ERROR_INVALID_HANDLE)
							{
								CHECK_ERROR(self->state == State::Stopping, L"WinHttpReadData failed with ERROR_INVALID_HANDLE but client is not stopping.");
								return;
							}
							CHECK_ERROR(httpResult == TRUE, L"WinHttpReadData failed.");
						});
					}
					break;
				case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
					{
						self->QueueCallback([=]()
						{
							if (self->state == State::Stopping) return;
							CHECK_ERROR(
								self->httpRespondBodyBufferWritingAvailable == dwStatusInformationLength,
								L"WinHttpReadData failed to read all available data."
								);
							self->httpRespondBodyBufferWriting += self->httpRespondBodyBufferWritingAvailable;

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
						});
					}
					break;
				case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
					{
						self->QueueCallback([=]()
						{
							if (self->state == State::Stopping) return;
							self->CloseRequest(httpRequest);
							self->RaiseErrorUnsafe(WString::Unmanaged(L"/Request canceled, another renderer may have connected to the core."));
						});
					}
					break;
				case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
					self->OnRequestHandleClosing(httpRequest);
					break;
				}
			},
			WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
			NULL);
		lastError = GetLastError();
		if (previousCallback == WINHTTP_INVALID_STATUS_CALLBACK && lastError == ERROR_INVALID_HANDLE)
		{
			CHECK_ERROR(state == State::Stopping, L"WinHttpSetStatusCallback failed with ERROR_INVALID_HANDLE but client is not stopping.");
			WinHttpCloseHandle(httpRequest);
			return;
		}
		CHECK_ERROR(previousCallback != WINHTTP_INVALID_STATUS_CALLBACK, L"WinHttpSetStatusCallback failed.");
	}
	{
		AttachRequest(httpRequest);
		httpResult = WinHttpSendRequest(
			httpRequest,
			WINHTTP_NO_ADDITIONAL_HEADERS,
			0,
			WINHTTP_NO_REQUEST_DATA,
			0,
			0,
			reinterpret_cast<DWORD_PTR>(this));
		lastError = GetLastError();
		if (httpResult == FALSE && lastError == ERROR_INVALID_HANDLE)
		{
			OnRequestHandleClosing(httpRequest);
			CHECK_ERROR(state == State::Stopping, L"WinHttpSendRequest failed with ERROR_INVALID_HANDLE but client is not stopping.");
			WinHttpCloseHandle(httpRequest);
			return;
		}
		if (httpResult == FALSE)
		{
			OnRequestHandleClosing(httpRequest);
			WinHttpCloseHandle(httpRequest);
			CHECK_FAIL(L"WinHttpSendRequest failed.");
		}
	}
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
	DWORD lastError = 0;
	state = State::WaitForServerConnection;
	LPCWSTR acceptTypes[] = { L"application/json; charset=utf8", NULL };
	BOOL httpResult = FALSE;

	HINTERNET httpRequest = WinHttpOpenRequest(
		httpConnection,
		L"GET",
		urlConnect.Buffer(),
		NULL,
		WINHTTP_NO_REFERER,
		acceptTypes,
		WINHTTP_FLAG_REFRESH);
	lastError = GetLastError();
	CHECK_ERROR(httpRequest != NULL, L"WinHttpOpenRequest failed.");
	{
		WINHTTP_STATUS_CALLBACK previousCallback = WinHttpSetStatusCallback(
			httpRequest,
			(WINHTTP_STATUS_CALLBACK)[](HINTERNET httpRequest, DWORD_PTR dwContext, DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD dwStatusInformationLength) -> void
			{
				if (!dwContext) return;
				auto self = reinterpret_cast<HttpClient*>(dwContext);
				switch (dwInternetStatus)
				{
				case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
				case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
				case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
				case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
					{
						self->dwInternetStatus_WaitForServer = dwInternetStatus;
						self->dwStatusInformationLength_WaitForServer = dwStatusInformationLength;
						SetEvent(self->hEventWaitForServer);
					}
					break;
				}
			},
			WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
			NULL);
		lastError = GetLastError();
		CHECK_ERROR(previousCallback != WINHTTP_INVALID_STATUS_CALLBACK, L"WinHttpSetStatusCallback failed.");
	}
	{
		ResetEvent(hEventWaitForServer);
		httpResult = WinHttpSendRequest(
			httpRequest,
			WINHTTP_NO_ADDITIONAL_HEADERS,
			0,
			WINHTTP_NO_REQUEST_DATA,
			0,
			0,
			reinterpret_cast<DWORD_PTR>(this));
		lastError = GetLastError();
		CHECK_ERROR(httpResult == TRUE, L"WinHttpSendRequest failed.");
		WaitForSingleObject(hEventWaitForServer, INFINITE);
		CHECK_ERROR(dwInternetStatus_WaitForServer == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, L"WinHttpSendRequest failed to complete.");
	}
	{
		ResetEvent(hEventWaitForServer);
		httpResult = WinHttpReceiveResponse(httpRequest, NULL);
		lastError = GetLastError();
		CHECK_ERROR(httpResult == TRUE, L"WinHttpReceiveResponse failed.");
		WaitForSingleObject(hEventWaitForServer, INFINITE);
		CHECK_ERROR(dwInternetStatus_WaitForServer == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, L"WinHttpSendRequest failed to complete.");
	}

	DWORD statusCode = 0;
	DWORD dataLength = 0;
	DWORD dwordLength = sizeof(DWORD);
	{
		httpResult = WinHttpQueryHeaders(
			httpRequest,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&statusCode,
			&dwordLength,
			WINHTTP_NO_HEADER_INDEX);
		lastError = GetLastError();
		CHECK_ERROR(httpResult == TRUE, L"WinHttpQueryHeaders failed to retrieve status code.");
		CHECK_ERROR(statusCode == 200, L"/Connect did not return status code: 200.");
	}
	{
		DWORD headerLength = 0;
		httpResult = WinHttpQueryHeaders(
			httpRequest,
			WINHTTP_QUERY_CONTENT_TYPE,
			WINHTTP_HEADER_NAME_BY_INDEX,
			NULL,
			&headerLength,
			WINHTTP_NO_HEADER_INDEX);
		lastError = GetLastError();
		CHECK_ERROR(httpResult == FALSE && lastError == ERROR_INSUFFICIENT_BUFFER, L"WinHttpQueryHeaders failed to retrieve content type.");

		Array<wchar_t> headerBuffer(headerLength + 1);
		ZeroMemory(&headerBuffer[0], headerBuffer.Count() * sizeof(wchar_t));

		httpResult = WinHttpQueryHeaders(
			httpRequest,
			WINHTTP_QUERY_CONTENT_TYPE,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&headerBuffer[0],
			&headerLength,
			WINHTTP_NO_HEADER_INDEX);
		lastError = GetLastError();
		CHECK_ERROR(httpResult == TRUE, L"WinHttpQueryHeaders failed to retrieve content-type.");

		const wchar_t* header = &headerBuffer[0];
		CHECK_ERROR(wcscmp(header, L"application/json; charset=utf8") == 0, L"/Content did not return content type: application/json; charset=utf8.");
	}
	{
		httpResult = WinHttpQueryDataAvailable(
			httpRequest,
			&dataLength);
		lastError = GetLastError();
		CHECK_ERROR(httpResult == TRUE, L"WinHttpQueryDataAvailable failed.");
	}
	{
		Array<char8_t> bodyBuffer(dataLength + 1);
		ZeroMemory(&bodyBuffer[0], bodyBuffer.Count() * sizeof(char8_t));

		ResetEvent(hEventWaitForServer);
		httpResult = WinHttpReadData(
			httpRequest,
			&bodyBuffer[0],
			dataLength,
			NULL);
		lastError = GetLastError();
		CHECK_ERROR(httpResult == TRUE, L"WinHttpReadData failed.");
		WaitForSingleObject(hEventWaitForServer, INFINITE);
		CHECK_ERROR(dwInternetStatus_WaitForServer == WINHTTP_CALLBACK_STATUS_READ_COMPLETE, L"WinHttpReadData failed to complete.");
		CHECK_ERROR(dwStatusInformationLength_WaitForServer == dataLength, L"WinHttpReadData failed to read full data.");

		U8String bodyUtf8 = U8String::Unmanaged(&bodyBuffer[0]);
		vint separatorIndex = bodyUtf8.IndexOf(L';');
		CHECK_ERROR(separatorIndex != -1, L"/Connect response body is not in the correct format: requestUrl;responseUrl.");
		urlRequest = baseUrl + u8tow(bodyUtf8.Left(separatorIndex));
		urlResponse = baseUrl + u8tow(bodyUtf8.Right(bodyUtf8.Length() - separatorIndex - 1));
	}
	WinHttpCloseHandle(httpRequest);
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

void HttpClient::SendString(const WString& str)
{
	if (state == State::Stopping) return;
	CHECK_ERROR(state == State::Running, L"SendString can only be called when client is running.");
	DWORD lastError = 0;
	BOOL httpResult = FALSE;

	HINTERNET httpRequest = WinHttpOpenRequest(
		httpConnection,
		L"POST",
		urlResponse.Buffer(),
		NULL,
		WINHTTP_NO_REFERER,
		NULL,
		WINHTTP_FLAG_REFRESH);
	lastError = GetLastError();
	if (lastError == ERROR_INVALID_HANDLE)
	{
		CHECK_ERROR(state == State::Stopping, L"WinHttpOpenRequest failed with ERROR_INVALID_HANDLE.");
		return;
	}
	CHECK_ERROR(httpRequest != NULL, L"WinHttpOpenRequest failed.");

	WINHTTP_STATUS_CALLBACK previousCallback = WinHttpSetStatusCallback(
		httpRequest,
		(WINHTTP_STATUS_CALLBACK)[](HINTERNET httpRequest, DWORD_PTR dwContext, DWORD dwInternetStatus, LPVOID lpvStatusInformation, DWORD dwStatusInformationLength) -> void
		{
			if (!dwContext) return;
			auto self = reinterpret_cast<HttpClient*>(dwContext);
			switch (dwInternetStatus)
			{
			case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
				{
					SPIN_LOCK(self->httpRequestBodiesLock)
					{
						self->httpRequestBodies.Remove(httpRequest);
					}
					self->QueueCallback([=]()
					{
						if (self->state == State::Stopping) return;
						DWORD lastError = 0;
						BOOL httpResult = WinHttpReceiveResponse(httpRequest, NULL);
						lastError = GetLastError();
						if (lastError == ERROR_INVALID_HANDLE)
						{
							CHECK_ERROR(self->state == State::Stopping, L"WinHttpReceiveResponse failed with ERROR_INVALID_HANDLE but client is not stopping.");
							return;
						}
						CHECK_ERROR(httpResult == TRUE, L"WinHttpReceiveResponse failed.");
					});
				}
				break;
			case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
				{
					self->QueueCallback([=]()
					{
						if (self->state == State::Stopping) return;
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

						self->CloseRequest(httpRequest);
						if (statusCode != 200)
						{
							self->RaiseErrorUnsafe(WString::Unmanaged(L"/Response returned status code: ") + itow(statusCode) + L", another renderer may have connected to the core.");
						}
					});
				}
				break;
			case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
				{
					SPIN_LOCK(self->httpRequestBodiesLock)
					{
						self->httpRequestBodies.Remove(httpRequest);
					}
					self->QueueCallback([=]()
					{
						if (self->state == State::Stopping) return;
						self->CloseRequest(httpRequest);
						self->RaiseErrorUnsafe(WString::Unmanaged(L"/Response canceled, another renderer may have connected to the core."));
					});
				}
				break;
			case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
				self->OnRequestHandleClosing(httpRequest);
				break;
			}
		},
		WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
		NULL);
	lastError = GetLastError();
	CHECK_ERROR(previousCallback != WINHTTP_INVALID_STATUS_CALLBACK, L"WinHttpSetStatusCallback failed.");

	httpResult = WinHttpAddRequestHeaders(
		httpRequest,
		L"Content-Type: application/json; charset=utf8",
		-1,
		WINHTTP_ADDREQ_FLAG_ADD);
	lastError = GetLastError();
	if (lastError == ERROR_INVALID_HANDLE)
	{
		CHECK_ERROR(state == State::Stopping, L"WinHttpAddRequestHeaders failed with ERROR_INVALID_HANDLE.");
		WinHttpCloseHandle(httpRequest);
		return;
	}
	CHECK_ERROR(httpResult == TRUE, L"WinHttpAddRequestHeaders failed.");

	U8String bodyUtf8 = wtou8(str);
	SPIN_LOCK(httpRequestBodiesLock)
	{
		httpRequestBodies.Add(httpRequest, bodyUtf8);
	}

	{
		AttachRequest(httpRequest);
		httpResult = WinHttpSendRequest(
			httpRequest,
			WINHTTP_NO_ADDITIONAL_HEADERS,
			0,
			(LPVOID)bodyUtf8.Buffer(),
			(DWORD)bodyUtf8.Length(),
			(DWORD)bodyUtf8.Length(),
			reinterpret_cast<DWORD_PTR>(this));
		lastError = GetLastError();
		if (lastError == ERROR_INVALID_HANDLE)
		{
			SPIN_LOCK(httpRequestBodiesLock)
			{
				httpRequestBodies.Remove(httpRequest);
			}
			OnRequestHandleClosing(httpRequest);
			CHECK_ERROR(state == State::Stopping, L"WinHttpSendRequest failed with ERROR_INVALID_HANDLE.");
			WinHttpCloseHandle(httpRequest);
			return;
		}
		if (httpResult == FALSE)
		{
			SPIN_LOCK(httpRequestBodiesLock)
			{
				httpRequestBodies.Remove(httpRequest);
			}
			OnRequestHandleClosing(httpRequest);
			WinHttpCloseHandle(httpRequest);
			CHECK_FAIL(L"WinHttpSendRequest failed.");
		}
	}
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

void HttpClient::QueueCallback(const Func<void()>& proc)
{
	BeginPendingCallback();
	auto queued = ThreadPoolLite::Queue([=]()
	{
		try
		{
			proc();
		}
		catch (...)
		{
			EndPendingCallback();
			throw;
		}

		EndPendingCallback();
	});
	if (!queued)
	{
		EndPendingCallback();
		CHECK_FAIL(L"HttpClient failed to queue asynchronous callback.");
	}
}

void HttpClient::AttachRequest(HINTERNET httpRequest)
{
	BeginPendingCallback();
	SPIN_LOCK(httpActiveRequestsLock)
	{
		httpActiveRequests.Add(httpRequest);
	}
}

void HttpClient::CloseRequest(HINTERNET httpRequest)
{
	bool closeRequest = false;
	SPIN_LOCK(httpActiveRequestsLock)
	{
		vint index = httpActiveRequests.IndexOf(httpRequest);
		if (index != -1)
		{
			httpActiveRequests.RemoveAt(index);
			closeRequest = true;
		}
	}
	if (closeRequest)
	{
		WinHttpCloseHandle(httpRequest);
	}
}

void HttpClient::OnRequestHandleClosing(HINTERNET httpRequest)
{
	SPIN_LOCK(httpActiveRequestsLock)
	{
		vint index = httpActiveRequests.IndexOf(httpRequest);
		if (index != -1)
		{
			httpActiveRequests.RemoveAt(index);
		}
	}
	EndPendingCallback();
}

HttpClient::HttpClient(const WString _baseUrl, vint port)
	: baseUrl(_baseUrl)
{
	DWORD lastError = 0;
	hEventWaitForServer = CreateEvent(NULL, FALSE, TRUE, NULL);
	CHECK_ERROR(hEventWaitForServer != NULL, L"HttpClient initialization failed on CreateEvent(hEventWaitForServer).");
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
	CloseHandle(hEventWaitForServer);
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
		state = State::Stopping;

		List<HINTERNET> stoppingRequests;
		SPIN_LOCK(httpActiveRequestsLock)
		{
			for (auto httpRequest : httpActiveRequests)
			{
				stoppingRequests.Add(httpRequest);
			}
			httpActiveRequests.Clear();
		}
		for (auto httpRequest : stoppingRequests)
		{
			WinHttpCloseHandle(httpRequest);
		}

		WinHttpCloseHandle(httpConnection);
		WinHttpSetStatusCallback(
			httpSession,
			NULL,
			WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
			NULL);
		WinHttpCloseHandle(httpSession);

		eventPendingCallbacks.Wait();

		httpConnection = NULL;
		httpSession = NULL;

		if (callback)
		{
			callback->OnDisconnected();
		}
	}
}

}
