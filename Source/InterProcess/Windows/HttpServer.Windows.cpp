#include "HttpServer.Windows.h"

namespace vl::inter_process::windows_http
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
		HttpServerApi::SendResponseUtf8(server->GetHttpRequestQueue(), httpPendingRequestId, pendingRequest);
		httpPendingRequestId = HTTP_NULL_ID;
	}
}

WString HttpServerConnection::SubmitResponse(PHTTP_REQUEST pRequest)
{
	auto body = server->GetUtf8Body(pRequest).Value();

	SPIN_LOCK(pendingRequestLock)
	{
		submittingResponse = true;
	}

	try
	{
		SPIN_LOCK(lockQueuedStrings)
		{
			if (callback)
			{
				callback->OnReadString(body);
			}
			else
			{
				queuedStrings.Add(body);
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
	CHECK_ERROR(!callback || !_callback, L"HttpServerConnection::InstallCallback only accepts one callback at a time.");
	if (_callback)
	{
		_callback->OnInstalled(this);
	}

	List<WString> strings;
	SPIN_LOCK(lockQueuedStrings)
	{
		callback = _callback;
		if (!callback) return;
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
			ULONG result = HttpServerApi::SendResponse(server->GetHttpRequestQueue(), httpPendingRequestId, { 200, L"OK", str, HttpNetworkProtocolContentType });
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
	Ptr<HttpServerConnection> holding;
	if (server)
	{
		SPIN_LOCK(server->lockConnections)
		{
			auto index = server->connections.Keys().IndexOf(guid);
			if (index != -1)
			{
				holding = server->connections.Values()[index];
				server->connections.Remove(guid);
			}
		}
		if (!holding) return;
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
			HttpServerApi::SendResponseUtf8(GetHttpRequestQueue(), pRequest->RequestId, CreateHttpNetworkProtocolConnectBody(completeUrlRequest, completeUrlResponse));
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
			auto result = HttpServerApi::SendResponse(GetHttpRequestQueue(), pRequest->RequestId, { 200, L"OK", responseToClient, HttpNetworkProtocolContentType });
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
