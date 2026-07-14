/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
  IAsyncSocket(Server|Client)

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET
#define VCZH_INTERPROCESS_ASYNCSOCKET

#include "../NetworkProtocol.h"
#include "../../Threading.h"
#include <concepts>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace vl::inter_process::async_tcp_socket
{
	/// <summary>A retained buffer for one asynchronous write.</summary>
	class AsyncSocketBuffer : public Object
	{
	public:
		collections::Array<vuint8_t>			data;
	};

	class IAsyncSocketConnection;

	/// <summary>Callbacks for an asynchronous byte-stream connection.</summary>
	class IAsyncSocketCallback : public virtual Interface
	{
	public:
		/// <summary>Called with one positive borrowed read block.</summary>
		virtual void							OnRead(const vuint8_t* buffer, vint size) = 0;
		/// <summary>Called after the complete retained write buffer has been sent.</summary>
		virtual void							OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer) {}
		/// <summary>Called when an asynchronous operation fails.</summary>
		virtual void							OnError(const WString& error, bool fatal) {}
		/// <summary>Called for the client connection after it is established.</summary>
		virtual void							OnConnected() {}
		/// <summary>Called exactly once when the connection stops.</summary>
		virtual void							OnDisconnected() {}
		/// <summary>Called synchronously when this callback is installed.</summary>
		virtual void							OnInstalled(IAsyncSocketConnection* connection) = 0;
	};

	/// <summary>An ordered, full-duplex asynchronous byte stream.</summary>
	class IAsyncSocketConnection : public virtual Interface
	{
	public:
		virtual void							InstallCallback(IAsyncSocketCallback* callback) = 0;
		virtual void							BeginReadingLoopUnsafe() = 0;
		virtual void							WriteAsync(Ptr<AsyncSocketBuffer> buffer) = 0;
		virtual void							Stop() = 0;
	};

	/// <summary>An asynchronous TCP client for the local machine.</summary>
	class IAsyncSocketClient : public virtual Interface
	{
	public:
		virtual IAsyncSocketConnection*			GetConnection() = 0;
		virtual void							WaitForServer() = 0;
		virtual ClientStatus					GetStatus() = 0;
	};

	/// <summary>An asynchronous TCP server for the local machine.</summary>
	class IAsyncSocketServer : public virtual Interface
	{
	public:
		virtual WaitForClientResult			OnClientConnected(IAsyncSocketConnection* connection) = 0;
		virtual void							Start() = 0;
		virtual void							Stop() = 0;
		virtual bool							IsStopped() = 0;
	};

	// This policy is intentionally platform-neutral. Each failed attempt creates
	// a fresh native socket and is followed by an asynchronous millisecond delay.
	constexpr vint AsyncSocketClientRetryCount = 50;
	constexpr vint AsyncSocketClientRetryDelay = 100;

/***********************************************************************
NetworkProtocolConnection
***********************************************************************/

	class NetworkProtocolCallbackDomain : public Object
	{
	public:
		struct CallbackFrame;

	private:
		inline static thread_local CallbackFrame*	currentCallbackFrame = nullptr;
		CriticalSection					lockState;
		ConditionVariable				cvState;
		vint							activeCallbacks = 0;

	public:
		struct CallbackFrame
		{
			Ptr<NetworkProtocolCallbackDomain>	domain;
			CallbackFrame*					previous = nullptr;

			CallbackFrame(Ptr<NetworkProtocolCallbackDomain> _domain)
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

			~CallbackFrame()
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
		};

		vint CurrentCallbackDepth()
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

		void WaitForCallbacks(vint callbackDepth)
		{
			CS_LOCK(lockState)
			{
				while (activeCallbacks > callbackDepth)
				{
					cvState.SleepWith(lockState);
				}
			}
		}
	};

	class NetworkProtocolConnectionLifecycle : public Object
	{
	public:
		IAsyncSocketConnection*				socketConnection = nullptr;
		Ptr<NetworkProtocolCallbackDomain>	callbackDomain;
		Ptr<Object>						retainedAdapter;

		CriticalSection					lockState;
		ConditionVariable				cvState;
		INetworkProtocolCallback*			callback = nullptr;
		bool							callbackInstalling = false;
		vint							activeCallbacks = 0;
		vint							activeSocketCallbacks = 0;
		vint							activeSocketCalls = 0;
		bool							stopStarted = false;
		bool							stopFinished = false;
		bool							terminal = false;
		bool							disconnectedNotified = false;
		bool							disconnectDelivering = false;
		bool							disconnectFinished = false;
		collections::List<Ptr<AsyncSocketBuffer>>
										queuedWrites;
		bool							writePending = false;
		bool							drainWrites = false;

		CriticalSection					lockParser;
		vuint8_t						lengthBytes[sizeof(vint32_t)] = {};
		vint							lengthBytesReceived = 0;
		vint32_t						expectedCharacters = -1;
		collections::Array<wchar_t>		characterBuffer;
		vint							characterBytesReceived = 0;
		bool							parserFailed = false;

		void TakeRetainedAdapterIfDrained(Ptr<Object>& releasing)
		{
			if (stopFinished && disconnectFinished && activeCallbacks == 0 && activeSocketCallbacks == 0 && activeSocketCalls == 0)
			{
				releasing = std::move(retainedAdapter);
			}
		}
	};

	/// <summary>Adapts an asynchronous byte stream to framed network-protocol strings.</summary>
	class NetworkProtocolConnection
		: public Object
		, public virtual INetworkProtocolConnection
		, public virtual IAsyncSocketCallback
	{
	private:
		using Lifecycle = NetworkProtocolConnectionLifecycle;
		static constexpr vint				WriteDrainTimeout = 1000;

		struct CallbackFrame;
		struct SocketCallbackFrame;
		inline static thread_local CallbackFrame*	currentCallbackFrame = nullptr;
		inline static thread_local SocketCallbackFrame*
										currentSocketCallbackFrame = nullptr;

		struct CallbackFrame
		{
			Ptr<Lifecycle>					state;
			CallbackFrame*					previous = nullptr;
			NetworkProtocolCallbackDomain::CallbackFrame
										domainFrame;

			CallbackFrame(Ptr<Lifecycle> _state)
				: state(_state)
				, previous(currentCallbackFrame)
				, domainFrame(state->callbackDomain)
			{
				currentCallbackFrame = this;
			}

			~CallbackFrame()
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
		};

		struct SocketCallbackFrame
		{
			Ptr<Lifecycle>					state;
			SocketCallbackFrame*				previous = nullptr;

			SocketCallbackFrame(Ptr<Lifecycle> _state)
				: state(_state)
				, previous(currentSocketCallbackFrame)
			{
				currentSocketCallbackFrame = this;
				CS_LOCK(state->lockState)
				{
					state->activeSocketCallbacks++;
				}
			}

			~SocketCallbackFrame()
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
		};

		Ptr<Lifecycle>					lifecycle;

		static vint CurrentCallbackDepth(Ptr<Lifecycle> state)
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

		static vint CurrentSocketCallbackDepth(Ptr<Lifecycle> state)
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

		static void FinishSocketCall(Ptr<Lifecycle> state)
		{
			CS_LOCK(state->lockState)
			{
				state->activeSocketCalls--;
				state->cvState.WakeAllPendings();
			}
		}

		template<typename TCallback>
		static void InvokeProtocolCallback(Ptr<Lifecycle> state, bool allowTerminal, TCallback&& invoke)
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

		static void SubmitWrite(Ptr<Lifecycle> state, IAsyncSocketConnection* connection, Ptr<AsyncSocketBuffer> buffer)
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

		static void NotifyProtocolDisconnected(Ptr<Lifecycle> state)
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

		static void DetachSocketCallback(Ptr<Lifecycle> state, IAsyncSocketConnection* connection)
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

		static void StopConnection(Ptr<Lifecycle> state, Ptr<Object> retainedAdapter = nullptr)
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

		static void ReportFatalError(Ptr<Lifecycle> state, const WString& error)
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

		template<typename TAsyncSocketServer>
		friend class NetworkProtocolServer;

		template<typename TAsyncSocketClient>
		friend class NetworkProtocolClient;

		void StopWithRetainedAdapter(Ptr<NetworkProtocolConnection> retainedAdapter)
		{
			StopConnection(lifecycle, retainedAdapter);
		}

	public:
		explicit NetworkProtocolConnection(IAsyncSocketConnection* connection, Ptr<NetworkProtocolCallbackDomain> callbackDomain = nullptr)
			: lifecycle(Ptr(new Lifecycle))
		{
			CHECK_ERROR(connection, L"NetworkProtocolConnection requires a valid async socket connection.");
			lifecycle->socketConnection = connection;
			lifecycle->callbackDomain = callbackDomain;
			connection->InstallCallback(this);
		}

		~NetworkProtocolConnection()
		{
			StopConnection(lifecycle);
		}

		void InstallCallback(INetworkProtocolCallback* value) override
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

		void BeginReadingLoopUnsafe() override
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

		void SendString(const WString& str) override
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

		void Stop() override
		{
			StopConnection(lifecycle);
		}

		void OnRead(const vuint8_t* buffer, vint size) override
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

		void OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer) override
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

		void OnError(const WString& error, bool fatal) override
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

		void OnConnected() override
		{
			auto state = lifecycle;
			SocketCallbackFrame socketCallbackFrame(state);
			InvokeProtocolCallback(state, false, [](INetworkProtocolCallback* installed)
			{
				installed->OnConnected();
			});
		}

		void OnDisconnected() override
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

		void OnInstalled(IAsyncSocketConnection* connection) override
		{
			auto state = lifecycle;
			SocketCallbackFrame socketCallbackFrame(state);
			CHECK_ERROR(connection == state->socketConnection, L"NetworkProtocolConnection was installed on an unexpected async socket connection.");
		}
	};

/***********************************************************************
NetworkProtocolServer
***********************************************************************/

	template<typename TAsyncSocketServer>
	class NetworkProtocolServer
		: public Object
		, public virtual INetworkProtocolServer
	{
		static_assert(std::derived_from<TAsyncSocketServer, IAsyncSocketServer>);
		static_assert(!std::is_final_v<TAsyncSocketServer>);

	private:
		class Lifecycle : public Object
		{
		public:
			NetworkProtocolServer*				owner = nullptr;
			Ptr<NetworkProtocolCallbackDomain>	callbackDomain = Ptr(new NetworkProtocolCallbackDomain);
			CriticalSection					lockState;
			ConditionVariable				cvState;
			collections::List<Ptr<NetworkProtocolConnection>>
										connections;
			bool							stopStarted = false;
			bool							stopFinished = false;
			bool							nativeStopCalling = false;

			Lifecycle(NetworkProtocolServer* _owner)
				: owner(_owner)
			{
			}
		};

		class SocketServerBridge : public TAsyncSocketServer
		{
		private:
			Ptr<Lifecycle>					lifecycle;
			CriticalSection					lockSelf;
			Ptr<SocketServerBridge>			selfReference;

		public:
			template<typename... TArgs>
			SocketServerBridge(Ptr<Lifecycle> _lifecycle, TArgs&&... args)
				: TAsyncSocketServer(std::forward<TArgs>(args)...)
				, lifecycle(_lifecycle)
			{
			}

			void InitializeSelf(Ptr<SocketServerBridge> self)
			{
				CS_LOCK(lockSelf)
				{
					selfReference = self;
				}
			}

			void ReleaseSelfReference()
			{
				CS_LOCK(lockSelf)
				{
					selfReference = nullptr;
				}
			}

			WaitForClientResult OnClientConnected(IAsyncSocketConnection* connection) override
			{
				Ptr<SocketServerBridge> self;
				CS_LOCK(lockSelf)
				{
					self = selfReference;
				}
				auto state = lifecycle;
				NetworkProtocolServer* owner = nullptr;
				CS_LOCK(state->lockState)
				{
					owner = state->owner;
				}
				return owner ? owner->OnSocketClientConnected(connection) : WaitForClientResult::Reject;
			}
		};

		Ptr<Lifecycle>					lifecycle;
		Ptr<SocketServerBridge>			asyncSocketServer;

		static void StopConnections(Ptr<Lifecycle> state, bool retainAdapters = false)
		{
			collections::List<Ptr<NetworkProtocolConnection>> stoppingConnections;
			CS_LOCK(state->lockState)
			{
				for (auto connection : state->connections)
				{
					stoppingConnections.Add(connection);
				}
			}
			for (auto connection : stoppingConnections)
			{
				if (retainAdapters)
				{
					connection->StopWithRetainedAdapter(connection);
				}
				else
				{
					connection->Stop();
				}
			}
		}

		static void QueueDeferredStop(Ptr<Lifecycle> state, Ptr<SocketServerBridge> nativeServer)
		{
			ThreadPoolLite::QueueLambda([state, nativeServer]()
			{
				state->lockState.Enter();
				while (!state->stopFinished || state->nativeStopCalling)
				{
					state->cvState.SleepWith(state->lockState);
				}
				state->nativeStopCalling = true;
				state->lockState.Leave();

				try
				{
					nativeServer->Stop();
					StopConnections(state);
					state->callbackDomain->WaitForCallbacks(0);
				}
				catch (...)
				{
				}

				CS_LOCK(state->lockState)
				{
					state->nativeStopCalling = false;
					state->cvState.WakeAllPendings();
				}
				nativeServer->ReleaseSelfReference();
			});
		}

		WaitForClientResult OnSocketClientConnected(IAsyncSocketConnection* connection)
		{
			auto state = lifecycle;
			NetworkProtocolCallbackDomain::CallbackFrame callbackFrame(state->callbackDomain);
			bool acceptCallback = false;
			CS_LOCK(state->lockState)
			{
				acceptCallback = !state->stopStarted;
			}
			if (!acceptCallback)
			{
				return WaitForClientResult::Reject;
			}

			auto protocolConnection = Ptr(new NetworkProtocolConnection(connection, state->callbackDomain));
			CS_LOCK(state->lockState)
			{
				state->connections.Add(protocolConnection);
				acceptCallback = !state->stopStarted;
			}
			return acceptCallback ? OnClientConnected(protocolConnection.Obj()) : WaitForClientResult::Reject;
		}

	public:
		template<typename... TArgs>
		NetworkProtocolServer(TArgs&&... args)
			: lifecycle(Ptr(new Lifecycle(this)))
			, asyncSocketServer(new SocketServerBridge(lifecycle, std::forward<TArgs>(args)...))
		{
			asyncSocketServer->InitializeSelf(asyncSocketServer);
		}

		~NetworkProtocolServer()
		{
			Stop();
			auto state = lifecycle;
			StopConnections(state, true);
			CS_LOCK(state->lockState)
			{
				state->owner = nullptr;
				state->connections.Clear();
			}
		}

		virtual WaitForClientResult OnClientConnected(INetworkProtocolConnection*) override
		{
			return WaitForClientResult::Accept;
		}

		void Start() override
		{
			asyncSocketServer->Start();
		}

		void Stop() override
		{
			auto state = lifecycle;
			auto nativeServer = asyncSocketServer;
			auto callbackDepth = state->callbackDomain->CurrentCallbackDepth();
			bool firstStop = false;
			bool nestedFollower = false;
			bool deferFinalization = false;
			state->lockState.Enter();
			if (!state->stopStarted)
			{
				state->stopStarted = true;
				state->nativeStopCalling = true;
				firstStop = true;
			}
			else if (callbackDepth > 0)
			{
				nestedFollower = true;
			}
			else
			{
				while (!state->stopFinished || state->nativeStopCalling)
				{
					state->cvState.SleepWith(state->lockState);
				}
				state->nativeStopCalling = true;
			}
			state->lockState.Leave();
			if (nestedFollower)
			{
				StopConnections(state);
				return;
			}

			try
			{
				nativeServer->Stop();
				StopConnections(state);
				state->callbackDomain->WaitForCallbacks(firstStop ? callbackDepth : 0);
				deferFinalization = firstStop && callbackDepth > 0;
				if (!deferFinalization)
				{
					nativeServer->ReleaseSelfReference();
				}
			}
			catch (...)
			{
				if (!firstStop || callbackDepth == 0)
				{
					nativeServer->ReleaseSelfReference();
				}
				CS_LOCK(state->lockState)
				{
					state->nativeStopCalling = false;
					if (firstStop)
					{
						state->stopFinished = true;
					}
					state->cvState.WakeAllPendings();
				}
				throw;
			}

			CS_LOCK(state->lockState)
			{
				state->nativeStopCalling = false;
				if (firstStop)
				{
					state->stopFinished = true;
				}
				state->cvState.WakeAllPendings();
			}
			if (deferFinalization)
			{
				QueueDeferredStop(state, nativeServer);
			}
		}

		bool IsStopped() override
		{
			return asyncSocketServer->IsStopped();
		}
	};

/***********************************************************************
NetworkProtocolClient
***********************************************************************/

	template<typename TAsyncSocketClient>
	class NetworkProtocolClient
		: public Object
		, public virtual INetworkProtocolClient
	{
		static_assert(std::derived_from<TAsyncSocketClient, IAsyncSocketClient>);

	private:
		Ptr<TAsyncSocketClient>				asyncSocketClient;
		Ptr<NetworkProtocolConnection>		connection;

		static void QueueDeferredRelease(Ptr<TAsyncSocketClient> nativeClient)
		{
			ThreadPoolLite::QueueLambda([nativeClient]()
			{
				nativeClient->GetConnection()->Stop();
			});
		}

	public:
		template<typename... TArgs>
		NetworkProtocolClient(TArgs&&... args)
			: asyncSocketClient(new TAsyncSocketClient(std::forward<TArgs>(args)...))
			, connection(new NetworkProtocolConnection(asyncSocketClient->GetConnection()))
		{
		}

		~NetworkProtocolClient()
		{
			auto state = connection->lifecycle;
			auto deferFinalization =
				NetworkProtocolConnection::CurrentCallbackDepth(state) > 0 ||
				NetworkProtocolConnection::CurrentSocketCallbackDepth(state) > 0;
			auto nativeClient = asyncSocketClient;
			connection->StopWithRetainedAdapter(connection);
			if (deferFinalization)
			{
				QueueDeferredRelease(nativeClient);
			}
		}

		INetworkProtocolConnection* GetConnection() override
		{
			return connection.Obj();
		}

		void WaitForServer() override
		{
			asyncSocketClient->WaitForServer();
		}

		ClientStatus GetStatus() override
		{
			return asyncSocketClient->GetStatus();
		}
	};
}

#endif
