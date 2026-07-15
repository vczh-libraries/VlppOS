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
		virtual void							OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer);
		/// <summary>Called when an asynchronous operation fails.</summary>
		virtual void							OnError(const WString& error, bool fatal);
		/// <summary>Called for the client connection after it is established.</summary>
		virtual void							OnConnected();
		/// <summary>Called exactly once when the connection stops.</summary>
		virtual void							OnDisconnected();
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
		static thread_local CallbackFrame*	currentCallbackFrame;
		CriticalSection					lockState;
		ConditionVariable				cvState;
		vint							activeCallbacks = 0;

	public:
		struct CallbackFrame
		{
			Ptr<NetworkProtocolCallbackDomain>	domain;
			CallbackFrame*					previous = nullptr;

			CallbackFrame(Ptr<NetworkProtocolCallbackDomain> _domain);
			~CallbackFrame();
		};

		vint							CurrentCallbackDepth();
		void							WaitForCallbacks(vint callbackDepth);
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

		void							TakeRetainedAdapterIfDrained(Ptr<Object>& releasing);
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
		static thread_local CallbackFrame*	currentCallbackFrame;
		static thread_local SocketCallbackFrame*
										currentSocketCallbackFrame;

		struct CallbackFrame
		{
			Ptr<Lifecycle>					state;
			CallbackFrame*					previous = nullptr;
			NetworkProtocolCallbackDomain::CallbackFrame
										domainFrame;

			CallbackFrame(Ptr<Lifecycle> _state);
			~CallbackFrame();
		};

		struct SocketCallbackFrame
		{
			Ptr<Lifecycle>					state;
			SocketCallbackFrame*				previous = nullptr;

			SocketCallbackFrame(Ptr<Lifecycle> _state);
			~SocketCallbackFrame();
		};

		Ptr<Lifecycle>					lifecycle;

		static vint						CurrentCallbackDepth(Ptr<Lifecycle> state);
		static vint						CurrentSocketCallbackDepth(Ptr<Lifecycle> state);
		static void						FinishSocketCall(Ptr<Lifecycle> state);

		template<typename TCallback>
		static void						InvokeProtocolCallback(Ptr<Lifecycle> state, bool allowTerminal, TCallback&& invoke);

		static void						SubmitWrite(Ptr<Lifecycle> state, IAsyncSocketConnection* connection, Ptr<AsyncSocketBuffer> buffer);
		static void						NotifyProtocolDisconnected(Ptr<Lifecycle> state);
		static void						DetachSocketCallback(Ptr<Lifecycle> state, IAsyncSocketConnection* connection);
		static void						StopConnection(Ptr<Lifecycle> state, Ptr<Object> retainedAdapter = nullptr);
		static void						ReportFatalError(Ptr<Lifecycle> state, const WString& error);

		template<typename TAsyncSocketServer>
		friend class NetworkProtocolServer;

		template<typename TAsyncSocketClient>
		friend class NetworkProtocolClient;

		void							StopWithRetainedAdapter(Ptr<NetworkProtocolConnection> retainedAdapter);

	public:
		explicit NetworkProtocolConnection(IAsyncSocketConnection* connection, Ptr<NetworkProtocolCallbackDomain> callbackDomain = nullptr);
		~NetworkProtocolConnection();

		void							InstallCallback(INetworkProtocolCallback* value) override;
		void							BeginReadingLoopUnsafe() override;
		void							SendString(const WString& str) override;
		void							Stop() override;
		void							OnRead(const vuint8_t* buffer, vint size) override;
		void							OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer) override;
		void							OnError(const WString& error, bool fatal) override;
		void							OnConnected() override;
		void							OnDisconnected() override;
		void							OnInstalled(IAsyncSocketConnection* connection) override;
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
