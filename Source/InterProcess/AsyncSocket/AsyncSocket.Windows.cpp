#include "AsyncSocket.Windows.h"

#pragma comment(lib, "Ws2_32.lib")

namespace vl::inter_process::async_tcp_socket::windows_socket
{
	using namespace collections;

	class IocpOperation;
	class IocpRuntime;
	class ConnectionState;
	class AsyncSocketConnection;

	struct NativeOverlapped
	{
		OVERLAPPED							overlapped;
		IocpOperation*					operation = nullptr;
	};

	struct CallbackFrame
	{
		ConnectionState*					connection = nullptr;
		CallbackFrame*						previous = nullptr;
	};

	static thread_local IocpRuntime* currentCompletionRuntime = nullptr;
	static thread_local IocpRuntime* currentCallbackRuntime = nullptr;
	static thread_local CallbackFrame* currentCallbackFrame = nullptr;

	WString SocketErrorMessage(const wchar_t* operation, DWORD error)
	{
		return WString::Unmanaged(operation) + L" failed with Windows error " + itow((vint)error) + L".";
	}

/***********************************************************************
IocpOperation
***********************************************************************/

	class IocpOperation
	{
	public:
		NativeOverlapped					native;

		IocpOperation()
		{
			ZeroMemory(&native.overlapped, sizeof(native.overlapped));
			native.operation = this;
		}

		virtual ~IocpOperation() = default;
		virtual bool Complete(DWORD bytes, DWORD error) = 0;
		virtual void EndPending() = 0;
	};

/***********************************************************************
IocpRuntime
***********************************************************************/

	class IocpRuntime : public Object
	{
	private:
		class CompletionWorker : public Thread
		{
		private:
			IocpRuntime*						runtime = nullptr;
		protected:
			void Run() override
			{
				currentCompletionRuntime = runtime;
				while (true)
				{
					DWORD bytes = 0;
					ULONG_PTR key = 0;
					OVERLAPPED* overlapped = nullptr;
					auto succeeded = GetQueuedCompletionStatus(runtime->iocp, &bytes, &key, &overlapped, INFINITE);
					if (!overlapped)
					{
						break;
					}

					auto native = CONTAINING_RECORD(overlapped, NativeOverlapped, overlapped);
					auto operation = native->operation;
					auto error = succeeded ? ERROR_SUCCESS : GetLastError();
					bool completed = true;
					try
					{
						completed = operation->Complete(bytes, error);
					}
					catch (...)
					{
						completed = true;
					}
					if (completed)
					{
						operation->EndPending();
						delete operation;
					}
				}
				currentCompletionRuntime = nullptr;
			}

		public:
			CompletionWorker(IocpRuntime* _runtime)
				: runtime(_runtime)
			{
			}
		};

		class CallbackWorker : public Thread
		{
		private:
			IocpRuntime*						runtime = nullptr;
		protected:
			void Run() override
			{
				currentCallbackRuntime = runtime;
				runtime->callbackQueue.RunTaskQueue();
				currentCallbackRuntime = nullptr;
			}

		public:
			CallbackWorker(IocpRuntime* _runtime)
				: runtime(_runtime)
			{
			}
		};

		HANDLE								iocp = nullptr;
		TaskQueue						callbackQueue;
		CompletionWorker*				completionWorker = nullptr;
		CallbackWorker*					callbackWorker = nullptr;
		SpinLock							lockStop;
		bool							stopRequested = false;
		bool							finalizing = false;
		bool							finalized = false;
		EventObject						eventFinalized;
		bool							winsockStarted = false;

	public:
		IocpRuntime()
		{
			CHECK_ERROR(eventFinalized.CreateManualUnsignal(false), L"IAsyncSocket failed to create its runtime drain event.");
			bool completionStarted = false;
			bool callbackStarted = false;
			try
			{
				WSADATA data;
				auto error = WSAStartup(MAKEWORD(2, 2), &data);
				CHECK_ERROR(error == 0, L"IAsyncSocket failed to initialize Winsock.");
				winsockStarted = true;

				iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
				CHECK_ERROR(iocp != nullptr, L"IAsyncSocket failed to create an IO completion port.");

				completionWorker = new CompletionWorker(this);
				callbackWorker = new CallbackWorker(this);
				completionStarted = completionWorker->Start();
				CHECK_ERROR(completionStarted, L"IAsyncSocket failed to start its completion worker.");
				callbackStarted = callbackWorker->Start();
				CHECK_ERROR(callbackStarted, L"IAsyncSocket failed to start its callback worker.");
			}
			catch (...)
			{
				if (callbackStarted)
				{
					callbackQueue.QueueExitTask();
					callbackWorker->Wait();
				}
				if (completionStarted)
				{
					PostQueuedCompletionStatus(iocp, 0, 0, nullptr);
					completionWorker->Wait();
				}
				delete callbackWorker;
				delete completionWorker;
				if (iocp)
				{
					CloseHandle(iocp);
				}
				if (winsockStarted)
				{
					WSACleanup();
				}
				throw;
			}
		}

		~IocpRuntime()
		{
			Stop();
		}

		bool Associate(SOCKET socket)
		{
			return CreateIoCompletionPort((HANDLE)socket, iocp, 0, 0) == iocp;
		}

		void QueueCallback(Func<void()> callback)
		{
			callbackQueue.QueueTask(callback);
		}

		void Stop()
		{
			bool requestExit = false;
			bool finalizeHere = false;
			bool waitForFinalization = false;
			auto selfWorker = currentCallbackRuntime == this || currentCompletionRuntime == this;
			SPIN_LOCK(lockStop)
			{
				if (finalized)
				{
					return;
				}
				if (!stopRequested)
				{
					stopRequested = true;
					requestExit = true;
				}
				if (!selfWorker)
				{
					if (!finalizing)
					{
						finalizing = true;
						finalizeHere = true;
					}
					else
					{
						waitForFinalization = true;
					}
				}
			}

			if (requestExit)
			{
				callbackQueue.QueueExitTask();
				PostQueuedCompletionStatus(iocp, 0, 0, nullptr);
			}
			if (selfWorker)
			{
				return;
			}
			if (waitForFinalization)
			{
				eventFinalized.Wait();
				return;
			}
			if (!finalizeHere)
			{
				return;
			}

			if (callbackWorker)
			{
				callbackWorker->Wait();
			}
			if (completionWorker)
			{
				completionWorker->Wait();
			}

			delete callbackWorker;
			callbackWorker = nullptr;
			delete completionWorker;
			completionWorker = nullptr;
			if (iocp)
			{
				CloseHandle(iocp);
				iocp = nullptr;
			}
			if (winsockStarted)
			{
				WSACleanup();
				winsockStarted = false;
			}
			SPIN_LOCK(lockStop)
			{
				finalized = true;
			}
			eventFinalized.Signal();
		}
	};

/***********************************************************************
ReadBlock
***********************************************************************/

	class ReadBlock : public Object
	{
	public:
		Array<vuint8_t>						data;

		ReadBlock()
		{
			data.Resize(65536);
		}
	};

/***********************************************************************
ConnectionState
***********************************************************************/

	class ConnectionState : public Object
	{
		friend class AsyncSocketConnection;
	private:
		class ReadOperation;
		class WriteOperation;
		class ConnectOperation;

		IocpRuntime*						runtime = nullptr;
		AsyncSocketConnection*				owner = nullptr;

		// covers every field below, pending counts, and their events
		CriticalSection					lockState;
		SOCKET							socket = INVALID_SOCKET;
		IAsyncSocketCallback*				callback = nullptr;
		bool							connected = false;
		bool							stopping = false;
		bool							stopped = false;
		bool							reading = false;
		bool							readPending = false;
		bool							writePending = false;
		bool							terminalPending = false;
		bool							disconnectedNotified = false;
		vint							pendingIo = 0;
		vint							activeCallbacks = 0;
		EventObject						eventIoDrained;
		EventObject						eventCallbacksDrained;

		bool							clientMode = false;
		vint							clientPort = 0;
		ClientStatus						clientStatus = ClientStatus::Ready;
		EventObject						eventWaitForServer;
		PTP_TIMER						clientRetryTimer = nullptr;
		vint							clientAttempts = 0;
		vint							clientGeneration = 0;

		void BeginPendingLocked()
		{
			if (pendingIo++ == 0)
			{
				eventIoDrained.Unsignal();
			}
		}

		void EndPendingLocked()
		{
			if (--pendingIo == 0)
			{
				eventIoDrained.Signal();
			}
		}

		void EndPending()
		{
			CS_LOCK(lockState)
			{
				EndPendingLocked();
			}
		}

		IAsyncSocketCallback* BeginCallback(bool terminal)
		{
			IAsyncSocketCallback* result = nullptr;
			CS_LOCK(lockState)
			{
				if (callback && (terminal || (!stopping && !terminalPending)))
				{
					result = callback;
					if (activeCallbacks++ == 0)
					{
						eventCallbacksDrained.Unsignal();
					}
				}
			}
			return result;
		}

		void EndCallback()
		{
			CS_LOCK(lockState)
			{
				if (--activeCallbacks == 0)
				{
					eventCallbacksDrained.Signal();
				}
			}
		}

		template<typename TCallback>
		bool InvokeCallback(bool terminal, TCallback&& invoke)
		{
			auto installed = BeginCallback(terminal);
			if (!installed)
			{
				return false;
			}

			CallbackFrame frame;
			frame.connection = this;
			frame.previous = currentCallbackFrame;
			currentCallbackFrame = &frame;
			try
			{
				invoke(installed);
			}
			catch (...)
			{
			}
			currentCallbackFrame = frame.previous;
			EndCallback();
			return true;
		}

		bool IsCurrentCallback()
		{
			for (auto frame = currentCallbackFrame; frame; frame = frame->previous)
			{
				if (frame->connection == this)
				{
					return true;
				}
			}
			return false;
		}

		void PostRead(Ptr<ConnectionState> retainedState);
		Ptr<ConnectionState> Retain();
		void DeliverRead(Ptr<ConnectionState> retainedState, Ptr<ReadBlock> block, vint bytes);
		void DeliverWrite(Ptr<AsyncSocketBuffer> buffer);
		void QueueTerminal(DWORD error, bool reportError);
		void DeliverTerminal(DWORD error, bool reportError);
		void StartConnectAttempt();
		void CompleteConnect(SOCKET operationSocket, vint generation, DWORD error);
		void QueueConnectFailure(DWORD error);
		void DeliverConnectFailure(DWORD error, bool fatal);
		void DeliverConnected();
		void ScheduleRetry();
		static VOID CALLBACK RetryTimerCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER)
		{
			auto self = (ConnectionState*)context;
			self->StartConnectAttempt();
		}

	public:
		ConnectionState(IocpRuntime* _runtime, bool _clientMode, vint _clientPort)
			: runtime(_runtime)
			, clientMode(_clientMode)
			, clientPort(_clientPort)
		{
			CHECK_ERROR(eventIoDrained.CreateManualUnsignal(true), L"IAsyncSocket failed to create its I/O drain event.");
			CHECK_ERROR(eventCallbacksDrained.CreateManualUnsignal(true), L"IAsyncSocket failed to create its callback drain event.");
			CHECK_ERROR(eventWaitForServer.CreateManualUnsignal(false), L"IAsyncSocket failed to create its client wait event.");
			if (clientMode)
			{
				clientRetryTimer = CreateThreadpoolTimer(&RetryTimerCallback, this, nullptr);
				CHECK_ERROR(clientRetryTimer != nullptr, L"IAsyncSocket failed to create its retry timer.");
			}
		}

		ConnectionState(IocpRuntime* _runtime, SOCKET _socket)
			: runtime(_runtime)
			, socket(_socket)
			, connected(true)
		{
			CHECK_ERROR(eventIoDrained.CreateManualUnsignal(true), L"IAsyncSocket failed to create its I/O drain event.");
			CHECK_ERROR(eventCallbacksDrained.CreateManualUnsignal(true), L"IAsyncSocket failed to create its callback drain event.");
			CHECK_ERROR(eventWaitForServer.CreateManualUnsignal(false), L"IAsyncSocket failed to create its client wait event.");
		}

		~ConnectionState()
		{
			if (clientRetryTimer)
			{
				SetThreadpoolTimer(clientRetryTimer, nullptr, 0, 0);
				WaitForThreadpoolTimerCallbacks(clientRetryTimer, TRUE);
				CloseThreadpoolTimer(clientRetryTimer);
				clientRetryTimer = nullptr;
			}
		}

		void CloseRetryTimer()
		{
			PTP_TIMER timer = nullptr;
			CS_LOCK(lockState)
			{
				timer = clientRetryTimer;
				clientRetryTimer = nullptr;
			}
			if (timer)
			{
				SetThreadpoolTimer(timer, nullptr, 0, 0);
				WaitForThreadpoolTimerCallbacks(timer, TRUE);
				CloseThreadpoolTimer(timer);
			}
		}

		void InstallCallback(IAsyncSocketCallback* value);
		void BeginReading();
		void Write(Ptr<AsyncSocketBuffer> buffer);
		void Stop();
		void WaitForServer();
		ClientStatus GetStatus();
	};

/***********************************************************************
AsyncSocketConnection
***********************************************************************/

	class AsyncSocketConnection : public Object, public virtual IAsyncSocketConnection
	{
	private:
		Ptr<ConnectionState>					state;

	public:
		AsyncSocketConnection(Ptr<ConnectionState> _state)
			: state(_state)
		{
			state->owner = this;
		}

		~AsyncSocketConnection()
		{
			state->Stop();
			state->owner = nullptr;
		}

		Ptr<ConnectionState> GetState()
		{
			return state;
		}

		void InstallCallback(IAsyncSocketCallback* callback) override
		{
			state->InstallCallback(callback);
		}

		void BeginReadingLoopUnsafe() override
		{
			state->BeginReading();
		}

		void WriteAsync(Ptr<AsyncSocketBuffer> buffer) override
		{
			state->Write(buffer);
		}

		void Stop() override
		{
			state->Stop();
		}
	};

/***********************************************************************
ConnectionState::ReadOperation
***********************************************************************/

	class ConnectionState::ReadOperation : public IocpOperation
	{
	public:
		Ptr<ConnectionState>					connection;
		Ptr<ReadBlock>						block;

		ReadOperation(Ptr<ConnectionState> _connection)
			: connection(_connection)
			, block(Ptr(new ReadBlock))
		{
		}

		bool Complete(DWORD bytes, DWORD error) override
		{
			bool cancelled = false;
			CS_LOCK(connection->lockState)
			{
				connection->readPending = false;
				cancelled = connection->stopping;
			}
			if (cancelled)
			{
				return true;
			}
			if (error != ERROR_SUCCESS)
			{
				connection->QueueTerminal(error, true);
			}
			else if (bytes == 0)
			{
				connection->QueueTerminal(ERROR_SUCCESS, false);
			}
			else
			{
				auto state = connection;
				auto retainedBlock = block;
				connection->runtime->QueueCallback(Func<void()>([state, retainedBlock, bytes]()
				{
					state->DeliverRead(state, retainedBlock, (vint)bytes);
				}));
			}
			return true;
		}

		void EndPending() override
		{
			connection->EndPending();
		}
	};

/***********************************************************************
ConnectionState::WriteOperation
***********************************************************************/

	class ConnectionState::WriteOperation : public IocpOperation
	{
	public:
		Ptr<ConnectionState>					connection;
		Ptr<AsyncSocketBuffer>				buffer;
		vint								offset = 0;

		WriteOperation(Ptr<ConnectionState> _connection, Ptr<AsyncSocketBuffer> _buffer)
			: connection(_connection)
			, buffer(_buffer)
		{
		}

		DWORD PostLocked()
		{
			WSABUF nativeBuffer;
			nativeBuffer.buf = (CHAR*)&buffer->data[offset];
			nativeBuffer.len = (ULONG)(buffer->data.Count() - offset);
			DWORD sent = 0;
			auto result = WSASend(connection->socket, &nativeBuffer, 1, &sent, 0, &native.overlapped, nullptr);
			if (result == 0)
			{
				return ERROR_SUCCESS;
			}
			auto error = WSAGetLastError();
			return error == WSA_IO_PENDING ? ERROR_SUCCESS : (DWORD)error;
		}

		bool Complete(DWORD bytes, DWORD error) override
		{
			bool queueCompleted = false;
			DWORD terminalError = ERROR_SUCCESS;
			connection->lockState.Enter();
			if (connection->stopping)
			{
				connection->lockState.Leave();
				return true;
			}
			if (error != ERROR_SUCCESS || bytes == 0)
			{
				terminalError = error == ERROR_SUCCESS ? WSAECONNRESET : error;
				connection->lockState.Leave();
				connection->QueueTerminal(terminalError, true);
				return true;
			}

			offset += (vint)bytes;
			if (offset < buffer->data.Count())
			{
				ZeroMemory(&native.overlapped, sizeof(native.overlapped));
				auto postError = PostLocked();
				connection->lockState.Leave();
				if (postError == ERROR_SUCCESS)
				{
					return false;
				}
				connection->QueueTerminal(postError, true);
				return true;
			}
			queueCompleted = true;
			connection->lockState.Leave();

			if (queueCompleted)
			{
				auto state = connection;
				auto retainedBuffer = buffer;
				connection->runtime->QueueCallback(Func<void()>([state, retainedBuffer]()
				{
					state->DeliverWrite(retainedBuffer);
				}));
			}
			return true;
		}

		void EndPending() override
		{
			connection->EndPending();
		}
	};

/***********************************************************************
ConnectionState::ConnectOperation
***********************************************************************/

	class ConnectionState::ConnectOperation : public IocpOperation
	{
	public:
		Ptr<ConnectionState>					connection;
		SOCKET								operationSocket = INVALID_SOCKET;
		vint								generation = 0;

		ConnectOperation(Ptr<ConnectionState> _connection, SOCKET _operationSocket, vint _generation)
			: connection(_connection)
			, operationSocket(_operationSocket)
			, generation(_generation)
		{
		}

		bool Complete(DWORD, DWORD error) override
		{
			connection->CompleteConnect(operationSocket, generation, error);
			return true;
		}

		void EndPending() override
		{
			connection->EndPending();
		}
	};

/***********************************************************************
ConnectionState
***********************************************************************/

	Ptr<ConnectionState> ConnectionState::Retain()
	{
		CHECK_ERROR(owner != nullptr, L"IAsyncSocketConnection lost its canonical state owner.");
		return owner->GetState();
	}

	void ConnectionState::InstallCallback(IAsyncSocketCallback* value)
	{
		if (!value)
		{
			bool selfCallback = IsCurrentCallback();
			CS_LOCK(lockState)
			{
				callback = nullptr;
			}
			if (!selfCallback)
			{
				eventCallbacksDrained.Wait();
			}
			return;
		}

		bool canInstall = false;
		CS_LOCK(lockState)
		{
			canInstall = callback == nullptr && !stopping;
			if (canInstall)
			{
				callback = value;
				if (activeCallbacks++ == 0)
				{
					eventCallbacksDrained.Unsignal();
				}
			}
		}
		CHECK_ERROR(canInstall, L"IAsyncSocketConnection::InstallCallback cannot replace a callback or install one on a stopped connection.");

		CallbackFrame frame;
		frame.connection = this;
		frame.previous = currentCallbackFrame;
		currentCallbackFrame = &frame;
		try
		{
			value->OnInstalled(owner);
		}
		catch (...)
		{
		}
		currentCallbackFrame = frame.previous;
		EndCallback();
	}

	void ConnectionState::BeginReading()
	{
		CS_LOCK(lockState)
		{
			CHECK_ERROR(connected && !stopping && !terminalPending, L"IAsyncSocketConnection::BeginReadingLoopUnsafe requires a connected connection.");
			CHECK_ERROR(callback != nullptr, L"IAsyncSocketConnection::BeginReadingLoopUnsafe requires an installed callback.");
			CHECK_ERROR(!reading, L"IAsyncSocketConnection::BeginReadingLoopUnsafe can only be called once.");
			reading = true;
		}
		PostRead(Retain());
	}

	void ConnectionState::PostRead(Ptr<ConnectionState> retainedState)
	{
		auto operation = new ReadOperation(retainedState);
		DWORD immediateError = ERROR_SUCCESS;
		lockState.Enter();
		if (!connected || stopping || terminalPending || !reading || readPending)
		{
			lockState.Leave();
			delete operation;
			return;
		}

		readPending = true;
		BeginPendingLocked();
		WSABUF buffer;
		buffer.buf = (CHAR*)&operation->block->data[0];
		buffer.len = (ULONG)operation->block->data.Count();
		DWORD flags = 0;
		DWORD received = 0;
		auto result = WSARecv(socket, &buffer, 1, &received, &flags, &operation->native.overlapped, nullptr);
		if (result == SOCKET_ERROR)
		{
			auto error = WSAGetLastError();
			if (error != WSA_IO_PENDING)
			{
				immediateError = (DWORD)error;
				readPending = false;
				EndPendingLocked();
			}
		}
		lockState.Leave();

		if (immediateError != ERROR_SUCCESS)
		{
			delete operation;
			QueueTerminal(immediateError, true);
		}
	}

	void ConnectionState::DeliverRead(Ptr<ConnectionState> retainedState, Ptr<ReadBlock> block, vint bytes)
	{
		auto invoked = InvokeCallback(false, [&](IAsyncSocketCallback* installed)
		{
			installed->OnRead(&block->data[0], bytes);
		});
		if (invoked)
		{
			PostRead(retainedState);
		}
	}

	void ConnectionState::Write(Ptr<AsyncSocketBuffer> buffer)
	{
		CHECK_ERROR(buffer, L"IAsyncSocketConnection::WriteAsync requires a buffer.");
		bool empty = false;
		bool canWrite = false;
		CS_LOCK(lockState)
		{
			canWrite = connected && !stopping && !terminalPending && !writePending;
			if (canWrite)
			{
				writePending = true;
				empty = buffer->data.Count() == 0;
			}
		}
		CHECK_ERROR(canWrite, L"IAsyncSocketConnection::WriteAsync requires a connected connection with no outstanding write.");

		if (empty)
		{
			auto state = Retain();
			runtime->QueueCallback(Func<void()>([state, buffer]()
			{
				state->DeliverWrite(buffer);
			}));
			return;
		}

		auto operation = new WriteOperation(Retain(), buffer);
		DWORD immediateError = ERROR_SUCCESS;
		lockState.Enter();
		if (stopping || terminalPending)
		{
			writePending = false;
			lockState.Leave();
			delete operation;
			return;
		}
		BeginPendingLocked();
		immediateError = operation->PostLocked();
		if (immediateError != ERROR_SUCCESS)
		{
			EndPendingLocked();
		}
		lockState.Leave();

		if (immediateError != ERROR_SUCCESS)
		{
			delete operation;
			QueueTerminal(immediateError, true);
		}
	}

	void ConnectionState::DeliverWrite(Ptr<AsyncSocketBuffer> buffer)
	{
		bool deliver = false;
		CS_LOCK(lockState)
		{
			if (writePending && !stopping && !terminalPending)
			{
				writePending = false;
				deliver = true;
			}
		}
		if (deliver)
		{
			InvokeCallback(false, [&](IAsyncSocketCallback* installed)
			{
				installed->OnWriteCompleted(buffer);
			});
		}
	}

	void ConnectionState::QueueTerminal(DWORD error, bool reportError)
	{
		bool queue = false;
		CS_LOCK(lockState)
		{
			if (!stopping && !terminalPending)
			{
				terminalPending = true;
				queue = true;
			}
		}
		if (queue)
		{
			auto state = Retain();
			runtime->QueueCallback(Func<void()>([state, error, reportError]()
			{
				state->DeliverTerminal(error, reportError);
			}));
		}
	}

	void ConnectionState::DeliverTerminal(DWORD error, bool reportError)
	{
		IAsyncSocketCallback* installed = nullptr;
		bool claimed = false;
		lockState.Enter();
		if (!stopping && terminalPending)
		{
			claimed = true;
			if (reportError && callback)
			{
				installed = callback;
				if (activeCallbacks++ == 0)
				{
					eventCallbacksDrained.Unsignal();
				}
			}
		}
		lockState.Leave();
		if (!claimed)
		{
			return;
		}

		if (installed)
		{
			CallbackFrame frame{ this, currentCallbackFrame };
			currentCallbackFrame = &frame;
			try
			{
				installed->OnError(SocketErrorMessage(L"Asynchronous socket operation", error), true);
			}
			catch (...)
			{
			}
			currentCallbackFrame = frame.previous;
			EndCallback();
		}
		Stop();
	}

	void ConnectionState::StartConnectAttempt()
	{
		auto retainedState = Retain();
		SOCKET createdSocket = INVALID_SOCKET;
		ConnectOperation* operation = nullptr;
		DWORD immediateError = ERROR_SUCCESS;

		lockState.Enter();
		if (!clientMode || stopping || clientStatus != ClientStatus::WaitingForServer)
		{
			lockState.Leave();
			return;
		}
		clientAttempts++;

		createdSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		if (createdSocket == INVALID_SOCKET)
		{
			immediateError = WSAGetLastError();
		}

		SOCKADDR_IN localAddress = {};
		localAddress.sin_family = AF_INET;
		localAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		localAddress.sin_port = 0;
		if (immediateError == ERROR_SUCCESS && bind(createdSocket, (SOCKADDR*)&localAddress, sizeof(localAddress)) == SOCKET_ERROR)
		{
			immediateError = WSAGetLastError();
		}

		LPFN_CONNECTEX connectEx = nullptr;
		GUID connectExGuid = WSAID_CONNECTEX;
		DWORD transferred = 0;
		if (immediateError == ERROR_SUCCESS && WSAIoctl(
			createdSocket,
			SIO_GET_EXTENSION_FUNCTION_POINTER,
			&connectExGuid,
			sizeof(connectExGuid),
			&connectEx,
			sizeof(connectEx),
			&transferred,
			nullptr,
			nullptr
		) == SOCKET_ERROR)
		{
			immediateError = WSAGetLastError();
		}
		if (immediateError == ERROR_SUCCESS && !runtime->Associate(createdSocket))
		{
			immediateError = GetLastError();
		}

		if (immediateError == ERROR_SUCCESS)
		{
			socket = createdSocket;
			connected = false;
			auto generation = ++clientGeneration;
			operation = new ConnectOperation(retainedState, createdSocket, generation);
			BeginPendingLocked();

			SOCKADDR_IN remoteAddress = {};
			remoteAddress.sin_family = AF_INET;
			remoteAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			remoteAddress.sin_port = htons((u_short)clientPort);
			auto result = connectEx(
				createdSocket,
				(SOCKADDR*)&remoteAddress,
				sizeof(remoteAddress),
				nullptr,
				0,
				nullptr,
				&operation->native.overlapped
			);
			if (!result)
			{
				auto error = WSAGetLastError();
				if (error != WSA_IO_PENDING)
				{
					immediateError = error;
					socket = INVALID_SOCKET;
					EndPendingLocked();
				}
			}
		}
		lockState.Leave();

		if (immediateError != ERROR_SUCCESS)
		{
			if (createdSocket != INVALID_SOCKET)
			{
				closesocket(createdSocket);
			}
			delete operation;
			QueueConnectFailure(immediateError);
		}
	}

	void ConnectionState::CompleteConnect(SOCKET operationSocket, vint generation, DWORD error)
	{
		if (error == ERROR_SUCCESS)
		{
			if (setsockopt(operationSocket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0) == SOCKET_ERROR)
			{
				error = WSAGetLastError();
			}
		}

		bool accepted = false;
		bool failed = false;
		lockState.Enter();
		if (!stopping && clientStatus == ClientStatus::WaitingForServer && socket == operationSocket && clientGeneration == generation)
		{
			if (error == ERROR_SUCCESS)
			{
				connected = true;
				clientStatus = ClientStatus::Connected;
				accepted = true;
			}
			else
			{
				socket = INVALID_SOCKET;
				failed = true;
			}
		}
		lockState.Leave();

		if (accepted)
		{
			auto state = Retain();
			runtime->QueueCallback(Func<void()>([state]()
			{
				state->DeliverConnected();
			}));
		}
		else if (failed)
		{
			closesocket(operationSocket);
			QueueConnectFailure(error);
		}
	}

	void ConnectionState::DeliverConnected()
	{
		bool deliver = false;
		CS_LOCK(lockState)
		{
			deliver = connected && !stopping && clientStatus == ClientStatus::Connected;
		}
		if (deliver)
		{
			InvokeCallback(false, [](IAsyncSocketCallback* installed)
			{
				installed->OnConnected();
			});
		}
		eventWaitForServer.Signal();
	}

	void ConnectionState::QueueConnectFailure(DWORD error)
	{
		bool queue = false;
		bool fatal = false;
		CS_LOCK(lockState)
		{
			if (!stopping && clientStatus == ClientStatus::WaitingForServer)
			{
				queue = true;
				fatal = clientAttempts >= AsyncSocketClientRetryCount;
			}
		}
		if (queue)
		{
			auto state = Retain();
			runtime->QueueCallback(Func<void()>([state, error, fatal]()
			{
				state->DeliverConnectFailure(error, fatal);
			}));
		}
	}

	void ConnectionState::DeliverConnectFailure(DWORD error, bool fatal)
	{
		bool deliver = false;
		CS_LOCK(lockState)
		{
			deliver = !stopping && clientStatus == ClientStatus::WaitingForServer;
		}
		if (!deliver)
		{
			return;
		}

		InvokeCallback(false, [&](IAsyncSocketCallback* installed)
		{
			installed->OnError(SocketErrorMessage(L"ConnectEx", error), fatal);
		});
		if (fatal)
		{
			Stop();
		}
		else
		{
			ScheduleRetry();
		}
	}

	void ConnectionState::ScheduleRetry()
	{
		CS_LOCK(lockState)
		{
			if (!stopping && clientStatus == ClientStatus::WaitingForServer && clientRetryTimer)
			{
				LARGE_INTEGER dueTime;
				dueTime.QuadPart = -(LONGLONG)AsyncSocketClientRetryDelay * 10000;
				FILETIME fileTime;
				fileTime.dwLowDateTime = dueTime.LowPart;
				fileTime.dwHighDateTime = dueTime.HighPart;
				SetThreadpoolTimer(clientRetryTimer, &fileTime, 0, 0);
			}
		}
	}

	void ConnectionState::WaitForServer()
	{
		bool begin = false;
		CS_LOCK(lockState)
		{
			if (clientMode && clientStatus == ClientStatus::Ready && !stopping)
			{
				clientStatus = ClientStatus::WaitingForServer;
				begin = true;
			}
		}
		CHECK_ERROR(begin, L"IAsyncSocketClient::WaitForServer can only be called once while the client is ready.");
		StartConnectAttempt();
		eventWaitForServer.Wait();
	}

	ClientStatus ConnectionState::GetStatus()
	{
		ClientStatus result;
		CS_LOCK(lockState)
		{
			result = clientStatus;
		}
		return result;
	}

	void ConnectionState::Stop()
	{
		SOCKET closingSocket = INVALID_SOCKET;
		PTP_TIMER timer = nullptr;
		CS_LOCK(lockState)
		{
			if (!stopping)
			{
				stopping = true;
				connected = false;
				reading = false;
				writePending = false;
				terminalPending = false;
				closingSocket = socket;
				socket = INVALID_SOCKET;
				if (clientMode)
				{
					clientStatus = ClientStatus::Disconnected;
					timer = clientRetryTimer;
				}
			}
			else if (clientMode)
			{
				timer = clientRetryTimer;
			}
		}

		if (timer)
		{
			SetThreadpoolTimer(timer, nullptr, 0, 0);
			WaitForThreadpoolTimerCallbacks(timer, TRUE);
		}
		if (closingSocket != INVALID_SOCKET)
		{
			// Prefer an orderly FIN for the peer while closesocket cancels this
			// connection's pending overlapped operations.
			shutdown(closingSocket, SD_BOTH);
			closesocket(closingSocket);
		}
		eventIoDrained.Wait();

		auto selfCallback = IsCurrentCallback();
		if (!selfCallback)
		{
			eventCallbacksDrained.Wait();
		}

		IAsyncSocketCallback* installed = nullptr;
		lockState.Enter();
		if (!disconnectedNotified)
		{
			disconnectedNotified = true;
			if (callback)
			{
				installed = callback;
				if (activeCallbacks++ == 0)
				{
					eventCallbacksDrained.Unsignal();
				}
			}
		}
		stopped = true;
		lockState.Leave();

		if (installed)
		{
			CallbackFrame frame{ this, currentCallbackFrame };
			currentCallbackFrame = &frame;
			try
			{
				installed->OnDisconnected();
			}
			catch (...)
			{
			}
			currentCallbackFrame = frame.previous;
			EndCallback();
		}

		if (!selfCallback)
		{
			eventCallbacksDrained.Wait();
		}
		if (clientMode)
		{
			eventWaitForServer.Signal();
		}
	}

/***********************************************************************
AsyncSocketServer::Impl
***********************************************************************/

	class AsyncSocketServer::Impl : public Object
	{
	private:
		class AcceptOperation : public IocpOperation
		{
		public:
			Impl*							server = nullptr;
			SOCKET							acceptedSocket = INVALID_SOCKET;
			BYTE							addresses[(sizeof(SOCKADDR_IN) + 16) * 2];

			AcceptOperation(Impl* _server, SOCKET _acceptedSocket)
				: server(_server)
				, acceptedSocket(_acceptedSocket)
			{
				ZeroMemory(addresses, sizeof(addresses));
			}

			bool Complete(DWORD, DWORD error) override
			{
				server->CompleteAccept(this, error);
				return true;
			}

			void EndPending() override
			{
				server->EndAcceptPending();
			}
		};

		AsyncSocketServer*					owner = nullptr;
		vint								port = 0;
		Ptr<IocpRuntime>						runtime;
		CriticalSection					lockState;
		bool							started = false;
		bool							stopping = false;
		bool							stopped = false;
		SOCKET							listener = INVALID_SOCKET;
		LPFN_ACCEPTEX						acceptEx = nullptr;
		bool							acceptPending = false;
		vint							pendingAccepts = 0;
		EventObject						eventAcceptDrained;
		EventObject						eventStopped;
		List<Ptr<AsyncSocketConnection>>		connections;

		void BeginAcceptPendingLocked()
		{
			if (pendingAccepts++ == 0)
			{
				eventAcceptDrained.Unsignal();
			}
		}

		void EndAcceptPending()
		{
			CS_LOCK(lockState)
			{
				if (--pendingAccepts == 0)
				{
					eventAcceptDrained.Signal();
				}
			}
		}

		bool PostAccept()
		{
			SOCKET acceptedSocket = INVALID_SOCKET;
			AcceptOperation* operation = nullptr;
			DWORD immediateError = ERROR_SUCCESS;

			lockState.Enter();
			if (!started || stopping || acceptPending)
			{
				lockState.Leave();
				return false;
			}
			acceptedSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
			if (acceptedSocket == INVALID_SOCKET)
			{
				immediateError = WSAGetLastError();
			}
			else
			{
				operation = new AcceptOperation(this, acceptedSocket);
				acceptPending = true;
				BeginAcceptPendingLocked();
				DWORD received = 0;
				auto result = acceptEx(
					listener,
					acceptedSocket,
					operation->addresses,
					0,
					sizeof(SOCKADDR_IN) + 16,
					sizeof(SOCKADDR_IN) + 16,
					&received,
					&operation->native.overlapped
				);
				if (!result)
				{
					auto error = WSAGetLastError();
					if (error != WSA_IO_PENDING)
					{
						immediateError = error;
						acceptPending = false;
						EndPendingAcceptLocked();
					}
				}
			}
			lockState.Leave();

			if (immediateError != ERROR_SUCCESS)
			{
				if (acceptedSocket != INVALID_SOCKET)
				{
					closesocket(acceptedSocket);
				}
				delete operation;
				return false;
			}
			return true;
		}

		void EndPendingAcceptLocked()
		{
			if (--pendingAccepts == 0)
			{
				eventAcceptDrained.Signal();
			}
		}

		void CompleteAccept(AcceptOperation* operation, DWORD error)
		{
			bool running = false;
			CS_LOCK(lockState)
			{
				acceptPending = false;
				running = started && !stopping;
			}

			auto acceptedSocket = operation->acceptedSocket;
			if (!running || error != ERROR_SUCCESS)
			{
				closesocket(acceptedSocket);
				if (running)
				{
					PostAccept();
				}
				return;
			}

			if (setsockopt(acceptedSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (CHAR*)&listener, sizeof(listener)) == SOCKET_ERROR || !runtime->Associate(acceptedSocket))
			{
				closesocket(acceptedSocket);
				PostAccept();
				return;
			}

			auto state = Ptr(new ConnectionState(runtime.Obj(), acceptedSocket));
			auto connection = Ptr(new AsyncSocketConnection(state));
			bool retain = false;
			CS_LOCK(lockState)
			{
				if (started && !stopping)
				{
					connections.Add(connection);
					retain = true;
				}
			}

			PostAccept();
			if (!retain)
			{
				connection->Stop();
				return;
			}

			auto self = this;
			runtime->QueueCallback(Func<void()>([self, connection]()
			{
				bool invoke = false;
				CS_LOCK(self->lockState)
				{
					invoke = self->started && !self->stopping;
				}
				if (!invoke)
				{
					connection->Stop();
					return;
				}

				WaitForClientResult result = WaitForClientResult::Reject;
				try
				{
					result = self->owner->OnClientConnected(connection.Obj());
				}
				catch (...)
				{
				}
				if (result == WaitForClientResult::Reject)
				{
					connection->Stop();
				}
			}));
		}

	public:
		Impl(AsyncSocketServer* _owner, vint _port)
			: owner(_owner)
			, port(_port)
			, runtime(Ptr(new IocpRuntime))
		{
			CHECK_ERROR(eventAcceptDrained.CreateManualUnsignal(true), L"AsyncSocketServer failed to create its accept drain event.");
			CHECK_ERROR(eventStopped.CreateManualUnsignal(false), L"AsyncSocketServer failed to create its stop event.");
		}

		~Impl()
		{
			Stop();
		}

		void Start()
		{
			SOCKET createdListener = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
			CHECK_ERROR(createdListener != INVALID_SOCKET, L"AsyncSocketServer failed to create its listener socket.");

			BOOL exclusive = TRUE;
			if (setsockopt(createdListener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (CHAR*)&exclusive, sizeof(exclusive)) == SOCKET_ERROR)
			{
				closesocket(createdListener);
				CHECK_ERROR(false, L"AsyncSocketServer failed to apply SO_EXCLUSIVEADDRUSE.");
			}

			SOCKADDR_IN address = {};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = htons((u_short)port);
			if (bind(createdListener, (SOCKADDR*)&address, sizeof(address)) == SOCKET_ERROR || listen(createdListener, SOMAXCONN) == SOCKET_ERROR)
			{
				closesocket(createdListener);
				CHECK_ERROR(false, L"AsyncSocketServer failed to bind or listen on 127.0.0.1.");
			}
			if (!runtime->Associate(createdListener))
			{
				closesocket(createdListener);
				CHECK_ERROR(false, L"AsyncSocketServer failed to associate its listener with the IO completion port.");
			}

			LPFN_ACCEPTEX loadedAcceptEx = nullptr;
			GUID acceptExGuid = WSAID_ACCEPTEX;
			DWORD transferred = 0;
			if (WSAIoctl(
				createdListener,
				SIO_GET_EXTENSION_FUNCTION_POINTER,
				&acceptExGuid,
				sizeof(acceptExGuid),
				&loadedAcceptEx,
				sizeof(loadedAcceptEx),
				&transferred,
				nullptr,
				nullptr
			) == SOCKET_ERROR)
			{
				closesocket(createdListener);
				CHECK_ERROR(false, L"AsyncSocketServer failed to load AcceptEx.");
			}

			bool canStart = false;
			CS_LOCK(lockState)
			{
				if (!started && !stopping)
				{
					listener = createdListener;
					acceptEx = loadedAcceptEx;
					started = true;
					canStart = true;
				}
			}
			if (!canStart)
			{
				closesocket(createdListener);
			}
			CHECK_ERROR(canStart, L"AsyncSocketServer::Start can only be called once.");
			if (!PostAccept())
			{
				Stop();
				CHECK_ERROR(false, L"AsyncSocketServer failed to post AcceptEx.");
			}
		}

		void Stop()
		{
			SOCKET closingListener = INVALID_SOCKET;
			bool first = false;
			auto selfWorker = currentCallbackRuntime == runtime.Obj() || currentCompletionRuntime == runtime.Obj();
			CS_LOCK(lockState)
			{
				if (!stopping)
				{
					stopping = true;
					started = false;
					closingListener = listener;
					listener = INVALID_SOCKET;
					first = true;
				}
			}
			if (!first)
			{
				// A runtime callback must not wait for the caller that is draining it.
				if (!selfWorker)
				{
					eventStopped.Wait();
					// A callback-worker caller requests runtime exit but cannot join itself.
					// An external repeated Stop completes that deferred finalization here.
					runtime->Stop();
				}
				return;
			}
			if (closingListener != INVALID_SOCKET)
			{
				closesocket(closingListener);
			}
			eventAcceptDrained.Wait();

			List<Ptr<AsyncSocketConnection>> stoppingConnections;
			CS_LOCK(lockState)
			{
				for (auto connection : connections)
				{
					stoppingConnections.Add(connection);
				}
				connections.Clear();
			}
			for (auto connection : stoppingConnections)
			{
				connection->Stop();
			}

			runtime->Stop();
			CS_LOCK(lockState)
			{
				stopped = true;
			}
			eventStopped.Signal();
		}

		bool IsStopped()
		{
			bool result = false;
			CS_LOCK(lockState)
			{
				result = stopped;
			}
			return result;
		}
	};

/***********************************************************************
AsyncSocketServer
***********************************************************************/

	AsyncSocketServer::AsyncSocketServer(vint port)
	{
		CHECK_ERROR(1 <= port && port <= 65535, L"AsyncSocketServer requires a port in 1..65535.");
		impl = new Impl(this, port);
	}

	AsyncSocketServer::~AsyncSocketServer()
	{
		delete impl;
	}

	WaitForClientResult AsyncSocketServer::OnClientConnected(IAsyncSocketConnection*)
	{
		return WaitForClientResult::Accept;
	}

	void AsyncSocketServer::Start()
	{
		impl->Start();
	}

	void AsyncSocketServer::Stop()
	{
		impl->Stop();
	}

	bool AsyncSocketServer::IsStopped()
	{
		return impl->IsStopped();
	}

/***********************************************************************
AsyncSocketClient::Impl
***********************************************************************/

	class AsyncSocketClient::Impl : public Object
	{
	private:
		Ptr<IocpRuntime>						runtime;
		Ptr<ConnectionState>				state;
		Ptr<AsyncSocketConnection>			connection;
		SpinLock							lockStop;
		bool							stopped = false;

	public:
		Impl(vint port)
			: runtime(Ptr(new IocpRuntime))
			, state(Ptr(new ConnectionState(runtime.Obj(), true, port)))
			, connection(Ptr(new AsyncSocketConnection(state)))
		{
		}

		~Impl()
		{
			Stop();
		}

		void Stop()
		{
			bool first = false;
			SPIN_LOCK(lockStop)
			{
				if (!stopped)
				{
					stopped = true;
					first = true;
				}
			}
			if (first)
			{
				connection->Stop();
				state->CloseRetryTimer();
				runtime->Stop();
			}
		}

		IAsyncSocketConnection* GetConnection()
		{
			return connection.Obj();
		}

		void WaitForServer()
		{
			state->WaitForServer();
		}

		ClientStatus GetStatus()
		{
			return state->GetStatus();
		}
	};

/***********************************************************************
AsyncSocketClient
***********************************************************************/

	AsyncSocketClient::AsyncSocketClient(vint port)
	{
		CHECK_ERROR(1 <= port && port <= 65535, L"AsyncSocketClient requires a port in 1..65535.");
		impl = new Impl(port);
	}

	AsyncSocketClient::~AsyncSocketClient()
	{
		delete impl;
	}

	IAsyncSocketConnection* AsyncSocketClient::GetConnection()
	{
		return impl->GetConnection();
	}

	void AsyncSocketClient::WaitForServer()
	{
		impl->WaitForServer();
	}

	ClientStatus AsyncSocketClient::GetStatus()
	{
		return impl->GetStatus();
	}
}
