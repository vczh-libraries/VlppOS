#include "NamedPipe.Windows.h"

namespace vl::inter_process
{

using namespace vl::console;
using namespace vl::collections;

class NamedPipeConnection::ReadWaitContext
{
public:
	NamedPipeConnection*							connection = nullptr;
	HANDLE											hWaitHandle = INVALID_HANDLE_VALUE;
	atomic_vint										callbackStarted = 0;
};

class NamedPipeServer::PendingConnection::ConnectWaitContext
{
public:
	Ptr<PendingConnection>							pendingConnection;
	HANDLE											hWaitHandle = INVALID_HANDLE_VALUE;
	atomic_vint										callbackStarted = 0;

	ConnectWaitContext(Ptr<PendingConnection> _pendingConnection)
		: pendingConnection(_pendingConnection)
	{
	}
};

/***********************************************************************
NamedPipeConnection (Reading)
***********************************************************************/

void NamedPipeConnection::BeginReadingUnsafe()
{
	if (firstRead)
	{
		streamReadFile.SeekFromBegin(0);
	}
}

void NamedPipeConnection::SubmitReadBufferUnsafe(vint bytes)
{
	streamReadFile.Write(&bufferReadFile[0], bytes);
}

void NamedPipeConnection::EndReadingUnsafe()
{
	vint32_t position = (vint32_t)streamReadFile.Position();
	streamReadFile.SeekFromBegin(0);
	CHECK_ERROR(position >= 2 * sizeof(vint32_t), L"ReadFile failed on incomplete message.");

	vint32_t bytes = 0;
	vint consumed = 0;
	consumed = streamReadFile.Read(&bytes, sizeof(bytes));
	CHECK_ERROR(consumed == sizeof(bytes), L"ReadFile failed on incomplete message.");

	if (bytes > position - (vint32_t)sizeof(bytes))
	{
		firstRead = false;
		streamReadFile.SeekFromBegin(position);
		return;
	}
	CHECK_ERROR(bytes == position - sizeof(bytes), L"ReadFile failed on corrupted message.");
	firstRead = true;

	Array<wchar_t> strBuffer;
	auto ReadSingleString = [&]()
	{
		vint32_t length = 0;
		consumed = streamReadFile.Read(&length, sizeof(length));
		CHECK_ERROR(consumed == sizeof(length) && streamReadFile.Position() <= position, L"ReadFile failed on incomplete message.");

		if (length == 0)
		{
			return WString::Empty;
		}
		else
		{
			strBuffer.Resize(length);
			consumed = streamReadFile.Read(&strBuffer[0], length * sizeof(wchar_t));
			CHECK_ERROR(consumed == length * sizeof(wchar_t) && streamReadFile.Position() <= position, L"ReadFile failed on incomplete message.");
			return WString::CopyFrom(&strBuffer[0], length);
		}
	};

	callback->OnReadString(ReadSingleString());

	CHECK_ERROR(streamReadFile.Position() == position, L"ReadFile failed on incomplete message.");
}

void NamedPipeConnection::BeginReadingLoopUnsafe()
{
	if (stopped) return;
RESTART_LOOP:
	{
		BeginReadingUnsafe();
		ResetEvent(hEventReadFile);
		ZeroMemory(&overlappedReadFile, sizeof(overlappedReadFile));
		overlappedReadFile.hEvent = hEventReadFile;
		DWORD read = 0;
		BOOL result = ReadFile(hPipe, &bufferReadFile[0], sizeof(BYTE) * MaxMessageSize, &read, &overlappedReadFile);

		if (result == TRUE)
		{
			SubmitReadBufferUnsafe((vint)read);
			EndReadingUnsafe();
			goto RESTART_LOOP;
		}

		DWORD error = GetLastError();
		if (error == ERROR_BROKEN_PIPE || error == ERROR_INVALID_HANDLE)
		{
			if (!stopped)
			{
				OnDisconnected();
			}
			return;
		}
		CHECK_ERROR(error == ERROR_MORE_DATA || error == ERROR_IO_PENDING, L"ReadFile failed on unexpected GetLastError.");

		auto context = new ReadWaitContext;
		context->connection = this;
		BeginPendingCallback();

		BOOL waitResult = FALSE;
		bool registered = false;
		{
			SPIN_LOCK(lockReadWait)
			{
				if (!stopped)
				{
					readWaitContext = context;
					waitResult = RegisterWaitForSingleObject(
						&context->hWaitHandle,
						hEventReadFile,
						[](PVOID lpParameter, BOOLEAN TimerOrWaitFired)
						{
							auto context = (ReadWaitContext*)lpParameter;
							context->callbackStarted = 1;

							auto self = context->connection;
							ReadWaitContext* expectedContext = context;
							bool ownsContext = self->readWaitContext.compare_exchange_strong(expectedContext, nullptr);

							auto finalize = [=]()
							{
								if (ownsContext)
								{
									UnregisterWait(context->hWaitHandle);
								}
								self->EndPendingCallback();
								if (ownsContext)
								{
									delete context;
								}
							};

							DWORD read = 0;
							BOOL result = GetOverlappedResult(self->hPipe, &self->overlappedReadFile, &read, FALSE);
							if (result == TRUE)
							{
								self->SubmitReadBufferUnsafe((vint)read);
								self->EndReadingUnsafe();
							}
							else
							{
								DWORD error = GetLastError();
								if (error == ERROR_BROKEN_PIPE || error == ERROR_INVALID_HANDLE)
								{
									if (!self->stopped)
									{
										self->OnDisconnected();
									}
									finalize();
									return;
								}
								CHECK_ERROR(error == ERROR_MORE_DATA, L"GetOverlappedResult(ReadFile) failed on unexpected GetLastError.");
								self->SubmitReadBufferUnsafe((vint)read);
							}
							if (!self->stopped)
							{
								self->BeginReadingLoopUnsafe();
							}
							finalize();
						},
						context,
						INFINITE,
						WT_EXECUTEONLYONCE);
					if (!waitResult)
					{
						readWaitContext = nullptr;
					}
					registered = true;
				}
			}
		}

		if (!registered)
		{
			EndPendingCallback();
			delete context;
			return;
		}
		if (!waitResult)
		{
			EndPendingCallback();
			delete context;
			CHECK_FAIL(L"RegisterWaitForSingleObject failed for ReadFile.");
		}
	}
}

/***********************************************************************
NamedPipeConnection (Writing)
***********************************************************************/

vint32_t NamedPipeConnection::WriteInt32ToStream(vint32_t number)
{
	return (vint32_t)streamWriteFile.Write(&number, sizeof(number));
}

vint32_t NamedPipeConnection::WriteStringToStream(const WString& str)
{
	vint32_t bytes = 0;
	vint32_t count = (vint32_t)str.Length();
	bytes += (vint32_t)streamWriteFile.Write(&count, sizeof(count));
	if (count > 0)
	{
		bytes += (vint32_t)streamWriteFile.Write((void*)str.Buffer(), sizeof(wchar_t) * str.Length());
	}
	return bytes;
}

void NamedPipeConnection::BeginSendStream()
{
	vint32_t bytes = 0;
	streamWriteFile.SeekFromBegin(0);
	streamWriteFile.Write(&bytes, sizeof(bytes));
}

void NamedPipeConnection::EndSendStream(vint32_t bytes)
{
	streamWriteFile.SeekFromBegin(0);
	WriteInt32ToStream(bytes);

	vint32_t length = bytes + sizeof(bytes);
	for (vint i = 0; i < (length + MaxMessageSize - 1) / MaxMessageSize; i++)
	{
		vint offset = i * MaxMessageSize;
		vint bytesToSend = length >= (i + 1) * MaxMessageSize ? MaxMessageSize : length % MaxMessageSize;
		auto buffer = (LPCVOID)((char*)streamWriteFile.GetInternalBuffer() + offset);

		ResetEvent(hEventWriteFile);
		ZeroMemory(&overlappedWriteFile, sizeof(overlappedWriteFile));
		overlappedWriteFile.hEvent = hEventWriteFile;
		DWORD written = 0;
		BOOL result = WriteFile(hPipe, buffer, (DWORD)bytesToSend, NULL, &overlappedWriteFile);
		if (result == FALSE)
		{
			auto error = GetLastError();
			if (error == ERROR_BROKEN_PIPE || error == ERROR_INVALID_HANDLE)
			{
				OnDisconnected();
				return;
			}
			CHECK_ERROR(error == ERROR_IO_PENDING, L"WriteFile failed on unexpected GetLastError.");
			WaitForSingleObject(hEventWriteFile, INFINITE);
			result = GetOverlappedResult(hPipe, &overlappedWriteFile, &written, FALSE);
			if (result == FALSE)
			{
				error = GetLastError();
				if (error == ERROR_BROKEN_PIPE || error == ERROR_INVALID_HANDLE || error == ERROR_OPERATION_ABORTED)
				{
					OnDisconnected();
					return;
				}
				CHECK_FAIL(L"GetOverlappedResult(WriteFile) failed on unexpected GetLastError.");
			}
		}
		else
		{
			written = (DWORD)bytesToSend;
		}
		CHECK_ERROR(written == (DWORD)bytesToSend, L"WriteFile failed to write all data.");
	}
}

void NamedPipeConnection::SendString(const WString& str)
{
	if (stopped) return;
	SPIN_LOCK(lockWrite)
	{
		vint32_t bytes = 0;
		BeginSendStream();
		bytes += WriteStringToStream(str);
		EndSendStream(bytes);
	}
}

/***********************************************************************
NamedPipeConnection
***********************************************************************/

void NamedPipeConnection::OnDisconnected()
{
	if (callback)
	{
		callback->OnDisconnected();
	}
	auto owningServer = server;
	if (owningServer && pendingCallbacks == 0)
	{
		SPIN_LOCK(owningServer->lockConnections)
		{
			if (server == owningServer)
			{
				owningServer->connections.Remove(this);
				server = nullptr;
			}
		}
	}
}

NamedPipeConnection::NamedPipeConnection(HANDLE _hPipe)
	: bufferReadFile(MaxMessageSize)
{
	hPipe = _hPipe;

	hEventReadFile = CreateEvent(NULL, TRUE, TRUE, NULL);
	CHECK_ERROR(hEventReadFile != NULL, L"NamedPipeConnection initialization failed on CreateEvent(hEventReadFile).");

	hEventWriteFile = CreateEvent(NULL, TRUE, TRUE, NULL);
	CHECK_ERROR(hEventWriteFile != NULL, L"NamedPipeConnection initialization failed on CreateEvent(hEventWriteFile).");

	CHECK_ERROR(eventPendingCallbacks.CreateManualUnsignal(true), L"NamedPipeConnection initialization failed on eventPendingCallbacks.CreateManualUnsignal.");
}

NamedPipeConnection::~NamedPipeConnection()
{
	Stop();
	CloseHandle(hEventReadFile);
	CloseHandle(hEventWriteFile);
}

void NamedPipeConnection::InstallCallback(INetworkProtocolCallback* _callback)
{
	callback = _callback;
	CHECK_ERROR(callback, L"NamedPipeConnection::InstallCallback needs a valid INetworkProtocolCallback.");
	callback->OnInstalled(this);
}

void NamedPipeConnection::Stop()
{
	stopped = 1;
	ReadWaitContext* context = nullptr;
	{
		SPIN_LOCK(lockReadWait)
		{
			context = readWaitContext.exchange(nullptr);
		}
	}
	if (context)
	{
		UnregisterWaitEx(context->hWaitHandle, INVALID_HANDLE_VALUE);
		if (context->callbackStarted == 0)
		{
			EndPendingCallback();
		}
		delete context;
	}
	eventPendingCallbacks.Wait();

	SPIN_LOCK(lockWrite)
	{
		if (hPipe != INVALID_HANDLE_VALUE)
		{
			CancelIoEx(hPipe, NULL);
			CloseHandle(hPipe);
			hPipe = INVALID_HANDLE_VALUE;
		}
	}
}

void NamedPipeConnection::BeginPendingCallback()
{
	if (pendingCallbacks++ == 0)
	{
		eventPendingCallbacks.Unsignal();
	}
}

void NamedPipeConnection::EndPendingCallback()
{
	if (--pendingCallbacks == 0)
	{
		eventPendingCallbacks.Signal();
	}
}

/***********************************************************************
NamedPipeServer
***********************************************************************/

NamedPipeServer::PendingConnection::PendingConnection(NamedPipeServer* _server, Ptr<NamedPipeConnection> _connection)
	: server(_server)
	, connection(_connection)
{
	ZeroMemory(&overlappedConnect, sizeof(overlappedConnect));
	hEventConnect = CreateEvent(NULL, TRUE, FALSE, NULL);
	CHECK_ERROR(hEventConnect != NULL, L"ConnectNamedPipe failed on CreateEvent.");
	overlappedConnect.hEvent = hEventConnect;
	CHECK_ERROR(eventPendingCallbacks.CreateManualUnsignal(true), L"ConnectNamedPipe failed on eventPendingCallbacks.CreateManualUnsignal.");
}

NamedPipeServer::PendingConnection::~PendingConnection()
{
	Stop();
	CloseHandle(hEventConnect);
}

void NamedPipeServer::PendingConnection::Stop()
{
	server = nullptr;
	ConnectWaitContext* context = nullptr;
	{
		SPIN_LOCK(lockConnectWait)
		{
			context = connectWaitContext.exchange(nullptr);
		}
	}
	if (context)
	{
		UnregisterWaitEx(context->hWaitHandle, INVALID_HANDLE_VALUE);
		if (context->callbackStarted == 0)
		{
			EndPendingCallback();
		}
		delete context;
	}
	eventPendingCallbacks.Wait();
	if (connection)
	{
		connection->Stop();
		connection = nullptr;
	}
}

void NamedPipeServer::PendingConnection::BeginPendingCallback()
{
	if (pendingCallbacks++ == 0)
	{
		eventPendingCallbacks.Unsignal();
	}
}

void NamedPipeServer::PendingConnection::EndPendingCallback()
{
	if (--pendingCallbacks == 0)
	{
		eventPendingCallbacks.Signal();
	}
}

HANDLE NamedPipeServer::ServerCreatePipe(const WString& pipeName)
{
	HANDLE hPipe = CreateNamedPipe(
		(L"\\\\.\\pipe\\" + pipeName).Buffer(),
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
		PIPE_UNLIMITED_INSTANCES,
		65536,
		65536,
		6000,
		NULL);
	CHECK_ERROR(hPipe != INVALID_HANDLE_VALUE, L"CreateNamedPipe failed.");
	return hPipe;
}

NamedPipeServer::NamedPipeServer(const WString& _pipeName)
	: pipeName(_pipeName)
{
}

NamedPipeServer::~NamedPipeServer()
{
	Stop();
}

void NamedPipeServer::BeginListening()
{
	Ptr<PendingConnection> pendingConnection;
	SPIN_LOCK(lockConnections)
	{
		if (!started || stopped)
		{
			return;
		}
	}

	auto connection = Ptr(new NamedPipeConnection(ServerCreatePipe(pipeName)));
	pendingConnection = Ptr(new PendingConnection(this, connection));

	BOOL result = ConnectNamedPipe(connection->hPipe, &pendingConnection->overlappedConnect);
	DWORD error = result ? ERROR_SUCCESS : GetLastError();
	switch (error)
	{
	case ERROR_SUCCESS:
	case ERROR_PIPE_CONNECTED:
		CompletePendingConnection(pendingConnection, true);
		break;
	case ERROR_IO_PENDING:
		SPIN_LOCK(lockConnections)
		{
			if (stopped)
			{
				pendingConnection->Stop();
				return;
			}
			pendingConnections.Add(pendingConnection);
			auto context = new PendingConnection::ConnectWaitContext(pendingConnection);
			pendingConnection->BeginPendingCallback();
			BOOL waitResult = FALSE;
			{
				SPIN_LOCK(pendingConnection->lockConnectWait)
				{
					pendingConnection->connectWaitContext = context;
					waitResult = RegisterWaitForSingleObject(
						&context->hWaitHandle,
						pendingConnection->hEventConnect,
						[](PVOID lpParameter, BOOLEAN TimerOrWaitFired)
						{
							auto context = (PendingConnection::ConnectWaitContext*)lpParameter;
							context->callbackStarted = 1;

							auto pendingConnection = context->pendingConnection;
							PendingConnection::ConnectWaitContext* expectedContext = context;
							bool ownsContext = pendingConnection->connectWaitContext.compare_exchange_strong(expectedContext, nullptr);

							auto finalize = [=]()
							{
								if (ownsContext)
								{
									UnregisterWait(context->hWaitHandle);
								}
								pendingConnection->EndPendingCallback();
								if (ownsContext)
								{
									delete context;
								}
							};

							DWORD transferred = 0;
							BOOL result = GetOverlappedResult(pendingConnection->connection->hPipe, &pendingConnection->overlappedConnect, &transferred, FALSE);
							if (result == TRUE)
							{
								if (pendingConnection->server)
								{
									pendingConnection->server->CompletePendingConnection(pendingConnection.Obj(), true);
								}
							}
							else
							{
								auto error = GetLastError();
								if (error == ERROR_OPERATION_ABORTED || error == ERROR_INVALID_HANDLE || error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA)
								{
									if (pendingConnection->server)
									{
										pendingConnection->server->CompletePendingConnection(pendingConnection.Obj(), false);
									}
									finalize();
									return;
								}
								CHECK_FAIL(L"GetOverlappedResult(ConnectNamedPipe) failed on unexpected GetLastError.");
							}
							finalize();
						},
						context,
						INFINITE,
						WT_EXECUTEONLYONCE);
					if (!waitResult)
					{
						pendingConnection->connectWaitContext = nullptr;
					}
				}
			}
			if (!waitResult)
			{
				pendingConnection->EndPendingCallback();
				delete context;
				CHECK_FAIL(L"RegisterWaitForSingleObject failed for ConnectNamedPipe.");
			}
		}
		break;
	default:
		CHECK_FAIL(L"ConnectNamedPipe failed on unexpected GetLastError.");
	}
}

void NamedPipeServer::CompletePendingConnection(Ptr<PendingConnection> pendingConnection, bool connected)
{
	auto connection = pendingConnection->connection;
	bool shouldListen = false;
	bool shouldNotify = false;

	{
		SPIN_LOCK(lockConnections)
		{
			pendingConnections.Remove(pendingConnection.Obj());
			if (started && !stopped)
			{
				shouldListen = true;
				if (connected)
				{
					connection->server = this;
					connections.Add(connection);
					shouldNotify = true;
					pendingConnection->connection = nullptr;
					pendingConnection->server = nullptr;
				}
			}
		}
	}

	if (shouldListen)
	{
		BeginListening();
	}

	if (shouldNotify)
	{
		auto result = OnClientConnected(connection.Obj());
		if (result == WaitForClientResult::Reject)
		{
			SPIN_LOCK(lockConnections)
			{
				connections.Remove(connection.Obj());
			}
			connection->server = nullptr;
			connection->Stop();
		}
	}
	else if (connection)
	{
		connection->Stop();
	}
}

void NamedPipeServer::CompletePendingConnection(PendingConnection* pendingConnection, bool connected)
{
	Ptr<PendingConnection> holding;
	{
		SPIN_LOCK(lockConnections)
		{
			for (vint i = 0; i < pendingConnections.Count(); i++)
			{
				if (pendingConnections[i].Obj() == pendingConnection)
				{
					holding = pendingConnections[i];
					break;
				}
			}
		}
	}
	if (holding)
	{
		CompletePendingConnection(holding, connected);
	}
}

WaitForClientResult NamedPipeServer::OnClientConnected(INetworkProtocolConnection* connection)
{
	return WaitForClientResult::Accept;
}

void NamedPipeServer::Start()
{
	{
		SPIN_LOCK(lockConnections)
		{
			CHECK_ERROR(!started, L"NamedPipeServer has already started.");
			CHECK_ERROR(!stopped, L"NamedPipeServer has stopped.");
			started = true;
		}
	}
	BeginListening();
}

void NamedPipeServer::Stop()
{
	List<Ptr<NamedPipeConnection>> stoppingConnections;
	List<Ptr<PendingConnection>> stoppingPendingConnections;
	SPIN_LOCK(lockConnections)
	{
		if (!stopped)
		{
			started = false;
			stopped = true;
			for (auto connection : connections)
			{
				connection->server = nullptr;
				stoppingConnections.Add(connection);
			}
			for (auto pendingConnection : pendingConnections)
			{
				stoppingPendingConnections.Add(pendingConnection);
			}
			connections.Clear();
			pendingConnections.Clear();
		}
	}

	for (auto pendingConnection : stoppingPendingConnections)
	{
		pendingConnection->Stop();
	}
	for (auto connection : stoppingConnections)
	{
		connection->Stop();
	}
}

bool NamedPipeServer::IsStopped()
{
	bool result = false;
	SPIN_LOCK(lockConnections)
	{
		result = stopped;
	}
	return result;
}

/***********************************************************************
NamedPipeClient
***********************************************************************/

HANDLE NamedPipeClient::ClientCreatePipe(const WString& pipeName)
{
	auto fullPipeName = L"\\\\.\\pipe\\" + pipeName;
	auto start = GetTickCount64();
	while (true)
	{
		HANDLE hPipe = CreateFile(
			fullPipeName.Buffer(),
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL);
		if (hPipe != INVALID_HANDLE_VALUE)
		{
			return hPipe;
		}

		auto error = GetLastError();
		CHECK_ERROR(error == ERROR_FILE_NOT_FOUND || error == ERROR_PIPE_BUSY, L"CreateFile failed.");
		CHECK_ERROR(GetTickCount64() - start < 6000, L"CreateFile failed because the pipe server did not become available.");
		if (error == ERROR_PIPE_BUSY)
		{
			WaitNamedPipe(fullPipeName.Buffer(), 100);
		}
		else
		{
			Thread::Sleep(1);
		}
	}
}

NamedPipeClient::NamedPipeClient(const WString& _pipeName)
	: NamedPipeConnection(ClientCreatePipe(_pipeName))
{
}

NamedPipeClient::~NamedPipeClient()
{
}

INetworkProtocolConnection* NamedPipeClient::GetConnection()
{
	return this;
}

void NamedPipeClient::WaitForServer()
{
	status = ClientStatus::WaitingForServer;
	DWORD dwPipeMode = PIPE_READMODE_MESSAGE;
	BOOL bSucceeded = SetNamedPipeHandleState(
		hPipe,
		&dwPipeMode,
		NULL,
		NULL);
	CHECK_ERROR(bSucceeded, L"SetNamedPipeHandleState failed.");
	status = ClientStatus::Connected;
	if (callback)
	{
		callback->OnConnected();
	}
}

ClientStatus NamedPipeClient::GetStatus()
{
	return status;
}

void NamedPipeClient::Stop()
{
	NamedPipeConnection::Stop();
	status = ClientStatus::Disconnected;
}

}
