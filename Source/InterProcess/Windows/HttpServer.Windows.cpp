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

void HttpServerConnection::ArmPollAcknowledgementTimeoutUnsafe()
{
	pollAcknowledgementPending = true;
	pollAcknowledgementTimeout->Arm(
		HttpNetworkProtocolPollAcknowledgementTimeout,
		Func<void()>([this]() { OnPollAcknowledgementTimeout(); })
		);
}

bool HttpServerConnection::SendPendingRequestUnsafe(const WString& str)
{
	auto result = HttpServerApi::SendResponse(
		server->GetHttpRequestQueue(),
		httpPendingRequestId,
		{ 200, L"OK", str, HttpNetworkProtocolContentType }
		);
	httpPendingRequestId = HTTP_NULL_ID;
	if (result == NO_ERROR)
	{
		ArmPollAcknowledgementTimeoutUnsafe();
		return false;
	}
	if (result == ERROR_CONNECTION_INVALID || result == ERROR_OPERATION_ABORTED)
	{
		pendingRequestsToSend.Add(str);
		return true;
	}
	CHECK_FAIL(L"HttpSendHttpResponse failed for responding /Request.");
	return false;
}

void HttpServerConnection::ReportPollingError(const WString& error)
{
	if (!callback) return;
	try
	{
		if (callback->OnLocalError(error, false))
		{
			Stop();
		}
	}
	catch (...)
	{
		Stop();
		throw;
	}
}

void HttpServerConnection::OnPollAcknowledgementTimeout()
{
	auto holding = RetainFromServer();
	if (!holding) return;
	bool report = false;
	SPIN_LOCK(pendingRequestLock)
	{
		if (pollAcknowledgementPending && !stopped)
		{
			pollAcknowledgementPending = false;
			pollAcknowledgementReporting = true;
			report = true;
		}
	}
	if (!report) return;
	try
	{
		ReportPollingError(L"HttpServerConnection did not receive the replacement /Request after delivering a server message.");
	}
	catch (...)
	{
		SPIN_LOCK(pendingRequestLock)
		{
			pollAcknowledgementReporting = false;
		}
		throw;
	}
	SPIN_LOCK(pendingRequestLock)
	{
		pollAcknowledgementReporting = false;
	}
}

Ptr<HttpServerConnection> HttpServerConnection::RetainFromServer()
{
	HttpServer* currentServer = nullptr;
	SPIN_LOCK(pendingRequestLock)
	{
		currentServer = server;
	}
	if (!currentServer) return {};

	SPIN_LOCK(currentServer->lockConnections)
	{
		auto index = currentServer->connections.Keys().IndexOf(guid);
		if (index != -1 && currentServer->connections.Values()[index].Obj() == this)
		{
			return currentServer->connections.Values()[index];
		}
	}
	return {};
}

bool HttpServerConnection::OnNewHttpRequestForPendingRequest(HTTP_REQUEST_ID httpRequestId)
{
	bool cancelAcknowledgement = false;
	SPIN_LOCK(pendingRequestLock)
	{
		if (stopped) return false;
		cancelAcknowledgement = pollAcknowledgementPending || pollAcknowledgementReporting;
		pollAcknowledgementPending = false;
	}
	if (cancelAcknowledgement)
	{
		pollAcknowledgementTimeout->CancelAndWait();
	}

	bool reportLostPoll = false;
	SPIN_LOCK(pendingRequestLock)
	{
		if (stopped) return false;
		OnCancelCurrentHttpRequestForPendingRequest();
		httpPendingRequestId = httpRequestId;
		if (pendingRequestsToSend.Count() > 0)
		{
			auto pendingRequest = pendingRequestsToSend[0];
			pendingRequestsToSend.RemoveAt(0);
			reportLostPoll = SendPendingRequestUnsafe(pendingRequest);
		}
	}
	if (reportLostPoll)
	{
		ReportPollingError(L"HttpServerConnection failed to respond to /Request because the polling connection was lost.");
	}
	return true;
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
	bool reportLostPoll = false;
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
		// Excess callback responses must also satisfy a poll that is already waiting.
		if (httpPendingRequestId != HTTP_NULL_ID && pendingRequestsToSend.Count() > 0)
		{
			auto pendingRequest = pendingRequestsToSend[0];
			pendingRequestsToSend.RemoveAt(0);
			reportLostPoll = SendPendingRequestUnsafe(pendingRequest);
		}
	}
	if (reportLostPoll)
	{
		ReportPollingError(L"HttpServerConnection failed to respond to /Request because the polling connection was lost.");
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
	bool reportLostPoll = false;
	SPIN_LOCK(pendingRequestLock)
	{
		CHECK_ERROR(!stopped, L"HttpServerConnection::SendString cannot send on a stopped connection.");
		if (submittingResponse)
		{
			responsesToSubmit.Add(str);
		}
		else if (httpPendingRequestId != HTTP_NULL_ID)
		{
			reportLostPoll = SendPendingRequestUnsafe(str);
		}
		else
		{
			pendingRequestsToSend.Add(str);
		}
	}
	if (reportLostPoll)
	{
		ReportPollingError(L"HttpServerConnection failed to respond to /Request because the polling connection was lost.");
	}
}

void HttpServerConnection::Stop()
{
	auto holding = RetainFromServer();
	HttpServer* stoppingServer = nullptr;
	bool first = false;
	SPIN_LOCK(pendingRequestLock)
	{
		if (!stopped)
		{
			first = true;
			stopped = true;
			pollAcknowledgementPending = false;
			OnCancelCurrentHttpRequestForPendingRequest();
			stoppingServer = server;
			server = nullptr;
		}
	}
	if (!first) return;
	pollAcknowledgementTimeout->CancelAndWait();
	if (stoppingServer)
	{
		SPIN_LOCK(stoppingServer->lockConnections)
		{
			stoppingServer->connections.Remove(guid);
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
		auto result = OnClientConnected(connection);
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
			if (!connection->OnNewHttpRequestForPendingRequest(pRequest->RequestId))
			{
				HttpServerApi::SendResponse(GetHttpRequestQueue(), pRequest->RequestId, { 404, L"Connection stopped" });
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
	}
	for (auto connection : stoppingConnections)
	{
		connection->Stop();
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

WaitForClientResult HttpServer::OnClientConnected(Ptr<INetworkProtocolConnection> connection)
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
