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
			server->GetHttpRequestQueue(),
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
		ULONG result = HttpServerApi::SendResponse(server->GetHttpRequestQueue(), httpPendingRequestId, { 200, L"OK", pendingRequest, L"application/json; charset=utf8" });
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
	CHECK_ERROR(bodyLength > 0, L"/Response must contain body data.");

	Array<char8_t> bodyBuffer(bodyLength + 1);
	ZeroMemory(&bodyBuffer[0], bodyBuffer.Count() * sizeof(char8_t));
	ULONG bodyWritten = 0;
	for (USHORT i = 0; i < pRequest->EntityChunkCount; i++)
	{
		auto& chunk = pRequest->pEntityChunks[i];
		CHECK_ERROR(chunk.DataChunkType == HttpDataChunkFromMemory, L"/Response contains an unsupported body chunk.");
		auto chunkLength = chunk.FromMemory.BufferLength;
		CHECK_ERROR(bodyWritten + chunkLength <= bodyLength, L"/Response body is longer than Content-Length.");
		memcpy(&bodyBuffer[bodyWritten], chunk.FromMemory.pBuffer, chunkLength);
		bodyWritten += chunkLength;
	}
	if (pRequest->Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS)
	{
		ULONG result = NO_ERROR;
		result = HttpReceiveRequestEntityBody(
			server->GetHttpRequestQueue(),
			pRequest->RequestId,
			HTTP_RECEIVE_REQUEST_ENTITY_BODY_FLAG_FILL_BUFFER,
			&bodyBuffer[bodyWritten],
			bodyLength - bodyWritten,
			&bodyReceived,
			NULL);
		CHECK_ERROR(result == NO_ERROR || result == ERROR_HANDLE_EOF, L"HttpReceiveRequestEntityBody.");
		bodyWritten += bodyReceived;
	}
	CHECK_ERROR(bodyWritten == bodyLength, L"/Response body is shorter than Content-Length.");

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
			ULONG result = HttpServerApi::SendResponse(server->GetHttpRequestQueue(), httpPendingRequestId, { 200, L"OK", str, L"application/json; charset=utf8" });
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
HttpServer (HttpServerApi)
***********************************************************************/

void HttpServer::OnHttpRequestReceived(PHTTP_REQUEST pRequest)
{
	bool isValidRequest = wcsncmp(pRequest->CookedUrl.pAbsPath, urlRequestPrefix.Buffer(), urlRequestPrefix.Length()) == 0;
	bool isValidResponse = wcsncmp(pRequest->CookedUrl.pAbsPath, urlResponsePrefix.Buffer(), urlResponsePrefix.Length()) == 0;

	auto FindExistingConnection = [=, this](const WString& guid)->Ptr<HttpServerConnection>
	{
		SPIN_LOCK(lockConnections)
		{
			vint index = connections.Keys().IndexOf(guid);
			if (index == -1)
			{
				HttpServerApi::SendResponse(GetHttpRequestQueue(), pRequest->RequestId, { 404, L"Unknown connection guid" });
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
			HttpServerApi::SendResponse(GetHttpRequestQueue(), pRequest->RequestId, { 404, L"Connection rejected" });
		}
		else
		{
			auto completeUrlRequest = WString::Unmanaged(HttpServerUrl_Request) + L"/" + newGuid;
			auto completeUrlResponse = WString::Unmanaged(HttpServerUrl_Response) + L"/" + newGuid;
			auto result = HttpServerApi::SendResponse(GetHttpRequestQueue(), pRequest->RequestId, { 200, L"OK", completeUrlRequest + L";" + completeUrlResponse, L"application/json; charset=utf8" });
			CHECK_ERROR(result == NO_ERROR, L"HttpSendHttpResponse failed for responding /Connect.");
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
			auto result = HttpServerApi::SendResponse(GetHttpRequestQueue(), pRequest->RequestId, { 200, L"OK", responseToClient, L"application/json; charset=utf8" });
			CHECK_ERROR(
				result == NO_ERROR || result == ERROR_CONNECTION_INVALID || result == ERROR_OPERATION_ABORTED,
				L"HttpSendHttpResponse failed for responding /Response."
				);
		}
	}
	else
	{
		HttpServerApi::SendResponse(GetHttpRequestQueue(), pRequest->RequestId, { 404, L"Unknown URL" });
	}
}

void HttpServer::OnHttpServerStopping()
{
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
}

/***********************************************************************
HttpServer
***********************************************************************/

HttpServer::HttpServer(const WString _baseUrl, vint port)
	: HttpServerApi(WString::Unmanaged(L"http://localhost:") + itow(port) + _baseUrl + L"/", true)
	, baseUrl(_baseUrl)
{
	urlConnect = baseUrl + HttpServerUrl_Connect;
	urlRequestPrefix = baseUrl + HttpServerUrl_Request + L"/";
	urlResponsePrefix = baseUrl + HttpServerUrl_Response + L"/";
}

HttpServer::~HttpServer()
{
	Stop();
}

WaitForClientResult HttpServer::OnClientConnected(INetworkProtocolConnection* connection)
{
	return WaitForClientResult::Accept;
}

void HttpServer::Start()
{
	HttpServerApi::Start();
}

void HttpServer::Stop()
{
	HttpServerApi::Stop();
}

bool HttpServer::IsStopped()
{
	return HttpServerApi::IsStopped();
}

}
