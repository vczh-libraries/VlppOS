#include "AsyncSocket.macOS.h"
#if defined VCZH_GCC && defined VCZH_APPLE
#include "../../Threading.h"

#include <Network/Network.h>
#include <dispatch/dispatch.h>
#include <cstdlib>
#include <cstring>

#if !defined VCZH_GCC || !defined VCZH_APPLE
static_assert(false, "Do not build this file for non-macOS applications.");
#endif

namespace vl::inter_process::async_tcp_socket::macos_socket
{
	using namespace collections;

	class ConnectionState;
	class AsyncSocketConnection;

	struct CallbackFrame
	{
		ConnectionState*					connection = nullptr;
		CallbackFrame*					previous = nullptr;
	};

	static thread_local CallbackFrame* currentCallbackFrame = nullptr;
	static char connectionQueueKey;
	static char serverQueueKey;

/***********************************************************************
NativeConnectionContext
***********************************************************************/

	class NativeConnectionContext : public Object
	{
		friend class ConnectionState;
	private:
		Ptr<ConnectionState>				state;
		nw_connection_t					connection = nullptr;
		vint							generation = 0;
		bool							started = false;
		bool							cancelRequested = false;
		bool							waitingHandled = false;
		bool							cancelledHandled = false;

	public:
		NativeConnectionContext(Ptr<ConnectionState> _state, nw_connection_t _connection);
		~NativeConnectionContext();
	};

	NativeConnectionContext::NativeConnectionContext(Ptr<ConnectionState> _state, nw_connection_t _connection)
		: state(_state)
		, connection(_connection)
	{
	}

	NativeConnectionContext::~NativeConnectionContext()
	{
		if (connection)
		{
			nw_release(connection);
		}
	}

/***********************************************************************
ConnectionState
***********************************************************************/

	class ConnectionState : public Object
	{
		friend class NativeConnectionContext;
		friend class AsyncSocketConnection;
	private:
		// covers all fields below
		CriticalSection					lockState;
		ConditionVariable				cvState;
		dispatch_queue_t				queue = nullptr;
		IAsyncSocketConnection*			owner = nullptr;
		IAsyncSocketCallback*			callback = nullptr;
		bool							clientMode = false;
		vint							port = 0;
		bool							logicalConnected = false;
		bool							nativeReady = false;
		bool							stopping = false;
		bool							terminalPending = false;
		bool							disconnectedNotified = false;
		vint							activeCallbacks = 0;
		bool							callbackExecuting = false;
		bool							readingRequested = false;
		bool							receivePending = false;
		bool							receiveHandling = false;
		vint							receiveGeneration = -1;
		Ptr<AsyncSocketBuffer>			pendingReadBuffer;
		bool							writePending = false;
		bool							writeIssued = false;
		vint							writeGeneration = -1;
		Ptr<AsyncSocketBuffer>			writeBuffer;
		dispatch_data_t					writeData = nullptr;
		vint							pendingLocalTasks = 0;
		Ptr<NativeConnectionContext>		nativeContext;
		vint							pendingNativeContexts = 0;
		vint							nextGeneration = 0;
		ClientStatus					clientStatus = ClientStatus::Ready;
		vint							clientAttempts = 0;
		vint							retryAfterGeneration = -1;
		dispatch_source_t				retryTimer = nullptr;
		vint							retryTimerGeneration = -1;
		bool							retryTimerFired = false;
		bool							retryTimerCancelRequested = false;
		vint							pendingRetryTimers = 0;
		EventObject						eventWaitForServer;

		vint CountCurrentCallbackFrames()
		{
			vint count = 0;
			for (auto frame = currentCallbackFrame; frame; frame = frame->previous)
			{
				if (frame->connection == this)
				{
					count++;
				}
			}
			return count;
		}

		bool IsOnQueue()
		{
			return dispatch_get_specific(&connectionQueueKey) == this;
		}

		enum class CallbackType
		{
			Ordinary,
			FatalError,
			Disconnected,
		};

		bool CanInvokeCallbackLocked(CallbackType type)
		{
			if (!callback)
			{
				return false;
			}
			switch (type)
			{
			case CallbackType::Ordinary:
				return !stopping && !terminalPending;
			case CallbackType::FatalError:
				return !stopping;
			case CallbackType::Disconnected:
				return true;
			default:
				return false;
			}
		}

		IAsyncSocketCallback* BeginCallback(CallbackType type, bool& ownsExecution)
		{
			IAsyncSocketCallback* installed = nullptr;
			auto nested = CountCurrentCallbackFrames() > 0;
			ownsExecution = false;
			lockState.Enter();
			while (!nested && callbackExecuting && CanInvokeCallbackLocked(type))
			{
				cvState.SleepWith(lockState);
			}
			if (CanInvokeCallbackLocked(type))
			{
				installed = callback;
				activeCallbacks++;
				if (!nested)
				{
					callbackExecuting = true;
					ownsExecution = true;
				}
			}
			lockState.Leave();
			return installed;
		}

		void EndCallback(bool ownsExecution)
		{
			CS_LOCK(lockState)
			{
				activeCallbacks--;
				if (ownsExecution)
				{
					callbackExecuting = false;
				}
				cvState.WakeAllPendings();
			}
		}

		template<typename TCallback>
		bool InvokeCallback(CallbackType type, TCallback&& invoke)
		{
			bool ownsExecution = false;
			auto installed = BeginCallback(type, ownsExecution);
			if (!installed)
			{
				return false;
			}

			CallbackFrame frame{ this, currentCallbackFrame };
			currentCallbackFrame = &frame;
			try
			{
				invoke(installed);
			}
			catch (...)
			{
			}
			currentCallbackFrame = frame.previous;
			EndCallback(ownsExecution);
			return true;
		}

		void WaitForOtherCallbacks(vint currentFrames)
		{
			lockState.Enter();
			while (activeCallbacks > currentFrames)
			{
				cvState.SleepWith(lockState);
			}
			lockState.Leave();
		}

		void NotifyDisconnected()
		{
			bool notify = false;
			lockState.Enter();
			if (!disconnectedNotified)
			{
				disconnectedNotified = true;
				notify = callback != nullptr;
			}
			lockState.Leave();

			if (notify)
			{
				InvokeCallback(CallbackType::Disconnected, [](IAsyncSocketCallback* installed)
				{
					installed->OnDisconnected();
				});
			}
		}

		void WaitForDrain()
		{
			lockState.Enter();
			while (
				activeCallbacks > 0 ||
				pendingNativeContexts > 0 ||
				pendingRetryTimers > 0 ||
				pendingLocalTasks > 0
				)
			{
				cvState.SleepWith(lockState);
			}
			lockState.Leave();
		}

		void RequestNativeCancelLocked(Ptr<NativeConnectionContext> context)
		{
			if (!context || !context->connection || context->cancelRequested)
			{
				return;
			}
			if (!context->started)
			{
				context->started = true;
				nw_connection_start(context->connection);
			}
			context->cancelRequested = true;
			nw_connection_cancel(context->connection);
		}

		void RequestRetryTimerCancelLocked()
		{
			if (retryTimer && !retryTimerCancelRequested)
			{
				retryTimerCancelRequested = true;
				dispatch_source_cancel(retryTimer);
			}
		}

		WString NetworkErrorMessage(const wchar_t* operation, nw_error_t error)
		{
			if (!error)
			{
				return WString(operation) + L" failed without a Network.framework error.";
			}
			return WString(operation) + L" failed with Network.framework domain "
				+ itow((vint)nw_error_get_error_domain(error)) + L", code "
				+ itow((vint)nw_error_get_error_code(error)) + L".";
		}

		void ConfigureNativeLocked(Ptr<NativeConnectionContext> context, bool start)
		{
			nativeContext = context;
			pendingNativeContexts++;
			context->generation = ++nextGeneration;
			nw_connection_set_queue(context->connection, queue);

			auto retainedContext = context;
			nw_connection_set_state_changed_handler(context->connection, ^(nw_connection_state_t state, nw_error_t error)
			{
				retainedContext->state->OnNativeState(retainedContext, state, error);
			});
			if (start)
			{
				context->started = true;
				nw_connection_start(context->connection);
			}
		}

		void IssueReceiveLocked(Ptr<NativeConnectionContext> context)
		{
			if (
				!context || context != nativeContext || !context->connection ||
				!logicalConnected || !nativeReady || stopping || terminalPending ||
				!callback || !readingRequested || receivePending || receiveHandling || pendingReadBuffer
				)
			{
				return;
			}

			receivePending = true;
			receiveGeneration = context->generation;
			auto retainedContext = context;
			nw_connection_receive(context->connection, 1, 65536, ^(dispatch_data_t content, nw_content_context_t, bool isComplete, nw_error_t error)
			{
				retainedContext->state->OnReceive(retainedContext, content, isComplete, error);
			});
		}

		void IssueWriteLocked(Ptr<NativeConnectionContext> context)
		{
			if (
				!context || context != nativeContext || !context->connection ||
				!logicalConnected || !nativeReady || stopping || terminalPending ||
				!writePending || writeIssued
				)
			{
				return;
			}

			writeIssued = true;
			writeGeneration = context->generation;
			if (writeBuffer->data.Count() == 0)
			{
				pendingLocalTasks++;
				auto retainedState = context->state;
				auto generation = context->generation;
				dispatch_async(queue, ^
				{
					retainedState->CompleteEmptyWrite(generation);
				});
			}
			else
			{
				auto retainedContext = context;
				nw_connection_send(
					context->connection,
					writeData,
					NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT,
					true,
					^(nw_error_t error)
					{
						retainedContext->state->OnSendCompleted(retainedContext, error);
					}
				);
			}
		}

		void EnterTerminal(Ptr<NativeConnectionContext> context, const WString& error, bool reportError)
		{
			bool claim = false;
			CS_LOCK(lockState)
			{
				if (!stopping && !terminalPending && (!context || context == nativeContext))
				{
					terminalPending = true;
					claim = true;
				}
			}
			if (!claim)
			{
				return;
			}

			if (reportError)
			{
				InvokeCallback(CallbackType::FatalError, [&](IAsyncSocketCallback* installed)
				{
					installed->OnError(error, true);
				});
			}
			Stop();
		}

		void StartClientAttempt(Ptr<ConnectionState> retainedState)
		{
			bool canStart = false;
			CS_LOCK(lockState)
			{
				canStart = clientMode && !stopping && clientStatus == ClientStatus::WaitingForServer && !nativeContext;
			}
			if (!canStart)
			{
				return;
			}

			auto portText = itoa(port);
			auto endpoint = nw_endpoint_create_host("127.0.0.1", portText.Buffer());
			auto parameters = nw_parameters_create_secure_tcp(
				NW_PARAMETERS_DISABLE_PROTOCOL,
				NW_PARAMETERS_DEFAULT_CONFIGURATION
			);
			nw_connection_t connection = nullptr;
			if (endpoint && parameters)
			{
				connection = nw_connection_create(endpoint, parameters);
			}
			if (endpoint)
			{
				nw_release(endpoint);
			}
			if (parameters)
			{
				nw_release(parameters);
			}

			if (!connection)
			{
				EnterTerminal(nullptr, L"AsyncSocketClient failed to create a Network.framework connection.", true);
				return;
			}

			auto context = Ptr(new NativeConnectionContext(retainedState, connection));
			bool installed = false;
			lockState.Enter();
			if (clientMode && !stopping && clientStatus == ClientStatus::WaitingForServer && !nativeContext)
			{
				clientAttempts++;
				ConfigureNativeLocked(context, true);
				installed = true;
			}
			lockState.Leave();
			if (!installed)
			{
				return;
			}
		}

		void ScheduleRetry(Ptr<ConnectionState> retainedState, vint generation)
		{
			auto source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
			if (!source)
			{
				EnterTerminal(nullptr, L"AsyncSocketClient failed to create its retry timer.", true);
				return;
			}

			auto retainedForEvent = retainedState;
			dispatch_source_set_event_handler(source, ^
			{
				bool cancel = false;
				retainedForEvent->lockState.Enter();
				if (retainedForEvent->retryTimer == source && !retainedForEvent->retryTimerCancelRequested)
				{
					retainedForEvent->retryTimerFired = true;
					retainedForEvent->retryTimerCancelRequested = true;
					cancel = true;
				}
				retainedForEvent->lockState.Leave();
				if (cancel)
				{
					dispatch_source_cancel(source);
				}
			});

			auto retainedForCancel = retainedState;
			dispatch_source_set_cancel_handler(source, ^
			{
				auto retainedForSentinel = retainedForCancel;
				dispatch_async(retainedForCancel->queue, ^
				{
					retainedForSentinel->FinishRetryTimer(retainedForSentinel, source, generation);
				});
			});
			dispatch_source_set_timer(
				source,
				dispatch_time(DISPATCH_TIME_NOW, (int64_t)AsyncSocketClientRetryDelay * NSEC_PER_MSEC),
				DISPATCH_TIME_FOREVER,
				0
			);

			bool installed = false;
			lockState.Enter();
			if (
				clientMode && !stopping && clientStatus == ClientStatus::WaitingForServer &&
				retryAfterGeneration == generation && !retryTimer
				)
			{
				retryTimer = source;
				retryTimerGeneration = generation;
				retryTimerFired = false;
				retryTimerCancelRequested = false;
				pendingRetryTimers++;
				installed = true;
			}
			lockState.Leave();

			if (installed)
			{
				dispatch_activate(source);
			}
			else
			{
				dispatch_source_set_event_handler(source, nullptr);
				dispatch_source_set_cancel_handler(source, nullptr);
				dispatch_activate(source);
				dispatch_source_cancel(source);
				dispatch_release(source);
			}
		}

		void FinishRetryTimer(Ptr<ConnectionState> retainedState, dispatch_source_t source, vint generation)
		{
			bool start = false;
			lockState.Enter();
			if (retryTimer == source)
			{
				start = retryTimerFired && !stopping && clientStatus == ClientStatus::WaitingForServer && retryTimerGeneration == generation;
				retryTimer = nullptr;
				retryTimerGeneration = -1;
				retryTimerFired = false;
				retryTimerCancelRequested = false;
			}
			lockState.Leave();

			dispatch_release(source);
			if (start)
			{
				StartClientAttempt(retainedState);
			}

			CS_LOCK(lockState)
			{
				pendingRetryTimers--;
				cvState.WakeAllPendings();
			}
		}

		void OnNativeReady(Ptr<NativeConnectionContext> context)
		{
			bool notifyConnected = false;
			lockState.Enter();
			if (
				context == nativeContext && !context->cancelRequested &&
				retryAfterGeneration != context->generation &&
				!stopping && !terminalPending
				)
			{
				nativeReady = true;
				logicalConnected = true;
				if (clientMode && clientStatus == ClientStatus::WaitingForServer)
				{
					clientStatus = ClientStatus::Connected;
					notifyConnected = true;
				}
			}
			lockState.Leave();

			if (notifyConnected)
			{
				InvokeCallback(CallbackType::Ordinary, [](IAsyncSocketCallback* installed)
				{
					installed->OnConnected();
				});
				eventWaitForServer.Signal();
			}

			lockState.Enter();
			if (context == nativeContext && nativeReady && logicalConnected && !stopping && !terminalPending)
			{
				IssueReceiveLocked(context);
				IssueWriteLocked(context);
			}
			lockState.Leave();
		}

		void OnNativeWaiting(Ptr<NativeConnectionContext> context, nw_error_t error)
		{
			bool retryable = false;
			bool exhausted = false;
			lockState.Enter();
			if (
				clientMode && context == nativeContext && !context->waitingHandled &&
				!stopping && clientStatus == ClientStatus::WaitingForServer
				)
			{
				context->waitingHandled = true;
				exhausted = clientAttempts >= AsyncSocketClientRetryCount;
				retryable = !exhausted;
				if (retryable)
				{
					retryAfterGeneration = context->generation;
				}
			}
			lockState.Leave();
			if (!retryable && !exhausted)
			{
				return;
			}

			auto message = NetworkErrorMessage(L"AsyncSocketClient connection attempt", error);
			if (exhausted)
			{
				EnterTerminal(context, message, true);
				return;
			}

			InvokeCallback(CallbackType::Ordinary, [&](IAsyncSocketCallback* installed)
			{
				installed->OnError(message, false);
			});

			lockState.Enter();
			if (
				context == nativeContext && retryAfterGeneration == context->generation &&
				!stopping && clientStatus == ClientStatus::WaitingForServer
				)
			{
				RequestNativeCancelLocked(context);
			}
			lockState.Leave();
		}

		void OnNativeCancelled(Ptr<NativeConnectionContext> context)
		{
			bool schedule = false;
			lockState.Enter();
			if (!context->cancelledHandled)
			{
				context->cancelledHandled = true;
				if (context->connection)
				{
					nw_connection_set_state_changed_handler(context->connection, nullptr);
					nw_release(context->connection);
					context->connection = nullptr;
				}
				schedule = true;
			}
			lockState.Leave();

			if (schedule)
			{
				auto retainedContext = context;
				dispatch_async(queue, ^
				{
					retainedContext->state->FinishNativeCancellation(retainedContext);
				});
			}
		}

		void FinishNativeCancellation(Ptr<NativeConnectionContext> context)
		{
			bool retry = false;
			lockState.Enter();
			if (nativeContext == context)
			{
				retry = clientMode && !stopping && clientStatus == ClientStatus::WaitingForServer && retryAfterGeneration == context->generation;
				nativeContext = nullptr;
				nativeReady = false;
				receivePending = false;
				receiveGeneration = -1;
			}
			lockState.Leave();

			if (retry)
			{
				ScheduleRetry(context->state, context->generation);
			}

			CS_LOCK(lockState)
			{
				pendingNativeContexts--;
				cvState.WakeAllPendings();
			}
		}

		void OnNativeState(Ptr<NativeConnectionContext> context, nw_connection_state_t state, nw_error_t error)
		{
			switch (state)
			{
			case nw_connection_state_waiting:
				OnNativeWaiting(context, error);
				break;
			case nw_connection_state_ready:
				OnNativeReady(context);
				break;
			case nw_connection_state_failed:
				{
					bool intentionalCancellation = false;
					CS_LOCK(lockState)
					{
						intentionalCancellation =
							context != nativeContext || context->cancelRequested ||
							retryAfterGeneration == context->generation || stopping;
					}
					if (!intentionalCancellation)
					{
						EnterTerminal(context, NetworkErrorMessage(L"Asynchronous socket connection", error), true);
					}
				}
				break;
			case nw_connection_state_cancelled:
				OnNativeCancelled(context);
				break;
			default:
				break;
			}
		}

		void ResumeReading()
		{
			while (true)
			{
				Ptr<AsyncSocketBuffer> buffered;
				lockState.Enter();
				if (
					pendingReadBuffer && callback && !receiveHandling &&
					logicalConnected && !stopping && !terminalPending
					)
				{
					buffered = pendingReadBuffer;
					pendingReadBuffer = nullptr;
					receiveHandling = true;
				}
				else
				{
					IssueReceiveLocked(nativeContext);
				}
				lockState.Leave();

				if (!buffered)
				{
					return;
				}

				auto invoked = InvokeCallback(CallbackType::Ordinary, [&](IAsyncSocketCallback* installed)
				{
					installed->OnRead(&buffered->data[0], buffered->data.Count());
				});

				bool continueReading = false;
				lockState.Enter();
				if (
					!invoked && !pendingReadBuffer && logicalConnected &&
					!stopping && !terminalPending
					)
				{
					pendingReadBuffer = buffered;
				}
				receiveHandling = false;
				continueReading = invoked && logicalConnected && !stopping && !terminalPending;
				lockState.Leave();
				if (!continueReading)
				{
					return;
				}
			}
		}

		void OnReceive(Ptr<NativeConnectionContext> context, dispatch_data_t content, bool isComplete, nw_error_t error)
		{
			bool claimed = false;
			bool deliver = false;
			lockState.Enter();
			if (context == nativeContext && receivePending && receiveGeneration == context->generation)
			{
				receivePending = false;
				receiveHandling = true;
				receiveGeneration = -1;
				claimed = true;
				deliver = !stopping && !terminalPending && logicalConnected;
			}
			lockState.Leave();

			if (!deliver)
			{
				if (claimed)
				{
					CS_LOCK(lockState)
					{
						receiveHandling = false;
					}
				}
				return;
			}

			auto undelivered = Ptr(new AsyncSocketBuffer);
			bool buffering = false;
			auto bufferingRef = &buffering;
			if (content)
			{
				dispatch_data_apply(content, ^bool(dispatch_data_t, size_t, const void* buffer, size_t size)
				{
					if (size == 0)
					{
						return true;
					}
					if (!*bufferingRef)
					{
						auto invoked = InvokeCallback(CallbackType::Ordinary, [&](IAsyncSocketCallback* installed)
						{
							installed->OnRead((const vuint8_t*)buffer, (vint)size);
						});
						if (invoked)
						{
							return true;
						}
						*bufferingRef = true;
					}

					auto oldSize = undelivered->data.Count();
					undelivered->data.Resize(oldSize + (vint)size);
					std::memcpy(&undelivered->data[oldSize], buffer, size);
					return true;
				});
			}

			if (error)
			{
				EnterTerminal(context, NetworkErrorMessage(L"Asynchronous socket receive", error), true);
			}
			else if (isComplete)
			{
				EnterTerminal(context, WString::Empty, false);
			}

			bool resume = false;
			lockState.Enter();
			if (
				!error && !isComplete && context == nativeContext &&
				!stopping && !terminalPending && logicalConnected
				)
			{
				if (undelivered->data.Count() > 0)
				{
					pendingReadBuffer = undelivered;
				}
				resume = true;
			}
			receiveHandling = false;
			lockState.Leave();
			if (resume)
			{
				ResumeReading();
			}
		}

		void OnSendCompleted(Ptr<NativeConnectionContext> context, nw_error_t error)
		{
			Ptr<AsyncSocketBuffer> completedBuffer;
			dispatch_data_t completedData = nullptr;
			bool reportCompletion = false;
			bool reportError = false;
			lockState.Enter();
			if (writePending && writeIssued && writeGeneration == context->generation)
			{
				completedBuffer = writeBuffer;
				completedData = writeData;
				writeBuffer = nullptr;
				writeData = nullptr;
				writePending = false;
				writeIssued = false;
				writeGeneration = -1;
				reportCompletion = !error && !stopping && !terminalPending && context == nativeContext;
				reportError = error && !stopping && !terminalPending && context == nativeContext;
			}
			lockState.Leave();

			if (completedData)
			{
				dispatch_release(completedData);
			}
			if (reportCompletion)
			{
				InvokeCallback(CallbackType::Ordinary, [&](IAsyncSocketCallback* installed)
				{
					installed->OnWriteCompleted(completedBuffer);
				});
			}
			else if (reportError)
			{
				EnterTerminal(context, NetworkErrorMessage(L"Asynchronous socket send", error), true);
			}
		}

		void CompleteEmptyWrite(vint generation)
		{
			Ptr<AsyncSocketBuffer> completedBuffer;
			bool reportCompletion = false;
			lockState.Enter();
			if (writePending && writeIssued && writeGeneration == generation)
			{
				completedBuffer = writeBuffer;
				writeBuffer = nullptr;
				writePending = false;
				writeIssued = false;
				writeGeneration = -1;
				reportCompletion = !stopping && !terminalPending && nativeContext && nativeContext->generation == generation;
			}
			lockState.Leave();

			if (reportCompletion)
			{
				InvokeCallback(CallbackType::Ordinary, [&](IAsyncSocketCallback* installed)
				{
					installed->OnWriteCompleted(completedBuffer);
				});
			}

			CS_LOCK(lockState)
			{
				pendingLocalTasks--;
				cvState.WakeAllPendings();
			}
		}

	public:
		ConnectionState(bool _clientMode, vint _port)
			: clientMode(_clientMode)
			, port(_port)
		{
			queue = dispatch_queue_create("vlppos.async-socket.connection", DISPATCH_QUEUE_SERIAL);
			CHECK_ERROR(queue != nullptr, L"IAsyncSocketConnection failed to create its dispatch queue.");
			try
			{
				CHECK_ERROR(eventWaitForServer.CreateManualUnsignal(false), L"IAsyncSocketClient failed to create its wait event.");
				dispatch_queue_set_specific(queue, &connectionQueueKey, this, nullptr);
			}
			catch (...)
			{
				dispatch_release(queue);
				queue = nullptr;
				throw;
			}
		}

		~ConnectionState()
		{
			if (writeData)
			{
				dispatch_release(writeData);
			}
			dispatch_queue_set_specific(queue, &connectionQueueKey, nullptr, nullptr);
			dispatch_release(queue);
		}

		void AttachOwner(IAsyncSocketConnection* value)
		{
			CS_LOCK(lockState)
			{
				owner = value;
			}
		}

		void DetachOwner(IAsyncSocketConnection* value)
		{
			CS_LOCK(lockState)
			{
				if (owner == value)
				{
					owner = nullptr;
				}
			}
		}

		bool RequiresDeferredDrain()
		{
			return CountCurrentCallbackFrames() > 0 || IsOnQueue();
		}

		void ConfigureServerNative(Ptr<ConnectionState> retainedState, nw_connection_t connection)
		{
			auto context = Ptr(new NativeConnectionContext(retainedState, connection));
			CS_LOCK(lockState)
			{
				CHECK_ERROR(!clientMode && !nativeContext && !stopping, L"The accepted async socket connection is already configured.");
				logicalConnected = true;
				ConfigureNativeLocked(context, false);
			}
		}

		void StartAcceptedConnection()
		{
			CS_LOCK(lockState)
			{
				if (nativeContext && !nativeContext->started && !stopping)
				{
					nativeContext->started = true;
					nw_connection_start(nativeContext->connection);
				}
			}
		}

		void InstallCallback(IAsyncSocketCallback* value)
		{
			if (!value)
			{
				auto currentFrames = CountCurrentCallbackFrames();
				lockState.Enter();
				callback = nullptr;
				cvState.WakeAllPendings();
				while (activeCallbacks > currentFrames)
				{
					cvState.SleepWith(lockState);
				}
				lockState.Leave();
				return;
			}

			IAsyncSocketConnection* installedOwner = nullptr;
			bool canInstall = false;
			bool ownsExecution = false;
			auto nested = CountCurrentCallbackFrames() > 0;
			lockState.Enter();
			while (!nested && callbackExecuting && !stopping)
			{
				cvState.SleepWith(lockState);
			}
			if (!callback && !stopping && owner)
			{
				callback = value;
				installedOwner = owner;
				activeCallbacks++;
				if (!nested)
				{
					callbackExecuting = true;
					ownsExecution = true;
				}
				canInstall = true;
			}
			lockState.Leave();
			CHECK_ERROR(canInstall, L"IAsyncSocketConnection::InstallCallback cannot replace a callback or install one on a stopped connection.");

			CallbackFrame frame{ this, currentCallbackFrame };
			currentCallbackFrame = &frame;
			try
			{
				value->OnInstalled(installedOwner);
			}
			catch (...)
			{
			}
			currentCallbackFrame = frame.previous;
			EndCallback(ownsExecution);
			ResumeReading();
		}

		void BeginReading()
		{
			CS_LOCK(lockState)
			{
				CHECK_ERROR(logicalConnected && !stopping && !terminalPending, L"IAsyncSocketConnection::BeginReadingLoopUnsafe requires a connected connection.");
				CHECK_ERROR(callback != nullptr, L"IAsyncSocketConnection::BeginReadingLoopUnsafe requires an installed callback.");
				CHECK_ERROR(!readingRequested, L"IAsyncSocketConnection::BeginReadingLoopUnsafe can only be called once.");
				readingRequested = true;
				IssueReceiveLocked(nativeContext);
			}
		}

		void Write(Ptr<AsyncSocketBuffer> buffer)
		{
			CHECK_ERROR(buffer, L"IAsyncSocketConnection::WriteAsync requires a buffer.");

			dispatch_data_t data = nullptr;
			if (buffer->data.Count() > 0)
			{
				auto bytes = std::malloc((size_t)buffer->data.Count());
				CHECK_ERROR(bytes != nullptr, L"IAsyncSocketConnection::WriteAsync failed to allocate native write storage.");
				std::memcpy(bytes, &buffer->data[0], (size_t)buffer->data.Count());
				data = dispatch_data_create(bytes, (size_t)buffer->data.Count(), nullptr, DISPATCH_DATA_DESTRUCTOR_FREE);
				if (!data)
				{
					std::free(bytes);
				}
				CHECK_ERROR(data != nullptr, L"IAsyncSocketConnection::WriteAsync failed to create native write data.");
			}

			bool canWrite = false;
			lockState.Enter();
			if (logicalConnected && !stopping && !terminalPending && !writePending)
			{
				writePending = true;
				writeBuffer = buffer;
				writeData = data;
				canWrite = true;
				IssueWriteLocked(nativeContext);
			}
			lockState.Leave();
			if (!canWrite && data)
			{
				dispatch_release(data);
			}
			CHECK_ERROR(canWrite, L"IAsyncSocketConnection::WriteAsync requires a connected connection with no outstanding write.");
		}

		void Stop()
		{
			auto currentFrames = CountCurrentCallbackFrames();
			auto deferredDrain = RequiresDeferredDrain();
			Ptr<AsyncSocketBuffer> cancelledBuffer;
			dispatch_data_t cancelledData = nullptr;

			lockState.Enter();
			if (!stopping)
			{
				stopping = true;
				terminalPending = true;
				logicalConnected = false;
				nativeReady = false;
				readingRequested = false;
				pendingReadBuffer = nullptr;
				if (clientMode)
				{
					clientStatus = ClientStatus::Disconnected;
				}
				if (writePending && !writeIssued)
				{
					cancelledBuffer = writeBuffer;
					cancelledData = writeData;
					writeBuffer = nullptr;
					writeData = nullptr;
					writePending = false;
					writeGeneration = -1;
				}
			}
			RequestRetryTimerCancelLocked();
			RequestNativeCancelLocked(nativeContext);
			cvState.WakeAllPendings();
			lockState.Leave();

			if (cancelledData)
			{
				dispatch_release(cancelledData);
			}
			if (clientMode)
			{
				eventWaitForServer.Signal();
			}

			WaitForOtherCallbacks(currentFrames);
			NotifyDisconnected();
			if (!deferredDrain)
			{
				WaitForDrain();
			}
		}

		void WaitForServer(Ptr<ConnectionState> retainedState)
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
			StartClientAttempt(retainedState);
			eventWaitForServer.Wait();
		}

		ClientStatus GetStatus()
		{
			ClientStatus result;
			CS_LOCK(lockState)
			{
				result = clientStatus;
			}
			return result;
		}
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
			state->AttachOwner(this);
		}

		~AsyncSocketConnection()
		{
			state->Stop();
			state->DetachOwner(this);
		}

		Ptr<ConnectionState> GetState()
		{
			return state;
		}

		void StartAcceptedConnection()
		{
			state->StartAcceptedConnection();
		}

		bool RequiresDeferredDrain()
		{
			return state->RequiresDeferredDrain();
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
ServerState
***********************************************************************/

	class ServerState : public Object
	{
	private:
		// covers all fields below
		CriticalSection					lockState;
		ConditionVariable				cvState;
		dispatch_queue_t				queue = nullptr;
		IAsyncSocketServerCallback*		callback = nullptr;
		vint							port = 0;
		bool							startCalled = false;
		bool							started = false;
		bool							stopping = false;
		bool							stopped = false;
		bool							stopFinalizing = false;
		bool							stopCompleted = false;
		nw_listener_t					listener = nullptr;
		bool							listenerCancelRequested = false;
		bool							listenerCancelledHandled = false;
		vint							pendingListener = 0;
		List<Ptr<AsyncSocketConnection>>	connections;

		bool IsOnQueue()
		{
			return dispatch_get_specific(&serverQueueKey) == this;
		}

		void RequestListenerCancelLocked()
		{
			if (listener && !listenerCancelRequested)
			{
				listenerCancelRequested = true;
				nw_listener_cancel(listener);
			}
		}

		void OnListenerCancelled(Ptr<ServerState> retainedState)
		{
			bool schedule = false;
			lockState.Enter();
			if (!listenerCancelledHandled)
			{
				listenerCancelledHandled = true;
				if (listener)
				{
					nw_listener_set_new_connection_handler(listener, nullptr);
					nw_listener_set_state_changed_handler(listener, nullptr);
					nw_release(listener);
					listener = nullptr;
				}
				schedule = true;
			}
			lockState.Leave();

			if (schedule)
			{
				dispatch_async(queue, ^
				{
					retainedState->FinishListenerCancellation();
				});
			}
		}

		void FinishListenerCancellation()
		{
			CS_LOCK(lockState)
			{
				pendingListener = 0;
				cvState.WakeAllPendings();
			}
		}

		void OnListenerState(Ptr<ServerState> retainedState, nw_listener_state_t state)
		{
			switch (state)
			{
			case nw_listener_state_failed:
				Stop();
				break;
			case nw_listener_state_cancelled:
				OnListenerCancelled(retainedState);
				break;
			default:
				break;
			}
		}

		Ptr<AsyncSocketConnection> CreateConnection(nw_connection_t connection)
		{
			nw_retain(connection);
			auto connectionState = Ptr(new ConnectionState(false, 0));
			auto wrapper = Ptr(new AsyncSocketConnection(connectionState));
			connectionState->ConfigureServerNative(connectionState, connection);
			return wrapper;
		}

		void OnNewConnection(nw_connection_t connection)
		{
			auto wrapper = CreateConnection(connection);
			IAsyncSocketServerCallback* installedCallback = nullptr;
			CS_LOCK(lockState)
			{
				if (started && !stopping)
				{
					installedCallback = callback;
				}
			}

			WaitForClientResult result = WaitForClientResult::Reject;
			if (installedCallback)
			{
				try
				{
					result = installedCallback->OnClientConnected(wrapper.Obj());
				}
				catch (...)
				{
				}
			}

			bool accepted = false;
			lockState.Enter();
			if (result == WaitForClientResult::Accept && started && !stopping)
			{
				connections.Add(wrapper);
				accepted = true;
			}
			lockState.Leave();

			if (accepted)
			{
				wrapper->StartAcceptedConnection();
			}
			else
			{
				wrapper->Stop();
			}
		}

	public:
		ServerState(vint _port)
			: port(_port)
		{
			queue = dispatch_queue_create("vlppos.async-socket.listener", DISPATCH_QUEUE_SERIAL);
			CHECK_ERROR(queue != nullptr, L"AsyncSocketServer failed to create its dispatch queue.");
			dispatch_queue_set_specific(queue, &serverQueueKey, this, nullptr);
		}

		~ServerState()
		{
			dispatch_queue_set_specific(queue, &serverQueueKey, nullptr, nullptr);
			dispatch_release(queue);
		}

		void Start(Ptr<ServerState> retainedState, IAsyncSocketServerCallback* value)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::macos_socket::ServerState::Start(Ptr<ServerState>, IAsyncSocketServerCallback*)#"
			CHECK_ERROR(value != nullptr, ERROR_MESSAGE_PREFIX L"Requires a callback.");
			bool begin = false;
			CS_LOCK(lockState)
			{
				if (!startCalled && !stopping)
				{
					startCalled = true;
					callback = value;
					begin = true;
				}
			}
			CHECK_ERROR(begin, ERROR_MESSAGE_PREFIX L"Can only be called once before stopping.");

			auto portText = itoa(port);
			auto endpoint = nw_endpoint_create_host("127.0.0.1", portText.Buffer());
			auto parameters = nw_parameters_create_secure_tcp(
				NW_PARAMETERS_DISABLE_PROTOCOL,
				NW_PARAMETERS_DEFAULT_CONFIGURATION
			);
			nw_listener_t createdListener = nullptr;
			if (endpoint && parameters)
			{
				nw_parameters_set_local_endpoint(parameters, endpoint);
				createdListener = nw_listener_create(parameters);
			}
			if (endpoint)
			{
				nw_release(endpoint);
			}
			if (parameters)
			{
				nw_release(parameters);
			}

			if (!createdListener)
			{
				CS_LOCK(lockState)
				{
					stopping = true;
					stopped = true;
					callback = nullptr;
				}
				CHECK_FAIL(ERROR_MESSAGE_PREFIX L"Failed to create the Network.framework listener.");
			}

			bool installed = false;
			lockState.Enter();
			if (!stopping)
			{
				listener = createdListener;
				pendingListener = 1;
				started = true;
				nw_listener_set_queue(listener, queue);

				auto retainedForState = retainedState;
				nw_listener_set_state_changed_handler(listener, ^(nw_listener_state_t state, nw_error_t)
				{
					retainedForState->OnListenerState(retainedForState, state);
				});

				auto retainedForConnection = retainedState;
				nw_listener_set_new_connection_handler(listener, ^(nw_connection_t connection)
				{
					retainedForConnection->OnNewConnection(connection);
				});
				nw_listener_start(listener);
				installed = true;
			}
			lockState.Leave();
			if (!installed)
			{
				nw_release(createdListener);
			}
#undef ERROR_MESSAGE_PREFIX
		}

		void Stop()
		{
			auto deferredDrain = IsOnQueue();
			List<Ptr<AsyncSocketConnection>> stoppingConnections;
			lockState.Enter();
			if (!stopping)
			{
				stopping = true;
				started = false;
				stopped = true;
			}
			RequestListenerCancelLocked();
			for (auto connection : connections)
			{
				stoppingConnections.Add(connection);
			}
			lockState.Leave();

			if (!deferredDrain)
			{
				for (auto connection : stoppingConnections)
				{
					if (connection->RequiresDeferredDrain())
					{
						deferredDrain = true;
						break;
					}
				}
			}

			if (deferredDrain)
			{
				for (auto connection : stoppingConnections)
				{
					connection->Stop();
				}
				return;
			}

			bool finalizeHere = false;
			lockState.Enter();
			if (!stopCompleted && !stopFinalizing)
			{
				stopFinalizing = true;
				finalizeHere = true;
			}
			while (!finalizeHere && !stopCompleted)
			{
				cvState.SleepWith(lockState);
			}
			lockState.Leave();
			if (!finalizeHere)
			{
				return;
			}

			lockState.Enter();
			while (pendingListener > 0)
			{
				cvState.SleepWith(lockState);
			}
			lockState.Leave();

			for (auto connection : stoppingConnections)
			{
				connection->Stop();
			}

			CS_LOCK(lockState)
			{
				connections.Clear();
				callback = nullptr;
				stopFinalizing = false;
				stopCompleted = true;
				cvState.WakeAllPendings();
			}
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
AsyncSocketServer::Impl
***********************************************************************/

	class AsyncSocketServer::Impl : public Object
	{
	private:
		Ptr<ServerState>					state;

	public:
		Impl(vint port)
			: state(Ptr(new ServerState(port)))
		{
		}

		~Impl()
		{
			state->Stop();
		}

		void Start(IAsyncSocketServerCallback* callback)
		{
			state->Start(state, callback);
		}

		void Stop()
		{
			state->Stop();
		}

		bool IsStopped()
		{
			return state->IsStopped();
		}
	};

/***********************************************************************
AsyncSocketServer
***********************************************************************/

	AsyncSocketServer::AsyncSocketServer(vint port)
	{
		CHECK_ERROR(1 <= port && port <= 65535, L"AsyncSocketServer requires a port in 1..65535.");
		impl = new Impl(port);
	}

	AsyncSocketServer::~AsyncSocketServer()
	{
		delete impl;
	}

	void AsyncSocketServer::Start(IAsyncSocketServerCallback* callback)
	{
		impl->Start(callback);
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
		Ptr<ConnectionState>					state;
		Ptr<AsyncSocketConnection>			connection;

	public:
		Impl(vint port)
			: state(Ptr(new ConnectionState(true, port)))
			, connection(Ptr(new AsyncSocketConnection(state)))
		{
		}

		~Impl()
		{
			connection->Stop();
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
#endif
