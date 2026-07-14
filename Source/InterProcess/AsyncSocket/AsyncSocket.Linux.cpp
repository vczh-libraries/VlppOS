#include "AsyncSocket.Linux.h"
#include "../../Threading.h"

#include <liburing.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace vl::inter_process::async_tcp_socket::linux_socket
{
	using namespace collections;

	class RingOperation;
	class RingRuntime;
	class ConnectionState;
	class ServerState;
	class AsyncSocketConnection;

	static thread_local RingRuntime* currentRingRuntime = nullptr;

	struct ConnectionCallbackFrame
	{
		ConnectionState*					connection = nullptr;
		ConnectionCallbackFrame*			previous = nullptr;
	};

	struct ServerCallbackFrame
	{
		ServerState*						server = nullptr;
		ServerCallbackFrame*				previous = nullptr;
	};

	static thread_local ConnectionCallbackFrame* currentConnectionCallbackFrame = nullptr;
	static thread_local ServerCallbackFrame* currentServerCallbackFrame = nullptr;

	WString LinuxSocketErrorMessage(const wchar_t* operation, vint error)
	{
		return WString::Unmanaged(operation)
			+ L" failed with Linux error "
			+ itow(error)
			+ L" ("
			+ atow(AString::Unmanaged(strerror((int)error)))
			+ L").";
	}

	void CloseFileDescriptor(vint fileDescriptor)
	{
		if (fileDescriptor >= 0)
		{
			// Do not retry close after EINTR: the descriptor number can already be reused.
			close((int)fileDescriptor);
		}
	}

	class OwnedFileDescriptor
	{
	private:
		vint								fileDescriptor = -1;

	public:
		OwnedFileDescriptor(vint _fileDescriptor)
			: fileDescriptor(_fileDescriptor)
		{
		}

		OwnedFileDescriptor(const OwnedFileDescriptor&) = delete;
		OwnedFileDescriptor& operator=(const OwnedFileDescriptor&) = delete;

		~OwnedFileDescriptor()
		{
			CloseFileDescriptor(fileDescriptor);
		}

		vint Get()
		{
			return fileDescriptor;
		}

		vint Detach()
		{
			auto result = fileDescriptor;
			fileDescriptor = -1;
			return result;
		}
	};

	class OperationDrain : public Object
	{
	private:
		CriticalSection					lockDrain;
		vint							pendingOperations = 0;
		EventObject						eventDrained;

	public:
		OperationDrain()
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::OperationDrain::OperationDrain()#"
			CHECK_ERROR(eventDrained.CreateManualUnsignal(true), ERROR_MESSAGE_PREFIX L"Failed to create an operation drain event.");
#undef ERROR_MESSAGE_PREFIX
		}

		void Begin()
		{
			CS_LOCK(lockDrain)
			{
				if (pendingOperations++ == 0)
				{
					eventDrained.Unsignal();
				}
			}
		}

		void End()
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::OperationDrain::End()#"
			CS_LOCK(lockDrain)
			{
				CHECK_ERROR(pendingOperations > 0, ERROR_MESSAGE_PREFIX L"Operation drain bookkeeping became unbalanced.");
				if (--pendingOperations == 0)
				{
					eventDrained.Signal();
				}
			}
#undef ERROR_MESSAGE_PREFIX
		}

		void Wait()
		{
			eventDrained.Wait();
		}
	};

	class RingOperationOwner : public Object
	{
		friend class RingRuntime;
	protected:
		Ptr<OperationDrain>				operationDrain;

		RingOperationOwner()
			: operationDrain(Ptr(new OperationDrain))
		{
		}

		virtual void EndTargetOperation() = 0;
		virtual void HandleOperationFailure(Ptr<RingOperationOwner> retainedOwner) = 0;
	};

	class RingOperation
	{
		friend class RingRuntime;
	private:
		Ptr<RingOperationOwner>			operationOwner;
		bool								targetOperation = false;

	public:
		vuint64_t							id = 0;

		RingOperation(vuint64_t _id, Ptr<RingOperationOwner> _operationOwner = nullptr, bool _targetOperation = false)
			: id(_id)
			, operationOwner(_operationOwner)
			, targetOperation(_targetOperation)
		{
		}

		virtual ~RingOperation() = default;
		virtual void Prepare(io_uring_sqe* sqe) noexcept = 0;
		virtual void Handle(vint result) = 0;
	};

	class RingRuntime : public Object
	{
	private:
		enum class FlushResult
		{
			Done,
			ConsumeCompletion,
		};

		io_uring							ring = {};
		CriticalSection					lockRuntime;
		ConditionVariable				cvRuntimeProgress;
		Dictionary<vuint64_t, RingOperation*>	operations;
		vuint64_t							nextOperationId = 1;
		bool								ringInitialized = false;
		bool								workerStarted = false;
		bool								stopRequested = false;
		EventObject							eventWorkerStopped;

		vuint64_t ReserveOperationIdLocked()
		{
			auto result = nextOperationId++;
			if (result == 0)
			{
				result = nextOperationId++;
			}
			return result;
		}

		FlushResult FlushPendingSubmissionsLocked()
		{
			while (io_uring_sq_ready(&ring))
			{
				vint submitResult = 0;
				do
				{
					submitResult = io_uring_submit(&ring);
				} while (submitResult == -EINTR);
				if (submitResult == -EAGAIN)
				{
					std::abort();
				}
				if (submitResult == -EBUSY)
				{
					return FlushResult::ConsumeCompletion;
				}
				if (submitResult <= 0)
				{
					std::abort();
				}
			}
			return FlushResult::Done;
		}

		void FlushPendingSubmissionsCore(bool completionWorker)
		{
			while (true)
			{
				FlushResult result = FlushResult::Done;
				bool waited = true;
				lockRuntime.Enter();
				if (!ringInitialized)
				{
					lockRuntime.Leave();
					return;
				}
				result = FlushPendingSubmissionsLocked();
				if (result == FlushResult::ConsumeCompletion && !completionWorker)
				{
					waited = cvRuntimeProgress.SleepWith(lockRuntime);
				}
				lockRuntime.Leave();
				if (!waited)
				{
					std::abort();
				}
				if (result == FlushResult::Done)
				{
					return;
				}
				if (completionWorker)
				{
					return;
				}
			}
		}

		bool SubmitLocked(RingOperation* operation, bool allowStopping)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::RingRuntime::SubmitLocked(RingOperation*, bool)#"
			if (!ringInitialized || (!allowStopping && stopRequested) || operation->id == 0 || operations.Keys().Contains(operation->id))
			{
				delete operation;
				return false;
			}

			try
			{
				operations.Add(operation->id, operation);
			}
			catch (...)
			{
				delete operation;
				return false;
			}
			auto sqe = io_uring_get_sqe(&ring);
			if (!sqe)
			{
				operations.Remove(operation->id);
				delete operation;
				return false;
			}

			operation->Prepare(sqe);
			io_uring_sqe_set_data64(sqe, operation->id);
			vint submitResult = 0;
			do
			{
				submitResult = io_uring_submit(&ring);
			} while (submitResult == -EINTR);
			if (submitResult == -EAGAIN || (submitResult <= 0 && submitResult != -EBUSY))
			{
				std::abort();
			}
#undef ERROR_MESSAGE_PREFIX
			return true;
		}

		void WorkerLoop()
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::RingRuntime::WorkerLoop()#"
			currentRingRuntime = this;
			while (true)
			{
				io_uring_cqe* cqe = nullptr;
				vint waitResult = 0;
				do
				{
					waitResult = io_uring_wait_cqe(&ring, &cqe);
				} while (waitResult == -EINTR);
				if (waitResult != 0 || !cqe)
				{
					std::abort();
				}

				auto operationId = io_uring_cqe_get_data64(cqe);
				RingOperation* operation = nullptr;
				CS_LOCK(lockRuntime)
				{
					if (operations.Keys().Contains(operationId))
					{
						operation = operations[operationId];
					}
				}
				if (!operation)
				{
					std::abort();
				}

				bool operationFailed = false;
				try
				{
					operation->Handle((vint)cqe->res);
				}
				catch (...)
				{
					operationFailed = true;
				}
				io_uring_cqe_seen(&ring, cqe);

				CS_LOCK(lockRuntime)
				{
					operations.Remove(operationId);
					cvRuntimeProgress.WakeAllPendings();
				}
				auto operationOwner = operation->operationOwner;
				auto operationDrain = operationOwner ? operationOwner->operationDrain : nullptr;
				auto targetOperation = operation->targetOperation;
				delete operation;
				if (operationOwner && targetOperation)
				{
					operationOwner->EndTargetOperation();
				}
				if (operationOwner && operationFailed)
				{
					try
					{
						operationOwner->HandleOperationFailure(operationOwner);
					}
					catch (...)
					{
					}
				}
				operationOwner = nullptr;
				if (operationDrain)
				{
					operationDrain->End();
				}

				FlushPendingSubmissionsCore(true);

				bool shouldStop = false;
				CS_LOCK(lockRuntime)
				{
					shouldStop = stopRequested && operations.Count() == 0;
				}
				if (shouldStop)
				{
					break;
				}
			}

			CS_LOCK(lockRuntime)
			{
				io_uring_queue_exit(&ring);
				ringInitialized = false;
				cvRuntimeProgress.WakeAllPendings();
			}
			currentRingRuntime = nullptr;
			eventWorkerStopped.Signal();
#undef ERROR_MESSAGE_PREFIX
		}

	public:
		RingRuntime()
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::RingRuntime::RingRuntime()#"
			CHECK_ERROR(eventWorkerStopped.CreateManualUnsignal(false), ERROR_MESSAGE_PREFIX L"Failed to create the io_uring worker drain event.");

			io_uring_params parameters = {};
			auto initResult = io_uring_queue_init_params(1024, &ring, &parameters);
			CHECK_ERROR(initResult == 0, ERROR_MESSAGE_PREFIX L"Failed to initialize io_uring.");
			ringInitialized = true;

			auto probe = io_uring_get_probe_ring(&ring);
			if (!probe)
			{
				io_uring_queue_exit(&ring);
				ringInitialized = false;
				CHECK_FAIL(ERROR_MESSAGE_PREFIX L"Failed to probe io_uring operations.");
			}
			auto supported =
				io_uring_opcode_supported(probe, IORING_OP_NOP) &&
				io_uring_opcode_supported(probe, IORING_OP_ACCEPT) &&
				io_uring_opcode_supported(probe, IORING_OP_CONNECT) &&
				io_uring_opcode_supported(probe, IORING_OP_RECV) &&
				io_uring_opcode_supported(probe, IORING_OP_SEND) &&
				io_uring_opcode_supported(probe, IORING_OP_ASYNC_CANCEL) &&
				io_uring_opcode_supported(probe, IORING_OP_TIMEOUT);
			io_uring_free_probe(probe);
			if (!supported)
			{
				io_uring_queue_exit(&ring);
				ringInitialized = false;
				CHECK_FAIL(ERROR_MESSAGE_PREFIX L"Requires io_uring accept, connect, receive, send, async-cancel, timeout, and nop operations.");
			}
#undef ERROR_MESSAGE_PREFIX
		}

		~RingRuntime()
		{
			Stop();
		}

		static Ptr<RingRuntime> Create(bool startWorker)
		{
			auto result = Ptr(new RingRuntime);
			if (startWorker)
			{
				result->Start(result);
			}
			return result;
		}

		void Start(Ptr<RingRuntime> retainedRuntime)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::RingRuntime::Start(Ptr<RingRuntime>)#"
			CS_LOCK(lockRuntime)
			{
				CHECK_ERROR(!workerStarted && !stopRequested && ringInitialized, ERROR_MESSAGE_PREFIX L"The io_uring runtime can only be started once before stopping.");
				auto worker = Thread::CreateAndStart(Func<void()>([retainedRuntime]()
				{
					retainedRuntime->WorkerLoop();
				}), true);
				CHECK_ERROR(worker != nullptr, ERROR_MESSAGE_PREFIX L"Failed to start the io_uring completion worker.");
				workerStarted = true;
			}
#undef ERROR_MESSAGE_PREFIX
		}

		vuint64_t ReserveOperationId()
		{
			vuint64_t result = 0;
			CS_LOCK(lockRuntime)
			{
				result = ReserveOperationIdLocked();
			}
			return result;
		}

		bool Submit(RingOperation* operation)
		{
			bool result = false;
			CS_LOCK(lockRuntime)
			{
				result = SubmitLocked(operation, false);
			}
			return result;
		}

		void FlushPendingSubmissions()
		{
			if (currentRingRuntime != this)
			{
				FlushPendingSubmissionsCore(false);
			}
		}

		void Stop();
	};

	class RuntimeWakeOperation : public RingOperation
	{
	public:
		RuntimeWakeOperation(vuint64_t id)
			: RingOperation(id)
		{
		}

		void Prepare(io_uring_sqe* sqe) noexcept override
		{
			io_uring_prep_nop(sqe);
		}

		void Handle(vint) override
		{
		}
	};

	void RingRuntime::Stop()
	{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::RingRuntime::Stop()#"
		bool waitForWorker = false;
		bool signalWithoutWorker = false;
		bool wakeFailed = false;
		bool wakeSubmitted = false;
		CS_LOCK(lockRuntime)
		{
			if (!stopRequested)
			{
				if (workerStarted)
				{
					if (currentRingRuntime == this)
					{
						stopRequested = true;
					}
					else
					{
						auto operation = new RuntimeWakeOperation(ReserveOperationIdLocked());
						if (SubmitLocked(operation, true))
						{
							stopRequested = true;
							wakeSubmitted = true;
						}
						else
						{
							wakeFailed = true;
						}
					}
				}
				else
				{
					stopRequested = true;
					if (ringInitialized)
					{
						io_uring_queue_exit(&ring);
						ringInitialized = false;
						cvRuntimeProgress.WakeAllPendings();
					}
					signalWithoutWorker = true;
				}
			}
			waitForWorker = stopRequested && workerStarted && currentRingRuntime != this;
		}
		CHECK_ERROR(!wakeFailed, ERROR_MESSAGE_PREFIX L"Failed to wake the stopping io_uring worker.");
		if (wakeSubmitted)
		{
			FlushPendingSubmissions();
		}

		if (signalWithoutWorker)
		{
			eventWorkerStopped.Signal();
		}
		if (waitForWorker)
		{
			eventWorkerStopped.Wait();
		}
#undef ERROR_MESSAGE_PREFIX
	}

	class ConnectionState : public RingOperationOwner
	{
		friend class AsyncSocketConnection;
	private:
		class ReceiveOperation;
		class WriteOperation;
		class ConnectOperation;
		class RetryOperation;
		class CancelOperation;

		Ptr<RingRuntime>					runtime;
		AsyncSocketConnection*			owner = nullptr;

		// covers all fields below, callback counts, and target operation counts
		CriticalSection					lockState;
		ConditionVariable				cvCallbacks;
		vint								fileDescriptor = -1;
		IAsyncSocketCallback*				callback = nullptr;
		bool								connected = false;
		bool								stopping = false;
		bool								reading = false;
		bool								writePending = false;
		bool								terminalPending = false;
		bool								disconnectedNotified = false;
		vint								activeCallbacks = 0;

		vuint64_t						readOperationId = 0;
		vuint64_t						writeOperationId = 0;
		vuint64_t						connectOperationId = 0;
		vuint64_t						retryOperationId = 0;
		bool								readCancelRequested = false;
		bool								writeCancelRequested = false;
		bool								connectCancelRequested = false;
		bool								retryCancelRequested = false;
		vint								targetOperations = 0;

		bool								clientMode = false;
		vint								clientPort = 0;
		ClientStatus						clientStatus = ClientStatus::Ready;
		vint								clientAttempts = 0;
		EventObject							eventWaitForServer;

		void BeginTargetLocked()
		{
			targetOperations++;
			operationDrain->Begin();
		}

		void BeginCancelLocked()
		{
			operationDrain->Begin();
		}

		void RollbackTargetLocked()
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::ConnectionState::RollbackTargetLocked()#"
			CHECK_ERROR(targetOperations > 0, ERROR_MESSAGE_PREFIX L"Target-operation bookkeeping became unbalanced.");
			targetOperations--;
			operationDrain->End();
#undef ERROR_MESSAGE_PREFIX
		}

		void RollbackCancelLocked()
		{
			operationDrain->End();
		}

		void EndTargetOperation() override
		{
			CS_LOCK(lockState)
			{
				if (targetOperations <= 0)
				{
					std::abort();
				}
				targetOperations--;
				if (stopping && targetOperations == 0 && fileDescriptor >= 0)
				{
					CloseFileDescriptor(fileDescriptor);
					fileDescriptor = -1;
				}
			}
		}

		void HandleOperationFailure(Ptr<RingOperationOwner> retainedOwner) override
		{
			auto retainedState = retainedOwner.Cast<ConnectionState>();
			if (!retainedState)
			{
				std::abort();
			}
			try
			{
				BeginTerminal(retainedState, EIO, true);
			}
			catch (...)
			{
				Stop(retainedState);
			}
		}

		vint CurrentCallbackDepth()
		{
			vint result = 0;
			for (auto frame = currentConnectionCallbackFrame; frame; frame = frame->previous)
			{
				if (frame->connection == this)
				{
					result++;
				}
			}
			return result;
		}

		IAsyncSocketCallback* BeginCallback(bool terminal)
		{
			IAsyncSocketCallback* result = nullptr;
			CS_LOCK(lockState)
			{
				if (callback && (terminal || (!stopping && !terminalPending)))
				{
					result = callback;
					activeCallbacks++;
				}
			}
			return result;
		}

		void EndCallback()
		{
			CS_LOCK(lockState)
			{
				activeCallbacks--;
				cvCallbacks.WakeAllPendings();
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

			ConnectionCallbackFrame frame{ this, currentConnectionCallbackFrame };
			currentConnectionCallbackFrame = &frame;
			try
			{
				invoke(installed);
			}
			catch (...)
			{
			}
			currentConnectionCallbackFrame = frame.previous;
			EndCallback();
			return true;
		}

		void PostRead(Ptr<ConnectionState> retainedState);
		void BeginTerminal(Ptr<ConnectionState> retainedState, vint error, bool reportError);
		void StartConnectAttempt(Ptr<ConnectionState> retainedState);
		void DeliverConnectFailure(Ptr<ConnectionState> retainedState, vint error, bool fatal);
		void ScheduleRetry(Ptr<ConnectionState> retainedState);

	public:
		ConnectionState(Ptr<RingRuntime> _runtime, bool _clientMode, vint _clientPort)
			: runtime(_runtime)
			, clientMode(_clientMode)
			, clientPort(_clientPort)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::ConnectionState::ConnectionState(Ptr<RingRuntime>, bool, vint)#"
			CHECK_ERROR(eventWaitForServer.CreateManualUnsignal(false), ERROR_MESSAGE_PREFIX L"Failed to create the client wait event.");
#undef ERROR_MESSAGE_PREFIX
		}

		ConnectionState(Ptr<RingRuntime> _runtime, vint _fileDescriptor)
			: runtime(_runtime)
			, fileDescriptor(_fileDescriptor)
			, connected(true)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::ConnectionState::ConnectionState(Ptr<RingRuntime>, vint)#"
			CHECK_ERROR(eventWaitForServer.CreateManualUnsignal(false), ERROR_MESSAGE_PREFIX L"Failed to create the client wait event.");
#undef ERROR_MESSAGE_PREFIX
		}

		void InstallCallback(Ptr<ConnectionState> retainedState, IAsyncSocketCallback* value);
		void BeginReading(Ptr<ConnectionState> retainedState);
		void Write(Ptr<ConnectionState> retainedState, Ptr<AsyncSocketBuffer> buffer);
		void Stop(Ptr<ConnectionState> retainedState);
		void WaitForServer(Ptr<ConnectionState> retainedState);
		ClientStatus GetStatus();
	};

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
			state->Stop(state);
			state->owner = nullptr;
		}

		void InstallCallback(IAsyncSocketCallback* callback) override
		{
			state->InstallCallback(state, callback);
		}

		void BeginReadingLoopUnsafe() override
		{
			state->BeginReading(state);
		}

		void WriteAsync(Ptr<AsyncSocketBuffer> buffer) override
		{
			state->Write(state, buffer);
		}

		void Stop() override
		{
			state->Stop(state);
		}
	};

	class ConnectionState::CancelOperation : public RingOperation
	{
	public:
		Ptr<ConnectionState>					connection;
		vuint64_t							targetId = 0;

		CancelOperation(vuint64_t id, Ptr<ConnectionState> _connection, vuint64_t _targetId)
			: RingOperation(id, _connection, false)
			, connection(_connection)
			, targetId(_targetId)
		{
		}

		void Prepare(io_uring_sqe* sqe) noexcept override
		{
			io_uring_prep_cancel64(sqe, targetId, 0);
		}

		void Handle(vint) override
		{
			CS_LOCK(connection->lockState)
			{
			}
		}
	};

	class ConnectionState::ReceiveOperation : public RingOperation
	{
	public:
		Ptr<ConnectionState>					connection;
		vint								fileDescriptor = -1;
		Array<vuint8_t>						buffer;

		ReceiveOperation(vuint64_t id, Ptr<ConnectionState> _connection, vint _fileDescriptor)
			: RingOperation(id, _connection, true)
			, connection(_connection)
			, fileDescriptor(_fileDescriptor)
		{
			buffer.Resize(65536);
		}

		void Prepare(io_uring_sqe* sqe) noexcept override
		{
			io_uring_prep_recv(sqe, (int)fileDescriptor, &buffer[0], (size_t)buffer.Count(), 0);
		}

		void Handle(vint result) override
		{
			bool active = false;
			CS_LOCK(connection->lockState)
			{
				if (connection->readOperationId == id)
				{
					connection->readOperationId = 0;
					connection->readCancelRequested = false;
					active = !connection->stopping && connection->connected && connection->reading;
				}
			}
			if (!active)
			{
				return;
			}

			if (result > 0 && result <= buffer.Count())
			{
				auto invoked = connection->InvokeCallback(false, [&](IAsyncSocketCallback* installed)
				{
					installed->OnRead(&buffer[0], result);
				});
				if (invoked)
				{
					connection->PostRead(connection);
				}
			}
			else if (result == 0)
			{
				connection->BeginTerminal(connection, 0, false);
			}
			else
			{
				auto error = result < 0 ? -result : EIO;
				connection->BeginTerminal(connection, error, true);
			}
		}
	};

	class ConnectionState::WriteOperation : public RingOperation
	{
	public:
		Ptr<ConnectionState>					connection;
		vint								fileDescriptor = -1;
		Ptr<AsyncSocketBuffer>				buffer;
		vint								offset = 0;
		bool								empty = false;

		WriteOperation(vuint64_t id, Ptr<ConnectionState> _connection, vint _fileDescriptor, Ptr<AsyncSocketBuffer> _buffer, vint _offset)
			: RingOperation(id, _connection, true)
			, connection(_connection)
			, fileDescriptor(_fileDescriptor)
			, buffer(_buffer)
			, offset(_offset)
			, empty(_buffer->data.Count() == 0)
		{
		}

		void Prepare(io_uring_sqe* sqe) noexcept override
		{
			if (empty)
			{
				io_uring_prep_nop(sqe);
			}
			else
			{
				io_uring_prep_send(
					sqe,
					(int)fileDescriptor,
					&buffer->data[offset],
					(size_t)(buffer->data.Count() - offset),
					MSG_NOSIGNAL
				);
			}
		}

		void Handle(vint result) override
		{
			if (empty)
			{
				bool deliver = false;
				CS_LOCK(connection->lockState)
				{
					if (connection->writeOperationId == id)
					{
						connection->writeOperationId = 0;
						connection->writeCancelRequested = false;
						if (!connection->stopping && connection->connected && result == 0)
						{
							connection->writePending = false;
							deliver = true;
						}
					}
				}
				if (deliver)
				{
					connection->InvokeCallback(false, [&](IAsyncSocketCallback* installed)
					{
						installed->OnWriteCompleted(buffer);
					});
				}
				else if (result < 0 && result != -ECANCELED)
				{
					connection->BeginTerminal(connection, -result, true);
				}
				return;
			}

			if (result > 0 && result <= buffer->data.Count() - offset)
			{
				auto nextOffset = offset + result;
				if (nextOffset == buffer->data.Count())
				{
					bool deliver = false;
					CS_LOCK(connection->lockState)
					{
						if (connection->writeOperationId == id)
						{
							connection->writeOperationId = 0;
							connection->writeCancelRequested = false;
							if (!connection->stopping && connection->connected)
							{
								connection->writePending = false;
								deliver = true;
							}
						}
					}
					if (deliver)
					{
						connection->InvokeCallback(false, [&](IAsyncSocketCallback* installed)
						{
							installed->OnWriteCompleted(buffer);
						});
					}
				}
				else
				{
					auto nextId = connection->runtime->ReserveOperationId();
					auto next = new WriteOperation(nextId, connection, fileDescriptor, buffer, nextOffset);
					bool attempted = false;
					bool submissionFailed = false;
					CS_LOCK(connection->lockState)
					{
						if (connection->writeOperationId == id && !connection->stopping && connection->connected)
						{
							attempted = true;
							connection->writeOperationId = nextId;
							connection->writeCancelRequested = false;
							connection->BeginTargetLocked();
							if (!connection->runtime->Submit(next))
							{
								connection->writeOperationId = 0;
								connection->RollbackTargetLocked();
								submissionFailed = true;
							}
						}
					}
					if (attempted && !submissionFailed)
					{
						connection->runtime->FlushPendingSubmissions();
					}
					if (!attempted)
					{
						delete next;
					}
					else if (submissionFailed)
					{
						connection->BeginTerminal(connection, EIO, true);
					}
				}
			}
			else
			{
				bool active = false;
				CS_LOCK(connection->lockState)
				{
					if (connection->writeOperationId == id)
					{
						connection->writeOperationId = 0;
						connection->writeCancelRequested = false;
						active = !connection->stopping && connection->connected;
					}
				}
				if (active)
				{
					auto error = result < 0 ? -result : EPIPE;
					connection->BeginTerminal(connection, error, true);
				}
			}
		}
	};

	class ConnectionState::ConnectOperation : public RingOperation
	{
	public:
		Ptr<ConnectionState>					connection;
		OwnedFileDescriptor					ownedSocket;
		sockaddr_in							address = {};

		ConnectOperation(vuint64_t id, Ptr<ConnectionState> _connection, vint _fileDescriptor, vint port)
			: RingOperation(id, _connection, true)
			, connection(_connection)
			, ownedSocket(_fileDescriptor)
		{
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = htons((vuint16_t)port);
		}

		void Prepare(io_uring_sqe* sqe) noexcept override
		{
			io_uring_prep_connect(sqe, (int)ownedSocket.Get(), (sockaddr*)&address, sizeof(address));
		}

		void Handle(vint result) override
		{
			bool accepted = false;
			bool failed = false;
			bool fatal = false;
			CS_LOCK(connection->lockState)
			{
				if (connection->connectOperationId == id)
				{
					connection->connectOperationId = 0;
					connection->connectCancelRequested = false;
					if (!connection->stopping && connection->clientStatus == ClientStatus::WaitingForServer)
					{
						if (result == 0)
						{
							connection->connected = true;
							connection->clientStatus = ClientStatus::Connected;
							ownedSocket.Detach();
							accepted = true;
						}
						else
						{
							connection->fileDescriptor = -1;
							failed = true;
							fatal = connection->clientAttempts >= AsyncSocketClientRetryCount;
						}
					}
					else
					{
						if (connection->fileDescriptor == ownedSocket.Get())
						{
							connection->fileDescriptor = -1;
						}
					}
				}
			}

			if (accepted)
			{
				connection->InvokeCallback(false, [](IAsyncSocketCallback* installed)
				{
					installed->OnConnected();
				});
				connection->eventWaitForServer.Signal();
			}
			else if (failed)
			{
				connection->DeliverConnectFailure(connection, result < 0 ? -result : EIO, fatal);
			}
		}
	};

	class ConnectionState::RetryOperation : public RingOperation
	{
	public:
		Ptr<ConnectionState>					connection;
		__kernel_timespec						timeout = {};

		RetryOperation(vuint64_t id, Ptr<ConnectionState> _connection)
			: RingOperation(id, _connection, true)
			, connection(_connection)
		{
			timeout.tv_sec = AsyncSocketClientRetryDelay / 1000;
			timeout.tv_nsec = (AsyncSocketClientRetryDelay % 1000) * 1000000;
		}

		void Prepare(io_uring_sqe* sqe) noexcept override
		{
			io_uring_prep_timeout(sqe, &timeout, 0, 0);
		}

		void Handle(vint result) override
		{
			bool active = false;
			CS_LOCK(connection->lockState)
			{
				if (connection->retryOperationId == id)
				{
					connection->retryOperationId = 0;
					connection->retryCancelRequested = false;
					active = !connection->stopping && connection->clientStatus == ClientStatus::WaitingForServer;
				}
			}
			if (!active)
			{
				return;
			}

			if (result == -ETIME)
			{
				connection->StartConnectAttempt(connection);
			}
			else if (result != -ECANCELED)
			{
				connection->BeginTerminal(connection, result < 0 ? -result : EIO, true);
			}
		}
	};

	void ConnectionState::InstallCallback(Ptr<ConnectionState>, IAsyncSocketCallback* value)
	{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::ConnectionState::InstallCallback(Ptr<ConnectionState>, IAsyncSocketCallback*)#"
		auto callbackDepth = CurrentCallbackDepth();
		if (!value)
		{
			CS_LOCK(lockState)
			{
				callback = nullptr;
				while (activeCallbacks > callbackDepth)
				{
					cvCallbacks.SleepWith(lockState);
				}
			}
			return;
		}

		bool canInstall = false;
		CS_LOCK(lockState)
		{
			canInstall = callback == nullptr && !stopping && owner != nullptr;
			if (canInstall)
			{
				callback = value;
				activeCallbacks++;
			}
		}
		CHECK_ERROR(canInstall, ERROR_MESSAGE_PREFIX L"Cannot replace a callback or install one on a stopped connection.");

		ConnectionCallbackFrame frame{ this, currentConnectionCallbackFrame };
		currentConnectionCallbackFrame = &frame;
		try
		{
			value->OnInstalled(owner);
		}
		catch (...)
		{
		}
		currentConnectionCallbackFrame = frame.previous;
		EndCallback();
#undef ERROR_MESSAGE_PREFIX
	}

	void ConnectionState::PostRead(Ptr<ConnectionState> retainedState)
	{
		auto operationId = runtime->ReserveOperationId();
		ReceiveOperation* operation = nullptr;
		bool submitted = false;
		bool submissionFailed = false;
		CS_LOCK(lockState)
		{
			if (connected && !stopping && !terminalPending && reading && readOperationId == 0)
			{
				operation = new ReceiveOperation(operationId, retainedState, fileDescriptor);
				readOperationId = operationId;
				readCancelRequested = false;
				BeginTargetLocked();
				submitted = runtime->Submit(operation);
				if (!submitted)
				{
					readOperationId = 0;
					RollbackTargetLocked();
					submissionFailed = true;
				}
			}
		}
		if (submitted)
		{
			runtime->FlushPendingSubmissions();
		}
		if (submissionFailed)
		{
			BeginTerminal(retainedState, EIO, true);
		}
	}

	void ConnectionState::BeginReading(Ptr<ConnectionState> retainedState)
	{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::ConnectionState::BeginReading(Ptr<ConnectionState>)#"
		CS_LOCK(lockState)
		{
			CHECK_ERROR(connected && !stopping && !terminalPending, ERROR_MESSAGE_PREFIX L"Requires a connected connection.");
			CHECK_ERROR(callback != nullptr, ERROR_MESSAGE_PREFIX L"Requires an installed callback.");
			CHECK_ERROR(!reading, ERROR_MESSAGE_PREFIX L"Can only be called once.");
			reading = true;
		}
		PostRead(retainedState);
#undef ERROR_MESSAGE_PREFIX
	}

	void ConnectionState::Write(Ptr<ConnectionState> retainedState, Ptr<AsyncSocketBuffer> buffer)
	{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::ConnectionState::Write(Ptr<ConnectionState>, Ptr<AsyncSocketBuffer>)#"
		CHECK_ERROR(buffer, ERROR_MESSAGE_PREFIX L"Requires a buffer.");
		auto operationId = runtime->ReserveOperationId();
		bool submitted = false;
		CS_LOCK(lockState)
		{
			CHECK_ERROR(connected && !stopping && !terminalPending && !writePending, ERROR_MESSAGE_PREFIX L"Requires a connected connection with no outstanding write.");
			auto operation = new WriteOperation(operationId, retainedState, fileDescriptor, buffer, 0);
			writePending = true;
			writeOperationId = operationId;
			writeCancelRequested = false;
			BeginTargetLocked();
			submitted = runtime->Submit(operation);
			if (!submitted)
			{
				writePending = false;
				writeOperationId = 0;
				RollbackTargetLocked();
			}
		}
		if (submitted)
		{
			runtime->FlushPendingSubmissions();
		}
		CHECK_ERROR(submitted, ERROR_MESSAGE_PREFIX L"Failed to submit the write operation.");
#undef ERROR_MESSAGE_PREFIX
	}

	void ConnectionState::BeginTerminal(Ptr<ConnectionState> retainedState, vint error, bool reportError)
	{
		bool claimed = false;
		CS_LOCK(lockState)
		{
			if (!stopping && !terminalPending)
			{
				terminalPending = true;
				claimed = true;
			}
		}
		if (!claimed)
		{
			return;
		}

		if (reportError)
		{
			InvokeCallback(true, [&](IAsyncSocketCallback* installed)
			{
				installed->OnError(LinuxSocketErrorMessage(L"Asynchronous socket operation", error), true);
			});
		}
		Stop(retainedState);
	}

	void ConnectionState::StartConnectAttempt(Ptr<ConnectionState> retainedState)
	{
		bool active = false;
		bool fatal = false;
		CS_LOCK(lockState)
		{
			if (clientMode && !stopping && clientStatus == ClientStatus::WaitingForServer && connectOperationId == 0 && retryOperationId == 0)
			{
				clientAttempts++;
				fatal = clientAttempts >= AsyncSocketClientRetryCount;
				active = true;
			}
		}
		if (!active)
		{
			return;
		}

		OwnedFileDescriptor createdSocket(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP));
		if (createdSocket.Get() < 0)
		{
			DeliverConnectFailure(retainedState, errno, fatal);
			return;
		}
		vint reuseAddress = 1;
		if (setsockopt(createdSocket.Get(), SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress)) != 0)
		{
			DeliverConnectFailure(retainedState, errno, fatal);
			return;
		}

		auto operationId = runtime->ReserveOperationId();
		auto createdSocketValue = createdSocket.Get();
		auto operation = new ConnectOperation(operationId, retainedState, createdSocketValue, clientPort);
		createdSocket.Detach();
		bool attempted = false;
		bool submissionFailed = false;
		CS_LOCK(lockState)
		{
			if (!stopping && clientStatus == ClientStatus::WaitingForServer && connectOperationId == 0 && retryOperationId == 0)
			{
				attempted = true;
				fileDescriptor = createdSocketValue;
				connectOperationId = operationId;
				connectCancelRequested = false;
				BeginTargetLocked();
				if (!runtime->Submit(operation))
				{
					fileDescriptor = -1;
					connectOperationId = 0;
					RollbackTargetLocked();
					submissionFailed = true;
				}
			}
		}
		if (attempted && !submissionFailed)
		{
			runtime->FlushPendingSubmissions();
		}
		if (!attempted)
		{
			delete operation;
		}
		else if (submissionFailed)
		{
			DeliverConnectFailure(retainedState, EIO, fatal);
		}
	}

	void ConnectionState::DeliverConnectFailure(Ptr<ConnectionState> retainedState, vint error, bool fatal)
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
			installed->OnError(LinuxSocketErrorMessage(L"io_uring connect", error), fatal);
		});
		if (fatal)
		{
			Stop(retainedState);
		}
		else
		{
			ScheduleRetry(retainedState);
		}
	}

	void ConnectionState::ScheduleRetry(Ptr<ConnectionState> retainedState)
	{
		auto operationId = runtime->ReserveOperationId();
		auto operation = new RetryOperation(operationId, retainedState);
		bool attempted = false;
		bool submissionFailed = false;
		CS_LOCK(lockState)
		{
			if (!stopping && clientStatus == ClientStatus::WaitingForServer && retryOperationId == 0 && connectOperationId == 0)
			{
				attempted = true;
				retryOperationId = operationId;
				retryCancelRequested = false;
				BeginTargetLocked();
				if (!runtime->Submit(operation))
				{
					retryOperationId = 0;
					RollbackTargetLocked();
					submissionFailed = true;
				}
			}
		}
		if (attempted && !submissionFailed)
		{
			runtime->FlushPendingSubmissions();
		}
		if (!attempted)
		{
			delete operation;
		}
		else if (submissionFailed)
		{
			BeginTerminal(retainedState, EIO, true);
		}
	}

	void ConnectionState::Stop(Ptr<ConnectionState> retainedState)
	{
		bool firstStop = false;
		bool cancellationSubmitted = false;
		CS_LOCK(lockState)
		{
			if (!stopping)
			{
				stopping = true;
				firstStop = true;
				connected = false;
				reading = false;
				writePending = false;
				terminalPending = false;
				if (clientMode)
				{
					clientStatus = ClientStatus::Disconnected;
				}
			}

			auto collect = [&](vuint64_t operationId, bool& cancelRequested)
			{
				if (operationId != 0 && !cancelRequested)
				{
					auto operation = new CancelOperation(runtime->ReserveOperationId(), retainedState, operationId);
					cancelRequested = true;
					BeginCancelLocked();
					if (!runtime->Submit(operation))
					{
						cancelRequested = false;
						RollbackCancelLocked();
					}
					else
					{
						cancellationSubmitted = true;
					}
				}
			};
			collect(readOperationId, readCancelRequested);
			collect(writeOperationId, writeCancelRequested);
			collect(connectOperationId, connectCancelRequested);
			collect(retryOperationId, retryCancelRequested);

			if (firstStop && fileDescriptor >= 0)
			{
				shutdown((int)fileDescriptor, SHUT_RDWR);
				if (targetOperations == 0)
				{
					CloseFileDescriptor(fileDescriptor);
					fileDescriptor = -1;
				}
			}
		}
		if (cancellationSubmitted)
		{
			runtime->FlushPendingSubmissions();
		}

		if (currentRingRuntime != runtime.Obj())
		{
			operationDrain->Wait();
		}

		auto callbackDepth = CurrentCallbackDepth();
		IAsyncSocketCallback* installed = nullptr;
		lockState.Enter();
		while (activeCallbacks > callbackDepth)
		{
			cvCallbacks.SleepWith(lockState);
		}
		if (!disconnectedNotified)
		{
			disconnectedNotified = true;
			if (callback)
			{
				installed = callback;
				activeCallbacks++;
			}
		}
		lockState.Leave();

		if (installed)
		{
			ConnectionCallbackFrame frame{ this, currentConnectionCallbackFrame };
			currentConnectionCallbackFrame = &frame;
			try
			{
				installed->OnDisconnected();
			}
			catch (...)
			{
			}
			currentConnectionCallbackFrame = frame.previous;
			EndCallback();
		}

		if (callbackDepth == 0)
		{
			CS_LOCK(lockState)
			{
				while (activeCallbacks > 0)
				{
					cvCallbacks.SleepWith(lockState);
				}
			}
		}
		if (clientMode)
		{
			eventWaitForServer.Signal();
		}
	}

	void ConnectionState::WaitForServer(Ptr<ConnectionState> retainedState)
	{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::ConnectionState::WaitForServer(Ptr<ConnectionState>)#"
		bool begin = false;
		CS_LOCK(lockState)
		{
			if (clientMode && clientStatus == ClientStatus::Ready && !stopping)
			{
				clientStatus = ClientStatus::WaitingForServer;
				begin = true;
			}
		}
		CHECK_ERROR(begin, ERROR_MESSAGE_PREFIX L"Can only be called once while the client is ready.");
		StartConnectAttempt(retainedState);
		eventWaitForServer.Wait();
#undef ERROR_MESSAGE_PREFIX
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

	class ServerState : public RingOperationOwner
	{
	private:
		class AcceptOperation;
		class CancelOperation;

		Ptr<RingRuntime>					runtime;
		AsyncSocketServer*					owner = nullptr;
		vint								port = 0;

		// covers all fields below, callback counts, and target operation counts
		CriticalSection					lockState;
		ConditionVariable				cvCallbacks;
		vint								listener = -1;
		bool								startCalled = false;
		bool								starting = false;
		bool								started = false;
		bool								stopping = false;
		bool								stopped = false;
		EventObject						eventStartFinished;
		vuint64_t						acceptOperationId = 0;
		bool								acceptCancelRequested = false;
		vint								targetOperations = 0;
		vint								activeCallbacks = 0;
		List<Ptr<AsyncSocketConnection>>		connections;

		void FinishStart()
		{
			CS_LOCK(lockState)
			{
				starting = false;
				eventStartFinished.Signal();
			}
		}

		class StartScope
		{
		private:
			ServerState*						server = nullptr;

		public:
			StartScope(ServerState* _server)
				: server(_server)
			{
			}

			~StartScope()
			{
				server->FinishStart();
			}
		};

		void BeginTargetLocked()
		{
			targetOperations++;
			operationDrain->Begin();
		}

		void BeginCancelLocked()
		{
			operationDrain->Begin();
		}

		void RollbackTargetLocked()
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::ServerState::RollbackTargetLocked()#"
			CHECK_ERROR(targetOperations > 0, ERROR_MESSAGE_PREFIX L"Target-operation bookkeeping became unbalanced.");
			targetOperations--;
			operationDrain->End();
#undef ERROR_MESSAGE_PREFIX
		}

		void RollbackCancelLocked()
		{
			operationDrain->End();
		}

		void EndTargetOperation() override
		{
			CS_LOCK(lockState)
			{
				if (targetOperations <= 0)
				{
					std::abort();
				}
				targetOperations--;
				if (stopping && targetOperations == 0 && listener >= 0)
				{
					CloseFileDescriptor(listener);
					listener = -1;
				}
			}
		}

		void HandleOperationFailure(Ptr<RingOperationOwner> retainedOwner) override
		{
			auto retainedState = retainedOwner.Cast<ServerState>();
			if (!retainedState)
			{
				std::abort();
			}
			Stop(retainedState);
		}

		vint CurrentCallbackDepth()
		{
			vint result = 0;
			for (auto frame = currentServerCallbackFrame; frame; frame = frame->previous)
			{
				if (frame->server == this)
				{
					result++;
				}
			}
			return result;
		}

		void EndCallback()
		{
			CS_LOCK(lockState)
			{
				activeCallbacks--;
				cvCallbacks.WakeAllPendings();
			}
		}

		bool PostAccept(Ptr<ServerState> retainedState);

	public:
		ServerState(Ptr<RingRuntime> _runtime, AsyncSocketServer* _owner, vint _port)
			: runtime(_runtime)
			, owner(_owner)
			, port(_port)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::ServerState::ServerState(Ptr<RingRuntime>, AsyncSocketServer*, vint)#"
			CHECK_ERROR(eventStartFinished.CreateManualUnsignal(true), ERROR_MESSAGE_PREFIX L"Failed to create the server startup drain event.");
#undef ERROR_MESSAGE_PREFIX
		}

		void Start(Ptr<ServerState> retainedState);
		void Stop(Ptr<ServerState> retainedState);
		bool IsStopped();
	};

	class ServerState::CancelOperation : public RingOperation
	{
	public:
		Ptr<ServerState>						server;
		vuint64_t							targetId = 0;

		CancelOperation(vuint64_t id, Ptr<ServerState> _server, vuint64_t _targetId)
			: RingOperation(id, _server, false)
			, server(_server)
			, targetId(_targetId)
		{
		}

		void Prepare(io_uring_sqe* sqe) noexcept override
		{
			io_uring_prep_cancel64(sqe, targetId, 0);
		}

		void Handle(vint) override
		{
			CS_LOCK(server->lockState)
			{
			}
		}
	};

	class ServerState::AcceptOperation : public RingOperation
	{
	public:
		Ptr<ServerState>						server;
		vint								listener = -1;
		sockaddr_storage						address = {};
		socklen_t							addressSize = sizeof(address);

		AcceptOperation(vuint64_t id, Ptr<ServerState> _server, vint _listener)
			: RingOperation(id, _server, true)
			, server(_server)
			, listener(_listener)
		{
		}

		void Prepare(io_uring_sqe* sqe) noexcept override
		{
			io_uring_prep_accept(sqe, (int)listener, (sockaddr*)&address, &addressSize, SOCK_CLOEXEC);
		}

		void Handle(vint result) override
		{
			bool running = false;
			CS_LOCK(server->lockState)
			{
				if (server->acceptOperationId == id)
				{
					server->acceptOperationId = 0;
					server->acceptCancelRequested = false;
					running = server->started && !server->stopping;
				}
			}

			if (result < 0)
			{
				bool recoverable = false;
				switch (-result)
				{
				case EINTR:
				case EAGAIN:
				case ECONNABORTED:
				case EPROTO:
				case ENETDOWN:
				case ENOPROTOOPT:
				case EHOSTDOWN:
				case ENONET:
				case EHOSTUNREACH:
				case EOPNOTSUPP:
				case ENETUNREACH:
					recoverable = true;
				}
				if (running && recoverable)
				{
					if (!server->PostAccept(server))
					{
						server->Stop(server);
					}
				}
				else if (running && result != -ECANCELED)
				{
					server->Stop(server);
				}
				return;
			}

			OwnedFileDescriptor acceptedSocket(result);
			if (!running)
			{
				return;
			}

			// Keep exactly one accept pending, and rearm before invoking user code.
			if (!server->PostAccept(server))
			{
				server->Stop(server);
				return;
			}
			auto connectionState = Ptr(new ConnectionState(server->runtime, acceptedSocket.Get()));
			auto connection = Ptr(new AsyncSocketConnection(connectionState));
			acceptedSocket.Detach();
			bool invoke = false;
			CS_LOCK(server->lockState)
			{
				if (server->started && !server->stopping && server->owner)
				{
					server->connections.Add(connection);
					server->activeCallbacks++;
					invoke = true;
				}
			}
			if (!invoke)
			{
				connection->Stop();
				return;
			}

			WaitForClientResult acceptResult = WaitForClientResult::Reject;
			ServerCallbackFrame frame{ server.Obj(), currentServerCallbackFrame };
			currentServerCallbackFrame = &frame;
			try
			{
				acceptResult = server->owner->OnClientConnected(connection.Obj());
			}
			catch (...)
			{
			}
			currentServerCallbackFrame = frame.previous;
			server->EndCallback();

			bool stillRunning = false;
			CS_LOCK(server->lockState)
			{
				stillRunning = server->started && !server->stopping;
			}
			if (acceptResult == WaitForClientResult::Reject || !stillRunning)
			{
				connection->Stop();
			}
		}
	};

	bool ServerState::PostAccept(Ptr<ServerState> retainedState)
	{
		auto operationId = runtime->ReserveOperationId();
		AcceptOperation* operation = nullptr;
		bool submitted = false;
		bool submissionFailed = false;
		CS_LOCK(lockState)
		{
			if (started && !stopping && listener >= 0 && acceptOperationId == 0)
			{
				operation = new AcceptOperation(operationId, retainedState, listener);
				acceptOperationId = operationId;
				acceptCancelRequested = false;
				BeginTargetLocked();
				submitted = runtime->Submit(operation);
				if (!submitted)
				{
					acceptOperationId = 0;
					RollbackTargetLocked();
					submissionFailed = true;
				}
			}
		}
		if (submitted)
		{
			runtime->FlushPendingSubmissions();
		}
		return !submissionFailed;
	}

	void ServerState::Start(Ptr<ServerState> retainedState)
	{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::ServerState::Start(Ptr<ServerState>)#"
		CS_LOCK(lockState)
		{
			CHECK_ERROR(!startCalled && !stopping, ERROR_MESSAGE_PREFIX L"Can only be called once before stopping.");
			startCalled = true;
			starting = true;
			eventStartFinished.Unsignal();
		}
		StartScope startScope(this);

		OwnedFileDescriptor createdListener(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP));
		vint setupError = createdListener.Get() < 0 ? errno : 0;
		if (setupError == 0)
		{
			vint reuseAddress = 1;
			if (setsockopt(createdListener.Get(), SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress)) != 0)
			{
				setupError = errno;
			}
		}

		sockaddr_in address = {};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons((vuint16_t)port);
		if (setupError == 0 && bind(createdListener.Get(), (sockaddr*)&address, sizeof(address)) != 0)
		{
			setupError = errno;
		}
		if (setupError == 0 && listen(createdListener.Get(), SOMAXCONN) != 0)
		{
			setupError = errno;
		}
		if (setupError != 0)
		{
			CS_LOCK(lockState)
			{
				stopping = true;
				stopped = true;
			}
			runtime->Stop();
			CHECK_FAIL(ERROR_MESSAGE_PREFIX L"Failed to create the loopback listener.");
		}

		bool committed = false;
		try
		{
			CS_LOCK(lockState)
			{
				if (!stopping)
				{
					runtime->Start(runtime);
					listener = createdListener.Detach();
					started = true;
					committed = true;
				}
			}
		}
		catch (...)
		{
			CS_LOCK(lockState)
			{
				started = false;
				stopping = true;
				stopped = true;
			}
			runtime->Stop();
			throw;
		}
		if (committed)
		{
			if (!PostAccept(retainedState))
			{
				CS_LOCK(lockState)
				{
					started = false;
					stopping = true;
					stopped = true;
					CloseFileDescriptor(listener);
					listener = -1;
				}
				runtime->Stop();
				CHECK_FAIL(ERROR_MESSAGE_PREFIX L"Failed to submit the first accept operation.");
			}
		}
#undef ERROR_MESSAGE_PREFIX
	}

	void ServerState::Stop(Ptr<ServerState> retainedState)
	{
		bool waitForStart = false;
		bool cancellationSubmitted = false;
		CS_LOCK(lockState)
		{
			if (!stopping)
			{
				stopping = true;
				stopped = true;
				started = false;
			}
			waitForStart = starting;
		}
		if (waitForStart)
		{
			eventStartFinished.Wait();
		}

		CS_LOCK(lockState)
		{
			if (acceptOperationId != 0 && !acceptCancelRequested)
			{
				auto operation = new CancelOperation(runtime->ReserveOperationId(), retainedState, acceptOperationId);
				acceptCancelRequested = true;
				BeginCancelLocked();
				if (!runtime->Submit(operation))
				{
					acceptCancelRequested = false;
					RollbackCancelLocked();
				}
				else
				{
					cancellationSubmitted = true;
				}
			}
			if (listener >= 0)
			{
				shutdown((int)listener, SHUT_RDWR);
				if (targetOperations == 0)
				{
					CloseFileDescriptor(listener);
					listener = -1;
				}
			}
		}
		if (cancellationSubmitted)
		{
			runtime->FlushPendingSubmissions();
		}

		if (currentRingRuntime != runtime.Obj())
		{
			operationDrain->Wait();
		}

		auto callbackDepth = CurrentCallbackDepth();
		CS_LOCK(lockState)
		{
			while (activeCallbacks > callbackDepth)
			{
				cvCallbacks.SleepWith(lockState);
			}
		}

		List<Ptr<AsyncSocketConnection>> retainedConnections;
		CS_LOCK(lockState)
		{
			for (auto connection : connections)
			{
				retainedConnections.Add(connection);
			}
		}
		for (auto connection : retainedConnections)
		{
			connection->Stop();
		}
		runtime->Stop();
	}

	bool ServerState::IsStopped()
	{
		bool result = false;
		CS_LOCK(lockState)
		{
			result = stopped;
		}
		return result;
	}

	class AsyncSocketServer::Impl : public Object
	{
	private:
		Ptr<RingRuntime>					runtime;
		Ptr<ServerState>					state;

	public:
		Impl(AsyncSocketServer* owner, vint port)
			: runtime(RingRuntime::Create(false))
			, state(Ptr(new ServerState(runtime, owner, port)))
		{
		}

		~Impl()
		{
			Stop();
		}

		void Start()
		{
			state->Start(state);
		}

		void Stop()
		{
			state->Stop(state);
		}

		bool IsStopped()
		{
			return state->IsStopped();
		}
	};

	AsyncSocketServer::AsyncSocketServer(vint port)
	{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::AsyncSocketServer::AsyncSocketServer(vint)#"
		CHECK_ERROR(1 <= port && port <= 65535, ERROR_MESSAGE_PREFIX L"The port must be in 1..65535.");
#undef ERROR_MESSAGE_PREFIX
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

	class AsyncSocketClient::Impl : public Object
	{
	private:
		Ptr<RingRuntime>					runtime;
		Ptr<ConnectionState>				state;
		Ptr<AsyncSocketConnection>		connection;

	public:
		Impl(vint port)
			: runtime(RingRuntime::Create(true))
			, state(Ptr(new ConnectionState(runtime, true, port)))
			, connection(Ptr(new AsyncSocketConnection(state)))
		{
		}

		~Impl()
		{
			Stop();
		}

		void Stop()
		{
			connection->Stop();
			runtime->Stop();
		}

		IAsyncSocketConnection* GetConnection()
		{
			return connection.Obj();
		}

		void WaitForServer()
		{
			state->WaitForServer(state);
		}

		ClientStatus GetStatus()
		{
			return state->GetStatus();
		}
	};

	AsyncSocketClient::AsyncSocketClient(vint port)
	{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::linux_socket::AsyncSocketClient::AsyncSocketClient(vint)#"
		CHECK_ERROR(1 <= port && port <= 65535, ERROR_MESSAGE_PREFIX L"The port must be in 1..65535.");
#undef ERROR_MESSAGE_PREFIX
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
