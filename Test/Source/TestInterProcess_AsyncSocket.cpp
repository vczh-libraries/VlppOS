#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.h"
#include "../../Source/Threading.h"
#if defined VCZH_MSVC
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.Windows.h"
#elif defined VCZH_GCC && defined VCZH_APPLE
#elif defined VCZH_GCC && !defined VCZH_APPLE
#endif

using namespace vl;
using namespace vl::collections;
using namespace vl::inter_process;
using namespace vl::inter_process::async_tcp_socket;

namespace async_socket_test
{
	constexpr vint AsyncSocketTestRepeatCount = 20;
	constexpr vint ConnectTimeout = 10000;
	constexpr vint TransferTimeout = 30000;
	constexpr vint RetryMilestoneTimeout = 3000;

	using AcceptHandler = Func<WaitForClientResult(IAsyncSocketConnection*)>;
	using CreateServer = Func<Ptr<IAsyncSocketServer>(vint, const AcceptHandler&)>;
	using CreateClient = Func<Ptr<IAsyncSocketClient>(vint)>;
	using WaitForEvent = Func<bool(EventObject&, vint)>;

	class SignalOnExit
	{
	private:
		EventObject*						eventObject = nullptr;

	public:
		SignalOnExit(EventObject& _eventObject)
			: eventObject(&_eventObject)
		{
		}

		~SignalOnExit()
		{
			eventObject->Signal();
		}
	};

	struct TestState
	{
		EventObject						eventWaitForServerReturned;
		EventObject						eventAccepted;
		EventObject						eventDone;
		EventObject						eventDisconnected;
		EventObject						eventFirstNonfatalError;
		EventObject						eventNestedStopReturned;

		// covers failure
		SpinLock						lockFailure;
		WString							failure;

		TestState()
		{
			CHECK_ERROR(eventWaitForServerReturned.CreateManualUnsignal(false), L"Async socket test failed to create eventWaitForServerReturned.");
			CHECK_ERROR(eventAccepted.CreateManualUnsignal(false), L"Async socket test failed to create eventAccepted.");
			CHECK_ERROR(eventDone.CreateManualUnsignal(false), L"Async socket test failed to create eventDone.");
			CHECK_ERROR(eventDisconnected.CreateManualUnsignal(false), L"Async socket test failed to create eventDisconnected.");
			CHECK_ERROR(eventFirstNonfatalError.CreateManualUnsignal(false), L"Async socket test failed to create eventFirstNonfatalError.");
			CHECK_ERROR(eventNestedStopReturned.CreateManualUnsignal(false), L"Async socket test failed to create eventNestedStopReturned.");
		}

		void Fail(const WString& message)
		{
			SPIN_LOCK(lockFailure)
			{
				if (failure == L"")
				{
					failure = message;
				}
			}
		}

		WString GetFailure()
		{
			WString result;
			SPIN_LOCK(lockFailure)
			{
				result = failure;
			}
			return result;
		}
	};

	void RecordCurrentException(TestState& state, const wchar_t* operation)
	{
		try
		{
			throw;
		}
		catch (const Error& error)
		{
			state.Fail(WString(operation) + L": " + error.Description());
		}
		catch (const Exception& exception)
		{
			state.Fail(WString(operation) + L": " + exception.Message());
		}
		catch (...)
		{
			state.Fail(WString(operation) + L": unknown exception.");
		}
	}

	bool WaitBounded(TestState& state, EventObject& eventObject, const WaitForEvent& waitForEvent, vint timeout, const wchar_t* timeoutMessage)
	{
		if (waitForEvent(eventObject, timeout))
		{
			return true;
		}
		state.Fail(timeoutMessage);
		return false;
	}

	void DrainWaitForServer(TestState& state, const WaitForEvent& waitForEvent, const wchar_t* timeoutMessage)
	{
		if (!waitForEvent(state.eventWaitForServerReturned, ConnectTimeout))
		{
			state.Fail(timeoutMessage);
			state.eventWaitForServerReturned.Wait();
		}
	}

	bool QueueWaitForServer(TestState& state, Ptr<IAsyncSocketClient> client)
	{
		auto queued = ThreadPoolLite::Queue(Func<void()>([&state, client]()
		{
			SignalOnExit signalOnExit(state.eventWaitForServerReturned);
			try
			{
				client->WaitForServer();
			}
			catch (...)
			{
				RecordCurrentException(state, L"IAsyncSocketClient::WaitForServer");
			}
		}));
		if (!queued)
		{
			state.Fail(L"ThreadPoolLite failed to queue IAsyncSocketClient::WaitForServer.");
			state.eventWaitForServerReturned.Signal();
		}
		return queued;
	}

	vuint8_t PayloadByte(vint index, vuint8_t seed)
	{
		return (vuint8_t)((index * 251 + index / 256 + seed) & 0xFF);
	}

	Ptr<AsyncSocketBuffer> CreatePayload(vint size, vuint8_t seed)
	{
		auto buffer = Ptr(new AsyncSocketBuffer);
		buffer->data.Resize(size);
		for (vint i = 0; i < size; i++)
		{
			buffer->data[i] = PayloadByte(i, seed);
		}
		return buffer;
	}

	class TestCallbackBase : public Object, public virtual IAsyncSocketCallback
	{
	protected:
		TestState*						state = nullptr;
		IAsyncSocketConnection*			connection = nullptr;
		bool							allowNonfatalError = false;
		bool							allowFatalError = false;

		void TrackCallbackAfterStop()
		{
			if (stopReturned)
			{
				callbacksAfterStop++;
			}
		}

	public:
		atomic_vint						installedCount = 0;
		atomic_vint						connectedCount = 0;
		atomic_vint						disconnectedCount = 0;
		atomic_vint						nonfatalErrorCount = 0;
		atomic_vint						fatalErrorCount = 0;
		atomic_vint						callbacksAfterStop = 0;
		atomic_vint						stopReturned = 0;
		atomic_vint						eventSequence = 0;
		atomic_vint						fatalErrorSequence = -1;
		atomic_vint						disconnectedSequence = -1;

		TestCallbackBase(TestState& _state, bool _allowNonfatalError = false, bool _allowFatalError = false)
			: state(&_state)
			, allowNonfatalError(_allowNonfatalError)
			, allowFatalError(_allowFatalError)
		{
		}

		void OnInstalled(IAsyncSocketConnection* _connection) override
		{
			TrackCallbackAfterStop();
			connection = _connection;
			installedCount++;
			if (!_connection)
			{
				state->Fail(L"IAsyncSocketCallback::OnInstalled received a null connection.");
				state->eventDone.Signal();
			}
		}

		void OnConnected() override
		{
			TrackCallbackAfterStop();
			connectedCount++;
		}

		void OnDisconnected() override
		{
			TrackCallbackAfterStop();
			disconnectedSequence = ++eventSequence;
			disconnectedCount++;
			state->eventDisconnected.Signal();
		}

		void OnError(const WString& error, bool fatal) override
		{
			TrackCallbackAfterStop();
			auto sequence = ++eventSequence;
			if (fatal)
			{
				fatalErrorSequence = sequence;
				fatalErrorCount++;
				state->eventFirstNonfatalError.Signal();
				if (!allowFatalError)
				{
					state->Fail(WString(L"Unexpected fatal async socket error: ") + error);
					state->eventDone.Signal();
				}
			}
			else
			{
				nonfatalErrorCount++;
				state->eventFirstNonfatalError.Signal();
				if (!allowNonfatalError)
				{
					state->Fail(WString(L"Unexpected nonfatal async socket error: ") + error);
					state->eventDone.Signal();
				}
			}
		}

		void MarkStopReturned()
		{
			stopReturned = 1;
		}
	};

	struct TransferState : TestState
	{
		enum Completion
		{
			ClientRead = 1,
			ClientWrite = 2,
			ServerRead = 4,
			ServerWrite = 8,
		};

		vint							requiredCompletions = 0;
		atomic_vint						completions = 0;
		atomic_vint						acceptedCount = 0;
		IAsyncSocketConnection*			serverConnection = nullptr;

		TransferState(vint _requiredCompletions)
			: requiredCompletions(_requiredCompletions)
		{
		}

		void MarkCompleted(vint completion)
		{
			auto previous = completions.fetch_or(completion);
			if (previous & completion)
			{
				Fail(L"An async socket transfer milestone was reported more than once.");
			}
			if (((previous | completion) & requiredCompletions) == requiredCompletions)
			{
				eventDone.Signal();
			}
		}
	};

	class TransferCallback : public TestCallbackBase
	{
	private:
		TransferState*					transferState = nullptr;
		vint							expectedReadSize = 0;
		vuint8_t						expectedReadSeed = 0;
		vint							expectedWriteSize = 0;
		vuint8_t						expectedWriteSeed = 0;
		vint							readCompletion = 0;
		vint							writeCompletion = 0;
		vint							readOffset = 0;
		AsyncSocketBuffer*				expectedWriteBuffer = nullptr;

		void FailTransfer(const WString& message)
		{
			state->Fail(message);
			state->eventDone.Signal();
		}

	public:
		atomic_vint						readCount = 0;
		atomic_vint						writeCompletionCount = 0;

		TransferCallback(
			TransferState& _state,
			vint _readCompletion,
			vint _writeCompletion,
			vint _expectedReadSize,
			vuint8_t _expectedReadSeed,
			vint _expectedWriteSize,
			vuint8_t _expectedWriteSeed,
			bool _allowNonfatalError = false
		)
			: TestCallbackBase(_state, _allowNonfatalError, false)
			, transferState(&_state)
			, expectedReadSize(_expectedReadSize)
			, expectedReadSeed(_expectedReadSeed)
			, expectedWriteSize(_expectedWriteSize)
			, expectedWriteSeed(_expectedWriteSeed)
			, readCompletion(_readCompletion)
			, writeCompletion(_writeCompletion)
		{
		}

		void SetExpectedWriteBuffer(AsyncSocketBuffer* buffer)
		{
			expectedWriteBuffer = buffer;
		}

		void OnRead(const vuint8_t* buffer, vint size) override
		{
			TrackCallbackAfterStop();
			readCount++;
			if (readCompletion == 0)
			{
				FailTransfer(L"Unexpected async socket read callback.");
				return;
			}
			if (!buffer || size <= 0 || readOffset > expectedReadSize || size > expectedReadSize - readOffset)
			{
				FailTransfer(L"Async socket read callback returned an invalid byte count.");
				return;
			}
			for (vint i = 0; i < size; i++)
			{
				if (buffer[i] != PayloadByte(readOffset + i, expectedReadSeed))
				{
					FailTransfer(L"Async socket read callback changed byte order or content.");
					return;
				}
			}
			readOffset += size;
			if (readOffset == expectedReadSize)
			{
				transferState->MarkCompleted(readCompletion);
			}
		}

		void OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer) override
		{
			TrackCallbackAfterStop();
			auto completionCount = ++writeCompletionCount;
			if (writeCompletion == 0)
			{
				FailTransfer(L"Unexpected async socket write completion callback.");
				return;
			}
			if (completionCount != 1 || !buffer || buffer.Obj() != expectedWriteBuffer)
			{
				FailTransfer(L"Async socket write completion did not return the submitted buffer exactly once.");
				return;
			}
			if (buffer->data.Count() != expectedWriteSize)
			{
				FailTransfer(L"Async socket write completion returned an invalid buffer.");
				return;
			}
			for (vint i = 0; i < expectedWriteSize; i++)
			{
				if (buffer->data[i] != PayloadByte(i, expectedWriteSeed))
				{
					FailTransfer(L"Async socket write completion returned changed content.");
					return;
				}
			}
			transferState->MarkCompleted(writeCompletion);
		}

		vint ReadSize()
		{
			return readOffset;
		}
	};

	class NoDataCallback : public TestCallbackBase
	{
	public:
		atomic_vint						readCount = 0;
		atomic_vint						writeCompletionCount = 0;

		NoDataCallback(TestState& state, bool allowNonfatalError = false, bool allowFatalError = false)
			: TestCallbackBase(state, allowNonfatalError, allowFatalError)
		{
		}

		void OnRead(const vuint8_t*, vint) override
		{
			TrackCallbackAfterStop();
			readCount++;
			state->Fail(L"Unexpected data was delivered to an async socket callback.");
			state->eventDone.Signal();
		}

		void OnWriteCompleted(Ptr<AsyncSocketBuffer>) override
		{
			TrackCallbackAfterStop();
			writeCompletionCount++;
			state->Fail(L"Unexpected async socket write completion callback.");
			state->eventDone.Signal();
		}
	};

	class TolerantWriteCallback : public TestCallbackBase
	{
	private:
		AsyncSocketBuffer*				expectedWriteBuffer = nullptr;
		vint							expectedWriteSize = 0;
		vuint8_t						expectedWriteSeed = 0;

	public:
		atomic_vint						readCount = 0;
		atomic_vint						writeCompletionCount = 0;

		TolerantWriteCallback(TestState& state, vint _expectedWriteSize, vuint8_t _expectedWriteSeed)
			: TestCallbackBase(state, false, true)
			, expectedWriteSize(_expectedWriteSize)
			, expectedWriteSeed(_expectedWriteSeed)
		{
		}

		void SetExpectedWriteBuffer(AsyncSocketBuffer* buffer)
		{
			expectedWriteBuffer = buffer;
		}

		void OnRead(const vuint8_t*, vint) override
		{
			TrackCallbackAfterStop();
			readCount++;
			state->Fail(L"Unexpected data was delivered to the server in the stop-from-callback test.");
			state->eventDone.Signal();
		}

		void OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer) override
		{
			TrackCallbackAfterStop();
			auto count = ++writeCompletionCount;
			if (count > 1 || !buffer || buffer.Obj() != expectedWriteBuffer || buffer->data.Count() != expectedWriteSize)
			{
				state->Fail(L"The optional server write completion was invalid.");
				state->eventDone.Signal();
				return;
			}
			for (vint i = 0; i < expectedWriteSize; i++)
			{
				if (buffer->data[i] != PayloadByte(i, expectedWriteSeed))
				{
					state->Fail(L"The optional server write completion changed content.");
					state->eventDone.Signal();
					return;
				}
			}
		}
	};

	class StopInReadCallback : public TestCallbackBase
	{
	public:
		atomic_vint						readCount = 0;
		atomic_vint						writeCompletionCount = 0;
		atomic_vint						nestedStopReturned = 0;
		atomic_vint						disconnectedBeforeStopReturned = 0;

		StopInReadCallback(TestState& state)
			: TestCallbackBase(state)
		{
		}

		void OnRead(const vuint8_t*, vint size) override
		{
			TrackCallbackAfterStop();
			auto count = ++readCount;
			if (count != 1 || size <= 0)
			{
				state->Fail(L"The stop-from-callback test received an invalid read callback.");
				state->eventNestedStopReturned.Signal();
				return;
			}

			try
			{
				connection->Stop();
				MarkStopReturned();
				nestedStopReturned = 1;
				if (disconnectedCount == 1)
				{
					disconnectedBeforeStopReturned = 1;
				}
				else
				{
					state->Fail(L"OnDisconnected was not delivered exactly once before nested Stop returned.");
				}
			}
			catch (...)
			{
				RecordCurrentException(*state, L"IAsyncSocketConnection::Stop from OnRead");
			}
			state->eventNestedStopReturned.Signal();
		}

		void OnWriteCompleted(Ptr<AsyncSocketBuffer>) override
		{
			TrackCallbackAfterStop();
			writeCompletionCount++;
			state->Fail(L"The stop-from-callback client unexpectedly completed a write.");
			state->eventDone.Signal();
		}
	};

	WaitForClientResult InstallAcceptedConnection(
		TestState& state,
		atomic_vint& acceptedCount,
		IAsyncSocketConnection*& storedConnection,
		IAsyncSocketConnection* connection,
		IAsyncSocketCallback* callback
	)
	{
		auto count = ++acceptedCount;
		if (count != 1)
		{
			state.Fail(L"The async socket server accepted more than one connection.");
			state.eventAccepted.Signal();
			return WaitForClientResult::Reject;
		}

		try
		{
			connection->InstallCallback(callback);
			connection->BeginReadingLoopUnsafe();
			storedConnection = connection;
			state.eventAccepted.Signal();
			return WaitForClientResult::Accept;
		}
		catch (...)
		{
			RecordCurrentException(state, L"IAsyncSocketServer::OnClientConnected");
			state.eventAccepted.Signal();
			return WaitForClientResult::Reject;
		}
	}

	void StopClient(TestState& state, Ptr<IAsyncSocketClient> client, TestCallbackBase& callback)
	{
		if (!client)
		{
			return;
		}
		try
		{
			auto connection = client->GetConnection();
			connection->Stop();
			callback.MarkStopReturned();
			connection->InstallCallback(nullptr);
		}
		catch (...)
		{
			RecordCurrentException(state, L"Stopping an async socket client");
		}
	}

	void StopServer(TestState& state, Ptr<IAsyncSocketServer> server)
	{
		if (!server)
		{
			return;
		}
		try
		{
			server->Stop();
		}
		catch (...)
		{
			RecordCurrentException(state, L"IAsyncSocketServer::Stop");
		}
	}

	void AssertState(TestState& state)
	{
		auto failure = state.GetFailure();
		if (failure != L"")
		{
			TEST_PRINT(failure);
		}
		TEST_ASSERT(failure == L"");
	}

	void RunLongFullDuplex(
		vint port,
		vint maximumReadBlockSize,
		const CreateServer& createServer,
		const CreateClient& createClient,
		const WaitForEvent& waitForEvent
	)
	{
		constexpr vuint8_t ClientSeed = 17;
		constexpr vuint8_t ServerSeed = 193;
		auto clientPayloadSize = maximumReadBlockSize * 8 + 257;
		auto serverPayloadSize = maximumReadBlockSize * 11 + 113;

		TransferState state(
			TransferState::ClientRead |
			TransferState::ClientWrite |
			TransferState::ServerRead |
			TransferState::ServerWrite
		);
		TransferCallback serverCallback(
			state,
			TransferState::ServerRead,
			TransferState::ServerWrite,
			clientPayloadSize,
			ClientSeed,
			serverPayloadSize,
			ServerSeed
		);
		TransferCallback clientCallback(
			state,
			TransferState::ClientRead,
			TransferState::ClientWrite,
			serverPayloadSize,
			ServerSeed,
			clientPayloadSize,
			ClientSeed
		);

		AcceptHandler acceptHandler([&](IAsyncSocketConnection* connection)
		{
			return InstallAcceptedConnection(state, state.acceptedCount, state.serverConnection, connection, &serverCallback);
		});

		auto server = createServer(port, acceptHandler);
		Ptr<IAsyncSocketClient> client;
		bool serverRunning = false;
		bool clientReady = false;
		bool waitQueued = false;
		bool waitReturned = false;
		bool accepted = false;
		bool connectedBeforeStop = false;
		bool serverStopped = false;

		try
		{
			server->Start();
			serverRunning = !server->IsStopped();
			client = createClient(port);
			clientReady = client->GetStatus() == ClientStatus::Ready;
			client->GetConnection()->InstallCallback(&clientCallback);
			waitQueued = QueueWaitForServer(state, client);
			waitReturned = WaitBounded(state, state.eventWaitForServerReturned, waitForEvent, ConnectTimeout, L"WaitForServer did not return for the full-duplex test.");
			accepted = WaitBounded(state, state.eventAccepted, waitForEvent, ConnectTimeout, L"The server did not accept the full-duplex test connection.");
			connectedBeforeStop = waitReturned && client->GetStatus() == ClientStatus::Connected;

			if (accepted && connectedBeforeStop && state.serverConnection)
			{
				client->GetConnection()->BeginReadingLoopUnsafe();
				{
					auto buffer = CreatePayload(clientPayloadSize, ClientSeed);
					clientCallback.SetExpectedWriteBuffer(buffer.Obj());
					client->GetConnection()->WriteAsync(buffer);
				}
				{
					auto buffer = CreatePayload(serverPayloadSize, ServerSeed);
					serverCallback.SetExpectedWriteBuffer(buffer.Obj());
					state.serverConnection->WriteAsync(buffer);
				}
				WaitBounded(state, state.eventDone, waitForEvent, TransferTimeout, L"The full-duplex async socket transfer timed out.");
			}
		}
		catch (...)
		{
			RecordCurrentException(state, L"Running the full-duplex async socket test");
		}

		StopClient(state, client, clientCallback);
		if (waitQueued && !waitReturned)
		{
			DrainWaitForServer(state, waitForEvent, L"WaitForServer did not drain after stopping the full-duplex client.");
		}
		StopServer(state, server);
		serverCallback.MarkStopReturned();
		serverStopped = server->IsStopped();

		AssertState(state);
		TEST_ASSERT(serverRunning);
		TEST_ASSERT(clientReady);
		TEST_ASSERT(connectedBeforeStop);
		TEST_ASSERT(serverStopped);
		TEST_ASSERT(state.acceptedCount == 1);
		TEST_ASSERT(serverCallback.installedCount == 1);
		TEST_ASSERT(clientCallback.installedCount == 1);
		TEST_ASSERT(serverCallback.connectedCount == 0);
		TEST_ASSERT(clientCallback.connectedCount == 1);
		TEST_ASSERT(serverCallback.ReadSize() == clientPayloadSize);
		TEST_ASSERT(clientCallback.ReadSize() == serverPayloadSize);
		TEST_ASSERT(serverCallback.readCount > 1);
		TEST_ASSERT(clientCallback.readCount > 1);
		TEST_ASSERT(serverCallback.writeCompletionCount == 1);
		TEST_ASSERT(clientCallback.writeCompletionCount == 1);
		TEST_ASSERT(serverCallback.nonfatalErrorCount == 0 && serverCallback.fatalErrorCount == 0);
		TEST_ASSERT(clientCallback.nonfatalErrorCount == 0 && clientCallback.fatalErrorCount == 0);
		TEST_ASSERT(serverCallback.disconnectedCount == 1);
		TEST_ASSERT(clientCallback.disconnectedCount == 1);
		TEST_ASSERT(serverCallback.callbacksAfterStop == 0);
		TEST_ASSERT(clientCallback.callbacksAfterStop == 0);
		TEST_ASSERT(client->GetStatus() == ClientStatus::Disconnected);
	}

	void RunReject(
		vint port,
		const CreateServer& createServer,
		const CreateClient& createClient,
		const WaitForEvent& waitForEvent
	)
	{
		TestState state;
		atomic_vint acceptedCount = 0;
		NoDataCallback clientCallback(state, false, true);

		AcceptHandler acceptHandler([&](IAsyncSocketConnection*)
		{
			acceptedCount++;
			state.eventAccepted.Signal();
			return WaitForClientResult::Reject;
		});

		auto server = createServer(port, acceptHandler);
		auto client = createClient(port);
		bool serverRunningBeforeStop = false;
		bool waitQueued = false;
		bool waitReturned = false;

		try
		{
			server->Start();
			client->GetConnection()->InstallCallback(&clientCallback);
			waitQueued = QueueWaitForServer(state, client);
			waitReturned = WaitBounded(state, state.eventWaitForServerReturned, waitForEvent, ConnectTimeout, L"WaitForServer did not return for the rejected connection.");
			WaitBounded(state, state.eventAccepted, waitForEvent, ConnectTimeout, L"The server did not observe the rejected connection.");
			if (waitReturned && client->GetStatus() == ClientStatus::Connected)
			{
				client->GetConnection()->BeginReadingLoopUnsafe();
			}
			WaitBounded(state, state.eventDisconnected, waitForEvent, ConnectTimeout, L"The rejected connection did not disconnect.");
			serverRunningBeforeStop = !server->IsStopped();
		}
		catch (...)
		{
			RecordCurrentException(state, L"Running the rejected async socket test");
		}

		StopClient(state, client, clientCallback);
		if (waitQueued && !waitReturned)
		{
			DrainWaitForServer(state, waitForEvent, L"WaitForServer did not drain for the rejected connection.");
		}
		StopServer(state, server);

		AssertState(state);
		TEST_ASSERT(acceptedCount == 1);
		TEST_ASSERT(serverRunningBeforeStop);
		TEST_ASSERT(clientCallback.readCount == 0);
		TEST_ASSERT(clientCallback.writeCompletionCount == 0);
		TEST_ASSERT(clientCallback.nonfatalErrorCount == 0);
		TEST_ASSERT(clientCallback.fatalErrorCount <= 1);
		TEST_ASSERT(clientCallback.disconnectedCount == 1);
		if (clientCallback.fatalErrorCount == 1)
		{
			TEST_ASSERT(clientCallback.fatalErrorSequence < clientCallback.disconnectedSequence);
		}
		TEST_ASSERT(client->GetStatus() == ClientStatus::Disconnected);
	}

	void RunStopFromCallback(
		vint port,
		const CreateServer& createServer,
		const CreateClient& createClient,
		const WaitForEvent& waitForEvent
	)
	{
		constexpr vint PayloadSize = 257;
		constexpr vuint8_t PayloadSeed = 91;
		TestState state;
		atomic_vint acceptedCount = 0;
		IAsyncSocketConnection* serverConnection = nullptr;
		TolerantWriteCallback serverCallback(state, PayloadSize, PayloadSeed);
		StopInReadCallback clientCallback(state);

		AcceptHandler acceptHandler([&](IAsyncSocketConnection* connection)
		{
			return InstallAcceptedConnection(state, acceptedCount, serverConnection, connection, &serverCallback);
		});

		auto server = createServer(port, acceptHandler);
		auto client = createClient(port);
		bool waitQueued = false;
		bool waitReturned = false;
		bool accepted = false;

		try
		{
			server->Start();
			client->GetConnection()->InstallCallback(&clientCallback);
			waitQueued = QueueWaitForServer(state, client);
			waitReturned = WaitBounded(state, state.eventWaitForServerReturned, waitForEvent, ConnectTimeout, L"WaitForServer did not return for the stop-from-callback test.");
			accepted = WaitBounded(state, state.eventAccepted, waitForEvent, ConnectTimeout, L"The server did not accept the stop-from-callback connection.");
			if (waitReturned && accepted && client->GetStatus() == ClientStatus::Connected && serverConnection)
			{
				client->GetConnection()->BeginReadingLoopUnsafe();
				auto buffer = CreatePayload(PayloadSize, PayloadSeed);
				serverCallback.SetExpectedWriteBuffer(buffer.Obj());
				serverConnection->WriteAsync(buffer);
				WaitBounded(state, state.eventNestedStopReturned, waitForEvent, ConnectTimeout, L"Stop called from OnRead did not return.");
			}
		}
		catch (...)
		{
			RecordCurrentException(state, L"Running the stop-from-callback async socket test");
		}

		StopClient(state, client, clientCallback);
		if (waitQueued && !waitReturned)
		{
			DrainWaitForServer(state, waitForEvent, L"WaitForServer did not drain for the stop-from-callback test.");
		}
		StopServer(state, server);
		serverCallback.MarkStopReturned();

		AssertState(state);
		TEST_ASSERT(acceptedCount == 1);
		TEST_ASSERT(clientCallback.readCount == 1);
		TEST_ASSERT(clientCallback.writeCompletionCount == 0);
		TEST_ASSERT(clientCallback.nestedStopReturned == 1);
		TEST_ASSERT(clientCallback.disconnectedBeforeStopReturned == 1);
		TEST_ASSERT(clientCallback.disconnectedCount == 1);
		TEST_ASSERT(clientCallback.nonfatalErrorCount == 0 && clientCallback.fatalErrorCount == 0);
		TEST_ASSERT(clientCallback.callbacksAfterStop == 0);
		TEST_ASSERT(serverCallback.readCount == 0);
		TEST_ASSERT(serverCallback.writeCompletionCount <= 1);
		TEST_ASSERT(serverCallback.nonfatalErrorCount == 0);
		TEST_ASSERT(serverCallback.fatalErrorCount <= 1);
		if (serverCallback.fatalErrorCount == 1)
		{
			TEST_ASSERT(serverCallback.fatalErrorSequence < serverCallback.disconnectedSequence);
		}
		TEST_ASSERT(serverCallback.disconnectedCount == 1);
	}

	void RunRetryThenConnect(
		vint port,
		const CreateServer& createServer,
		const CreateClient& createClient,
		const WaitForEvent& waitForEvent
	)
	{
		constexpr vint PayloadSize = 257;
		constexpr vuint8_t PayloadSeed = 47;
		TransferState state(TransferState::ClientWrite | TransferState::ServerRead);
		TransferCallback serverCallback(
			state,
			TransferState::ServerRead,
			0,
			PayloadSize,
			PayloadSeed,
			0,
			0
		);
		TransferCallback clientCallback(
			state,
			0,
			TransferState::ClientWrite,
			0,
			0,
			PayloadSize,
			PayloadSeed,
			true
		);

		AcceptHandler acceptHandler([&](IAsyncSocketConnection* connection)
		{
			return InstallAcceptedConnection(state, state.acceptedCount, state.serverConnection, connection, &serverCallback);
		});

		auto client = createClient(port);
		Ptr<IAsyncSocketServer> server;
		bool clientReady = client->GetStatus() == ClientStatus::Ready;
		bool firstFailureObserved = false;
		bool waitQueued = false;
		bool waitReturned = false;
		bool accepted = false;
		bool connectedBeforeStop = false;

		try
		{
			client->GetConnection()->InstallCallback(&clientCallback);
			waitQueued = QueueWaitForServer(state, client);
			firstFailureObserved = WaitBounded(state, state.eventFirstNonfatalError, waitForEvent, RetryMilestoneTimeout, L"The async socket client did not report the first retryable connection failure.");
			if (firstFailureObserved && clientCallback.nonfatalErrorCount > 0 && clientCallback.fatalErrorCount == 0)
			{
				server = createServer(port, acceptHandler);
				server->Start();
			}
			waitReturned = WaitBounded(state, state.eventWaitForServerReturned, waitForEvent, ConnectTimeout, L"WaitForServer did not return after starting the retry server.");
			accepted = WaitBounded(state, state.eventAccepted, waitForEvent, ConnectTimeout, L"The retry server did not accept the client.");
			connectedBeforeStop = waitReturned && client->GetStatus() == ClientStatus::Connected;
			if (accepted && connectedBeforeStop)
			{
				client->GetConnection()->BeginReadingLoopUnsafe();
				auto buffer = CreatePayload(PayloadSize, PayloadSeed);
				clientCallback.SetExpectedWriteBuffer(buffer.Obj());
				client->GetConnection()->WriteAsync(buffer);
				WaitBounded(state, state.eventDone, waitForEvent, ConnectTimeout, L"The retry connection did not transfer data.");
			}
		}
		catch (...)
		{
			RecordCurrentException(state, L"Running the retry-then-connect async socket test");
		}

		StopClient(state, client, clientCallback);
		if (waitQueued && !waitReturned)
		{
			DrainWaitForServer(state, waitForEvent, L"WaitForServer did not drain in the retry-then-connect test.");
		}
		StopServer(state, server);
		serverCallback.MarkStopReturned();

		AssertState(state);
		TEST_ASSERT(clientReady);
		TEST_ASSERT(firstFailureObserved);
		TEST_ASSERT(clientCallback.nonfatalErrorCount >= 1);
		TEST_ASSERT(clientCallback.fatalErrorCount == 0);
		TEST_ASSERT(clientCallback.connectedCount == 1);
		TEST_ASSERT(connectedBeforeStop);
		TEST_ASSERT(state.acceptedCount == 1);
		TEST_ASSERT(serverCallback.ReadSize() == PayloadSize);
		TEST_ASSERT(serverCallback.readCount >= 1);
		TEST_ASSERT(clientCallback.writeCompletionCount == 1);
		TEST_ASSERT(clientCallback.disconnectedCount == 1);
		TEST_ASSERT(clientCallback.callbacksAfterStop == 0);
		TEST_ASSERT(client->GetStatus() == ClientStatus::Disconnected);
	}

	void RunStopDuringRetry(
		vint port,
		const CreateClient& createClient,
		const WaitForEvent& waitForEvent
	)
	{
		TestState state;
		NoDataCallback clientCallback(state, true, false);
		auto client = createClient(port);
		bool clientReady = client->GetStatus() == ClientStatus::Ready;
		bool firstFailureObserved = false;
		bool waitQueued = false;
		bool stopReturned = false;

		try
		{
			client->GetConnection()->InstallCallback(&clientCallback);
			waitQueued = QueueWaitForServer(state, client);
			firstFailureObserved = WaitBounded(state, state.eventFirstNonfatalError, waitForEvent, RetryMilestoneTimeout, L"The async socket client did not begin retrying before Stop.");
			client->GetConnection()->Stop();
			stopReturned = true;
			clientCallback.MarkStopReturned();
			client->GetConnection()->Stop();
			if (waitQueued)
			{
				DrainWaitForServer(state, waitForEvent, L"WaitForServer did not return after stopping retry work.");
			}
			client->GetConnection()->InstallCallback(nullptr);
		}
		catch (...)
		{
			RecordCurrentException(state, L"Running the stop-during-retry async socket test");
		}

		if (!stopReturned)
		{
			StopClient(state, client, clientCallback);
			if (waitQueued)
			{
				DrainWaitForServer(state, waitForEvent, L"WaitForServer did not drain after retry-stop cleanup.");
			}
		}

		AssertState(state);
		TEST_ASSERT(clientReady);
		TEST_ASSERT(firstFailureObserved);
		TEST_ASSERT(stopReturned);
		TEST_ASSERT(clientCallback.nonfatalErrorCount >= 1);
		TEST_ASSERT(clientCallback.fatalErrorCount == 0);
		TEST_ASSERT(clientCallback.connectedCount == 0);
		TEST_ASSERT(clientCallback.disconnectedCount == 1);
		TEST_ASSERT(clientCallback.readCount == 0);
		TEST_ASSERT(clientCallback.writeCompletionCount == 0);
		TEST_ASSERT(clientCallback.callbacksAfterStop == 0);
		TEST_ASSERT(client->GetStatus() == ClientStatus::Disconnected);
	}

	template<typename TServerBase>
	class TestServer : public TServerBase
	{
	private:
		AcceptHandler						acceptHandler;

	public:
		TestServer(vint port, const AcceptHandler& _acceptHandler)
			: TServerBase(port)
			, acceptHandler(_acceptHandler)
		{
		}

		WaitForClientResult OnClientConnected(IAsyncSocketConnection* connection) override
		{
			return acceptHandler(connection);
		}
	};

	template<typename TServerBase>
	Ptr<IAsyncSocketServer> CreateTestServer(vint port, const AcceptHandler& acceptHandler)
	{
		return Ptr<IAsyncSocketServer>(new TestServer<TServerBase>(port, acceptHandler));
	}

	template<typename TClient>
	Ptr<IAsyncSocketClient> CreateTestClient(vint port)
	{
		return Ptr<IAsyncSocketClient>(new TClient(port));
	}

	template<typename TServerBase, typename TClient>
	void RunAsyncSocketTestCases(vint maximumReadBlockSize, const WaitForEvent& waitForEvent)
	{
		CreateServer createServer(&CreateTestServer<TServerBase>);
		CreateClient createClient(&CreateTestClient<TClient>);

		TEST_CASE(L"AsyncSocket full-duplex long data")
		{
			for (vint i = 0; i < AsyncSocketTestRepeatCount; i++)
			{
				RunLongFullDuplex(38000 + i, maximumReadBlockSize, createServer, createClient, waitForEvent);
			}
		});

		TEST_CASE(L"AsyncSocket rejected connection")
		{
			RunReject(38100, createServer, createClient, waitForEvent);
		});

		TEST_CASE(L"AsyncSocket Stop from OnRead")
		{
			for (vint i = 0; i < AsyncSocketTestRepeatCount; i++)
			{
				RunStopFromCallback(38200 + i, createServer, createClient, waitForEvent);
			}
		});

		TEST_CASE(L"AsyncSocket retry then connect")
		{
			RunRetryThenConnect(38300, createServer, createClient, waitForEvent);
		});

		TEST_CASE(L"AsyncSocket Stop during retry")
		{
			RunStopDuringRetry(38400, createClient, waitForEvent);
		});
	}
}

#if defined VCZH_MSVC

namespace async_socket_test
{
	bool WaitForWindowsEvent(EventObject& eventObject, vint timeout)
	{
		return eventObject.WaitForTime(timeout);
	}
}

#elif defined VCZH_GCC && defined VCZH_APPLE
#elif defined VCZH_GCC && !defined VCZH_APPLE
#endif

using namespace async_socket_test;

TEST_FILE
{
#if defined VCZH_MSVC
	using namespace vl::inter_process::async_tcp_socket::windows_socket;
	RunAsyncSocketTestCases<AsyncSocketServer, AsyncSocketClient>(65536, WaitForEvent(&WaitForWindowsEvent));
#elif defined VCZH_GCC && defined VCZH_APPLE
#elif defined VCZH_GCC && !defined VCZH_APPLE
#endif
}
