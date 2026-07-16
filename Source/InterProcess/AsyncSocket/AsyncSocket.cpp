#include "AsyncSocket.h"
#include <cstring>
#include <limits>

namespace vl::inter_process::async_tcp_socket
{
	thread_local NetworkProtocolCallbackDomain::CallbackFrame*	NetworkProtocolCallbackDomain::currentCallbackFrame = nullptr;
	thread_local NetworkProtocolConnection::CallbackFrame*	NetworkProtocolConnection::currentCallbackFrame = nullptr;
	thread_local NetworkProtocolConnection::SocketCallbackFrame*
										NetworkProtocolConnection::currentSocketCallbackFrame = nullptr;

/***********************************************************************
IAsyncSocketCallback
***********************************************************************/

	void IAsyncSocketCallback::OnWriteCompleted(Ptr<AsyncSocketBuffer>)
	{
	}

	void IAsyncSocketCallback::OnError(const WString&, bool)
	{
	}

	void IAsyncSocketCallback::OnConnected()
	{
	}

	void IAsyncSocketCallback::OnDisconnected()
	{
	}

	AsyncSocketServerStartException::AsyncSocketServerStartException(AsyncSocketServerStartFailure _failure, const WString& message)
		: Exception(message)
		, failure(_failure)
	{
	}

	AsyncSocketServerStartFailure AsyncSocketServerStartException::GetFailure()const
	{
		return failure;
	}

	void IAsyncSocketServerCallback::OnServerStopped()
	{
	}

/***********************************************************************
NetworkProtocolCallbackDomain::CallbackFrame
***********************************************************************/

	NetworkProtocolCallbackDomain::CallbackFrame::CallbackFrame(Ptr<NetworkProtocolCallbackDomain> _domain)
		: domain(_domain)
	{
		if (domain)
		{
			previous = currentCallbackFrame;
			currentCallbackFrame = this;
			CS_LOCK(domain->lockState)
			{
				domain->activeCallbacks++;
			}
		}
	}

	NetworkProtocolCallbackDomain::CallbackFrame::~CallbackFrame()
	{
		if (domain)
		{
			currentCallbackFrame = previous;
			CS_LOCK(domain->lockState)
			{
				domain->activeCallbacks--;
				domain->cvState.WakeAllPendings();
			}
		}
	}

/***********************************************************************
NetworkProtocolCallbackDomain
***********************************************************************/

	vint NetworkProtocolCallbackDomain::CurrentCallbackDepth()
	{
		vint depth = 0;
		for (auto frame = currentCallbackFrame; frame; frame = frame->previous)
		{
			if (frame->domain.Obj() == this)
			{
				depth++;
			}
		}
		return depth;
	}

	void NetworkProtocolCallbackDomain::WaitForCallbacks(vint callbackDepth)
	{
		CS_LOCK(lockState)
		{
			while (activeCallbacks > callbackDepth)
			{
				cvState.SleepWith(lockState);
			}
		}
	}

/***********************************************************************
NetworkProtocolConnectionLifecycle
***********************************************************************/

	void NetworkProtocolConnectionLifecycle::TakeRetainedAdapterIfDrained(Ptr<Object>& releasing)
	{
		if (stopFinished && disconnectFinished && activeCallbacks == 0 && activeSocketCallbacks == 0 && activeSocketCalls == 0)
		{
			releasing = std::move(retainedAdapter);
		}
	}

/***********************************************************************
NetworkProtocolConnection::CallbackFrame
***********************************************************************/

	NetworkProtocolConnection::CallbackFrame::CallbackFrame(Ptr<Lifecycle> _state)
		: state(_state)
		, previous(currentCallbackFrame)
		, domainFrame(state->callbackDomain)
	{
		currentCallbackFrame = this;
	}

	NetworkProtocolConnection::CallbackFrame::~CallbackFrame()
	{
		currentCallbackFrame = previous;
		Ptr<Object> releasing;
		CS_LOCK(state->lockState)
		{
			state->activeCallbacks--;
			state->TakeRetainedAdapterIfDrained(releasing);
			state->cvState.WakeAllPendings();
		}
	}

/***********************************************************************
NetworkProtocolConnection::SocketCallbackFrame
***********************************************************************/

	NetworkProtocolConnection::SocketCallbackFrame::SocketCallbackFrame(Ptr<Lifecycle> _state)
		: state(_state)
		, previous(currentSocketCallbackFrame)
	{
		currentSocketCallbackFrame = this;
		CS_LOCK(state->lockState)
		{
			state->activeSocketCallbacks++;
		}
	}

	NetworkProtocolConnection::SocketCallbackFrame::~SocketCallbackFrame()
	{
		currentSocketCallbackFrame = previous;
		Ptr<Object> releasing;
		CS_LOCK(state->lockState)
		{
			state->activeSocketCallbacks--;
			state->TakeRetainedAdapterIfDrained(releasing);
			state->cvState.WakeAllPendings();
		}
	}

/***********************************************************************
NetworkProtocolConnection
***********************************************************************/

	vint NetworkProtocolConnection::CurrentCallbackDepth(Ptr<Lifecycle> state)
	{
		vint depth = 0;
		for (auto frame = currentCallbackFrame; frame; frame = frame->previous)
		{
			if (frame->state.Obj() == state.Obj())
			{
				depth++;
			}
		}
		return depth;
	}

	vint NetworkProtocolConnection::CurrentSocketCallbackDepth(Ptr<Lifecycle> state)
	{
		vint depth = 0;
		for (auto frame = currentSocketCallbackFrame; frame; frame = frame->previous)
		{
			if (frame->state.Obj() == state.Obj())
			{
				depth++;
			}
		}
		return depth;
	}

	void NetworkProtocolConnection::FinishSocketCall(Ptr<Lifecycle> state)
	{
		CS_LOCK(state->lockState)
		{
			state->activeSocketCalls--;
			state->cvState.WakeAllPendings();
		}
	}

	template<typename TCallback>
	void NetworkProtocolConnection::InvokeProtocolCallback(Ptr<Lifecycle> state, bool allowTerminal, TCallback&& invoke)
	{
		INetworkProtocolCallback* installed = nullptr;
		auto callbackDepth = CurrentCallbackDepth(state);
		state->lockState.Enter();
		while (state->callbackInstalling && callbackDepth == 0 && state->callback)
		{
			state->cvState.SleepWith(state->lockState);
		}
		if (state->callback && (allowTerminal || (!state->stopStarted && !state->terminal)))
		{
			installed = state->callback;
			state->activeCallbacks++;
		}
		state->lockState.Leave();

		if (installed)
		{
			CallbackFrame frame(state);
			invoke(installed);
		}
	}

	void NetworkProtocolConnection::SubmitWrite(Ptr<Lifecycle> state, IAsyncSocketConnection* connection, Ptr<AsyncSocketBuffer> buffer)
	{
		try
		{
			connection->WriteAsync(buffer);
		}
		catch (...)
		{
			CS_LOCK(state->lockState)
			{
				state->queuedWrites.Clear();
				state->writePending = false;
				state->cvState.WakeAllPendings();
			}
			FinishSocketCall(state);
			throw;
		}
		FinishSocketCall(state);
	}

	void NetworkProtocolConnection::NotifyProtocolDisconnected(Ptr<Lifecycle> state)
	{
		auto callbackDepth = CurrentCallbackDepth(state);
		state->lockState.Enter();
		if (!state->disconnectedNotified)
		{
			state->disconnectedNotified = true;
			state->terminal = true;
			state->queuedWrites.Clear();
			state->writePending = false;
			state->cvState.WakeAllPendings();
		}
		if (state->disconnectFinished)
		{
			state->lockState.Leave();
			return;
		}

		if (state->disconnectDelivering)
		{
			if (callbackDepth == 0)
			{
				while (!state->disconnectFinished)
				{
					state->cvState.SleepWith(state->lockState);
				}
			}
			state->lockState.Leave();
			return;
		}

		if (callbackDepth == 0)
		{
			while (state->activeCallbacks > 0 && !state->disconnectDelivering && !state->disconnectFinished)
			{
				state->cvState.SleepWith(state->lockState);
			}
			if (state->disconnectFinished)
			{
				state->lockState.Leave();
				return;
			}
			if (state->disconnectDelivering)
			{
				while (!state->disconnectFinished)
				{
					state->cvState.SleepWith(state->lockState);
				}
				state->lockState.Leave();
				return;
			}
		}

		state->disconnectDelivering = true;
		while (state->activeCallbacks > callbackDepth)
		{
			state->cvState.SleepWith(state->lockState);
		}
		state->lockState.Leave();

		try
		{
			InvokeProtocolCallback(state, true, [](INetworkProtocolCallback* installed)
			{
				installed->OnDisconnected();
			});
		}
		catch (...)
		{
			Ptr<Object> releasing;
			CS_LOCK(state->lockState)
			{
				state->callback = nullptr;
				while (state->activeCallbacks > callbackDepth)
				{
					state->cvState.SleepWith(state->lockState);
				}
				state->disconnectFinished = true;
				state->TakeRetainedAdapterIfDrained(releasing);
				state->cvState.WakeAllPendings();
			}
			throw;
		}

		Ptr<Object> releasing;
		CS_LOCK(state->lockState)
		{
			state->callback = nullptr;
			while (state->activeCallbacks > callbackDepth)
			{
				state->cvState.SleepWith(state->lockState);
			}
			state->disconnectFinished = true;
			state->TakeRetainedAdapterIfDrained(releasing);
			state->cvState.WakeAllPendings();
		}
	}

	void NetworkProtocolConnection::DetachSocketCallback(Ptr<Lifecycle> state, IAsyncSocketConnection* connection)
	{
		if (connection)
		{
			connection->InstallCallback(nullptr);
		}
		CS_LOCK(state->lockState)
		{
			if (state->socketConnection == connection)
			{
				state->socketConnection = nullptr;
			}
			state->cvState.WakeAllPendings();
		}
	}

	void NetworkProtocolConnection::StopConnection(Ptr<Lifecycle> state, Ptr<Object> retainedAdapter)
	{
		auto callbackDepth = CurrentCallbackDepth(state);
		auto socketCallbackDepth = CurrentSocketCallbackDepth(state);
		auto nestedCallback = callbackDepth > 0 || socketCallbackDepth > 0;
		IAsyncSocketConnection* connection = nullptr;
		bool executeStop = false;
		bool nestedFollower = false;
		Ptr<Object> releasing;

		state->lockState.Enter();
		if (retainedAdapter)
		{
			state->retainedAdapter = retainedAdapter;
		}
		if (!state->stopStarted)
		{
			state->stopStarted = true;
			if (!nestedCallback && !state->terminal && state->queuedWrites.Count() > 0)
			{
				state->drainWrites = true;
				auto deadline = DateTime::LocalTime().osMilliseconds + WriteDrainTimeout;
				while (state->queuedWrites.Count() > 0 && !state->terminal)
				{
					auto now = DateTime::LocalTime().osMilliseconds;
					if (now >= deadline)
					{
						break;
					}
					state->cvState.SleepWithForTime(state->lockState, (vint)(deadline - now));
				}
				state->drainWrites = false;
			}
			state->queuedWrites.Clear();
			state->writePending = false;
			while (state->activeSocketCalls > 0)
			{
				state->cvState.SleepWith(state->lockState);
			}
			connection = state->socketConnection;
			executeStop = true;
		}
		else if (nestedCallback)
		{
			connection = state->socketConnection;
			nestedFollower = true;
		}
		else
		{
			while (!state->stopFinished)
			{
				state->cvState.SleepWith(state->lockState);
			}
			while (state->activeCallbacks > 0 || state->activeSocketCallbacks > 0 || state->activeSocketCalls > 0)
			{
				state->cvState.SleepWith(state->lockState);
			}
			state->TakeRetainedAdapterIfDrained(releasing);
			state->lockState.Leave();
			return;
		}
		state->lockState.Leave();

		if (nestedFollower)
		{
			if (connection && socketCallbackDepth > 0)
			{
				connection->Stop();
			}
			NotifyProtocolDisconnected(state);
			return;
		}

		if (executeStop && connection)
		{
			connection->Stop();
		}
		NotifyProtocolDisconnected(state);

		CS_LOCK(state->lockState)
		{
			while (state->activeCallbacks > callbackDepth || state->activeSocketCallbacks > socketCallbackDepth)
			{
				state->cvState.SleepWith(state->lockState);
			}
			state->stopFinished = true;
			state->TakeRetainedAdapterIfDrained(releasing);
			state->cvState.WakeAllPendings();
		}
	}

	void NetworkProtocolConnection::ReportFatalError(Ptr<Lifecycle> state, const WString& error)
	{
		bool report = false;
		CS_LOCK(state->lockState)
		{
			if (!state->terminal && !state->stopStarted)
			{
				state->terminal = true;
				state->queuedWrites.Clear();
				state->writePending = false;
				state->cvState.WakeAllPendings();
				report = true;
			}
		}
		if (report)
		{
			try
			{
				InvokeProtocolCallback(state, true, [&](INetworkProtocolCallback* installed)
				{
					installed->OnLocalError(error, true);
				});
			}
			catch (...)
			{
				StopConnection(state);
				throw;
			}
			StopConnection(state);
		}
	}

	void NetworkProtocolConnection::StopWithRetainedAdapter(Ptr<NetworkProtocolConnection> retainedAdapter)
	{
		StopConnection(lifecycle, retainedAdapter);
	}

	NetworkProtocolConnection::NetworkProtocolConnection(IAsyncSocketConnection* connection, Ptr<NetworkProtocolCallbackDomain> callbackDomain)
		: lifecycle(Ptr(new Lifecycle))
	{
		CHECK_ERROR(connection, L"NetworkProtocolConnection requires a valid async socket connection.");
		lifecycle->socketConnection = connection;
		lifecycle->callbackDomain = callbackDomain;
		connection->InstallCallback(this);
	}

	NetworkProtocolConnection::~NetworkProtocolConnection()
	{
		StopConnection(lifecycle);
	}

	void NetworkProtocolConnection::InstallCallback(INetworkProtocolCallback* value)
	{
		auto state = lifecycle;
		if (!value)
		{
			auto callbackDepth = CurrentCallbackDepth(state);
			bool uninstallOwner = false;
			CS_LOCK(state->lockState)
			{
				uninstallOwner = state->callback != nullptr;
				state->callback = nullptr;
				while ((callbackDepth == 0 || uninstallOwner) && state->activeCallbacks > callbackDepth)
				{
					state->cvState.SleepWith(state->lockState);
				}
			}
			return;
		}

		bool canInstall = false;
		CS_LOCK(state->lockState)
		{
			if (!state->callback && !state->callbackInstalling && !state->stopStarted && !state->terminal)
			{
				state->callback = value;
				state->callbackInstalling = true;
				state->activeCallbacks++;
				canInstall = true;
			}
		}
		CHECK_ERROR(canInstall, L"NetworkProtocolConnection::InstallCallback cannot replace a callback or install one on a stopped connection.");

		CallbackFrame frame(state);
		try
		{
			value->OnInstalled(this);
		}
		catch (...)
		{
			CS_LOCK(state->lockState)
			{
				if (state->callback == value)
				{
					state->callback = nullptr;
				}
				state->callbackInstalling = false;
				state->cvState.WakeAllPendings();
			}
			throw;
		}

		CS_LOCK(state->lockState)
		{
			state->callbackInstalling = false;
			state->cvState.WakeAllPendings();
		}
	}

	void NetworkProtocolConnection::BeginReadingLoopUnsafe()
	{
		auto state = lifecycle;
		IAsyncSocketConnection* connection = nullptr;
		CS_LOCK(state->lockState)
		{
			if (!state->stopStarted && !state->terminal && state->socketConnection)
			{
				connection = state->socketConnection;
				state->activeSocketCalls++;
			}
		}
		if (!connection)
		{
			return;
		}

		try
		{
			connection->BeginReadingLoopUnsafe();
		}
		catch (...)
		{
			FinishSocketCall(state);
			throw;
		}
		FinishSocketCall(state);
	}

	void NetworkProtocolConnection::SendString(const WString& str)
	{
		auto state = lifecycle;
		auto length = str.Length();
		CHECK_ERROR(length >= 0 && length <= (std::numeric_limits<vint32_t>::max)(), L"NetworkProtocolConnection::SendString cannot encode a string longer than vint32_t.");
		CHECK_ERROR((size_t)length <= ((size_t)(std::numeric_limits<vint>::max)() - sizeof(vint32_t)) / sizeof(wchar_t), L"NetworkProtocolConnection::SendString frame size overflow.");

		auto buffer = Ptr(new AsyncSocketBuffer);
		auto characterBytes = (size_t)length * sizeof(wchar_t);
		auto frameBytes = sizeof(vint32_t) + characterBytes;
		buffer->data.Resize((vint)frameBytes);
		auto encodedLength = (vint32_t)length;
		std::memcpy(&buffer->data[0], &encodedLength, sizeof(encodedLength));
		if (characterBytes > 0)
		{
			std::memcpy(&buffer->data[sizeof(encodedLength)], str.Buffer(), characterBytes);
		}

		IAsyncSocketConnection* connection = nullptr;
		Ptr<AsyncSocketBuffer> submitting;
		CS_LOCK(state->lockState)
		{
			if (!state->stopStarted && !state->terminal && state->socketConnection)
			{
				state->queuedWrites.Add(buffer);
				if (!state->writePending)
				{
					state->writePending = true;
					connection = state->socketConnection;
					submitting = state->queuedWrites[0];
					state->activeSocketCalls++;
				}
			}
		}
		if (submitting)
		{
			SubmitWrite(state, connection, submitting);
		}
	}

	void NetworkProtocolConnection::Stop()
	{
		StopConnection(lifecycle);
	}

	void NetworkProtocolConnection::OnRead(const vuint8_t* buffer, vint size)
	{
		auto state = lifecycle;
		SocketCallbackFrame socketCallbackFrame(state);
		if (!buffer || size <= 0)
		{
			return;
		}

		collections::List<WString> completedStrings;
		bool malformed = false;
		CS_LOCK(state->lockParser)
		{
			if (state->parserFailed)
			{
				return;
			}

			auto reading = buffer;
			auto available = size;
			while (available > 0)
			{
				if (state->expectedCharacters == -1)
				{
					auto required = (vint)sizeof(vint32_t) - state->lengthBytesReceived;
					auto copied = required < available ? required : available;
					std::memcpy(state->lengthBytes + state->lengthBytesReceived, reading, (size_t)copied);
					state->lengthBytesReceived += copied;
					reading += copied;
					available -= copied;
					if (state->lengthBytesReceived < (vint)sizeof(vint32_t))
					{
						continue;
					}

					std::memcpy(&state->expectedCharacters, state->lengthBytes, sizeof(state->expectedCharacters));
					state->lengthBytesReceived = 0;
					if (state->expectedCharacters < 0 || (size_t)state->expectedCharacters > ((size_t)(std::numeric_limits<vint>::max)() - sizeof(vint32_t)) / sizeof(wchar_t))
					{
						state->parserFailed = true;
						malformed = true;
						break;
					}

					state->characterBuffer.Resize((vint)state->expectedCharacters);
					state->characterBytesReceived = 0;
					if (state->expectedCharacters == 0)
					{
						completedStrings.Add(WString());
						state->expectedCharacters = -1;
					}
				}

				if (state->expectedCharacters >= 0)
				{
					auto characterBytes = (vint)((size_t)state->expectedCharacters * sizeof(wchar_t));
					auto required = characterBytes - state->characterBytesReceived;
					auto copied = required < available ? required : available;
					std::memcpy((vuint8_t*)&state->characterBuffer[0] + state->characterBytesReceived, reading, (size_t)copied);
					state->characterBytesReceived += copied;
					reading += copied;
					available -= copied;
					if (state->characterBytesReceived == characterBytes)
					{
						completedStrings.Add(WString::CopyFrom(&state->characterBuffer[0], state->expectedCharacters));
						state->characterBuffer.Resize(0);
						state->characterBytesReceived = 0;
						state->expectedCharacters = -1;
					}
				}
			}
		}

		for (auto&& str : completedStrings)
		{
			InvokeProtocolCallback(state, false, [&](INetworkProtocolCallback* installed)
			{
				installed->OnReadString(str);
			});
		}
		if (malformed)
		{
			ReportFatalError(state, L"The async socket protocol received an invalid string length.");
		}
	}

	void NetworkProtocolConnection::OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer)
	{
		auto state = lifecycle;
		SocketCallbackFrame socketCallbackFrame(state);
		IAsyncSocketConnection* connection = nullptr;
		Ptr<AsyncSocketBuffer> submitting;
		bool mismatched = false;
		CS_LOCK(state->lockState)
		{
			if (state->queuedWrites.Count() == 0)
			{
				return;
			}
			if (state->queuedWrites[0].Obj() != buffer.Obj())
			{
				state->queuedWrites.Clear();
				state->writePending = false;
				mismatched = true;
				state->cvState.WakeAllPendings();
			}
			else
			{
				state->queuedWrites.RemoveAt(0);
				if ((!state->stopStarted || state->drainWrites) && !state->terminal && state->socketConnection && state->queuedWrites.Count() > 0)
				{
					connection = state->socketConnection;
					submitting = state->queuedWrites[0];
					state->activeSocketCalls++;
				}
				else
				{
					state->writePending = false;
				}
				state->cvState.WakeAllPendings();
			}
		}
		CHECK_ERROR(!mismatched, L"NetworkProtocolConnection received a completion for an unexpected async socket buffer.");
		if (submitting)
		{
			SubmitWrite(state, connection, submitting);
		}
	}

	void NetworkProtocolConnection::OnError(const WString& error, bool fatal)
	{
		auto state = lifecycle;
		SocketCallbackFrame socketCallbackFrame(state);
		if (fatal)
		{
			ReportFatalError(state, error);
		}
		else
		{
			InvokeProtocolCallback(state, false, [&](INetworkProtocolCallback* installed)
			{
				installed->OnLocalError(error, false);
			});
		}
	}

	void NetworkProtocolConnection::OnConnected()
	{
		auto state = lifecycle;
		SocketCallbackFrame socketCallbackFrame(state);
		InvokeProtocolCallback(state, false, [](INetworkProtocolCallback* installed)
		{
			installed->OnConnected();
		});
	}

	void NetworkProtocolConnection::OnDisconnected()
	{
		auto state = lifecycle;
		SocketCallbackFrame socketCallbackFrame(state);
		IAsyncSocketConnection* connection = nullptr;
		CS_LOCK(state->lockState)
		{
			while (state->activeSocketCalls > 0)
			{
				state->cvState.SleepWith(state->lockState);
			}
			connection = state->socketConnection;
		}

		try
		{
			DetachSocketCallback(state, connection);
		}
		catch (...)
		{
			CS_LOCK(state->lockState)
			{
				if (state->socketConnection == connection)
				{
					state->socketConnection = nullptr;
				}
				state->cvState.WakeAllPendings();
			}
			NotifyProtocolDisconnected(state);
			throw;
		}
		NotifyProtocolDisconnected(state);
	}

	void NetworkProtocolConnection::OnInstalled(IAsyncSocketConnection* connection)
	{
		auto state = lifecycle;
		SocketCallbackFrame socketCallbackFrame(state);
		CHECK_ERROR(connection == state->socketConnection, L"NetworkProtocolConnection was installed on an unexpected async socket connection.");
	}
}
