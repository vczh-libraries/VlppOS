#include "NamedPipe.Windows.h"

namespace vl::inter_process
{

using namespace vl::console;
using namespace vl::collections;

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

	vint32_t count = 0;
	consumed = streamReadFile.Read(&count, sizeof(count));
	CHECK_ERROR(consumed == sizeof(count), L"ReadFile failed on incomplete message.");

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
			OnDisconnected();
			return;
		}
		CHECK_ERROR(error == ERROR_MORE_DATA || error == ERROR_IO_PENDING, L"ReadFile failed on unexpected GetLastError.");

		RegisterWaitForSingleObject(
			&hWaitHandleReadFile,
			hEventReadFile,
			[](PVOID lpParameter, BOOLEAN TimerOrWaitFired)
			{
				auto self = (NamedPipeConnection*)lpParameter;
				UnregisterWait(self->hWaitHandleReadFile);
				self->hWaitHandleReadFile = INVALID_HANDLE_VALUE;

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
						self->OnDisconnected();
						return;
					}
					CHECK_ERROR(error == ERROR_MORE_DATA, L"GetOverlappedResult(ReadFile) failed on unexpected GetLastError.");
					self->SubmitReadBufferUnsafe((vint)read);
				}
				self->BeginReadingLoopUnsafe();
			},
			this,
			INFINITE,
			WT_EXECUTEONLYONCE);
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

		WaitForSingleObject(hEventWriteFile, INFINITE);
		ResetEvent(hEventWriteFile);
		ZeroMemory(&overlappedWriteFile, sizeof(overlappedWriteFile));
		overlappedWriteFile.hEvent = hEventWriteFile;
		WriteFile(hPipe, buffer, (DWORD)bytesToSend, NULL, &overlappedWriteFile);
	}
}

void NamedPipeConnection::SendString(const WString& str)
{
	vint32_t bytes = 0;
	BeginSendStream();
	bytes += WriteStringToStream(str);
	EndSendStream(bytes);
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
	if (server)
	{
		SPIN_LOCK(server->lockConnections)
		{
			server->connections.Remove(this);
		}
	}
}

NamedPipeConnection::NamedPipeConnection(HANDLE _hPipe)
{
	hPipe = _hPipe;

	hEventReadFile = CreateEvent(NULL, TRUE, TRUE, NULL);
	CHECK_ERROR(hEventReadFile != NULL, L"NamedPipeConnection initialization failed on CreateEvent(hEventReadFile).");

	hEventWriteFile = CreateEvent(NULL, TRUE, TRUE, NULL);
	CHECK_ERROR(hEventWriteFile != NULL, L"NamedPipeConnection initialization failed on CreateEvent(hEventWriteFile).");
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
}

void NamedPipeConnection::Stop()
{
	if (hPipe != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hPipe);
		hPipe = INVALID_HANDLE_VALUE;
	}
}

/***********************************************************************
NamedPipeServer
***********************************************************************/

HANDLE NamedPipeServer::ServerCreatePipe(const WString& pipeName)
{
	HANDLE hPipe = CreateNamedPipe(
		pipeName.Buffer(),
		PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_REJECT_REMOTE_CLIENTS,
		1,
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

INetworkProtocolConnection* NamedPipeServer::WaitForClient()
{
	SPIN_LOCK(lockConnections)
	{
		CHECK_ERROR(!stopped, L"NamedPipeServer has stopped.");
	}

	auto connection = Ptr(new NamedPipeConnection(ServerCreatePipe(pipeName)));
	{
		OVERLAPPED overlapped;
		ZeroMemory(&overlapped, sizeof(overlapped));
		overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		CHECK_ERROR(overlapped.hEvent != NULL, L"ConnectNamedPipe failed on CreateEvent.");

		BOOL result = ConnectNamedPipe(connection->hPipe, &overlapped);
		CHECK_ERROR(result == FALSE, L"ConnectNamedPipe failed.");
		DWORD error = GetLastError();
		switch (error)
		{
		case ERROR_IO_PENDING:
			WaitForSingleObject(overlapped.hEvent, INFINITE);
			break;
		default:
			CHECK_ERROR(error == ERROR_PIPE_CONNECTED, L"ConnectNamedPipe failed on unexpected GetLastError.");
		}

		CloseHandle(overlapped.hEvent);
	}

	SPIN_LOCK(lockConnections)
	{
		if (!stopped)
		{
			connection->server = this;
			connections.Add(connection);
			return connection.Obj();
		}
	}
	CHECK_FAIL(L"NamedPipeServer has stopped.");
}

void NamedPipeServer::Stop()
{
	SPIN_LOCK(lockConnections)
	{
		stopped = true;
		for (auto connection : connections)
		{
			connection->server = nullptr;
		}
	}

	for (auto connection : connections)
	{
		connection->Stop();
	}
	connections.Clear();
}

/***********************************************************************
NamedPipeClient
***********************************************************************/

HANDLE NamedPipeClient::ClientCreatePipe(const WString& pipeName)
{
	HANDLE hPipe = CreateFile(
		pipeName.Buffer(),
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		NULL);
	CHECK_ERROR(hPipe != INVALID_HANDLE_VALUE, L"CreateFile failed.");
	CHECK_ERROR(GetLastError() == 0, L"Another renderer already connected.");
	return hPipe;
}

NamedPipeClient::NamedPipeClient(const WString& _pipeName)
	: NamedPipeConnection(ClientCreatePipe(_pipeName))
{
}

NamedPipeClient::~NamedPipeClient()
{
}

void NamedPipeClient::WaitForServer()
{
	DWORD dwPipeMode = PIPE_READMODE_MESSAGE;
	BOOL bSucceeded = SetNamedPipeHandleState(
		hPipe,
		&dwPipeMode,
		NULL,
		NULL);
	CHECK_ERROR(bSucceeded, L"SetNamedPipeHandleState failed.");
	if (callback)
	{
		callback->OnConnected();
	}
}

}