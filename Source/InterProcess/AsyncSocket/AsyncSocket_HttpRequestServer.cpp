#include "AsyncSocket_HttpRequestServer.h"

namespace vl::inter_process::async_tcp_socket
{
/***********************************************************************
HttpRequestServer::Impl
***********************************************************************/

	class HttpRequestServer::Impl : public Object
	{
	private:
		class Lifecycle : public Object
		{
		public:
			HttpRequestServer*					owner = nullptr;
			Ptr<IAsyncSocketServer>				server;
			Ptr<HttpRequestCallbackDomain>		callbackDomain = Ptr(new HttpRequestCallbackDomain);
			Func<Ptr<IHttpRequestTimeoutController>()>
										timeoutControllerFactory;

			// covers owner, connections, and all lifecycle flags below
			CriticalSection						lockState;
			ConditionVariable					cvState;
			collections::List<Ptr<HttpRequestConnection>>
										connections;
			bool								startCalled = false;
			bool								stopStarted = false;
			bool								unexpectedStopNotified = false;
			bool								stopFinished = false;
			bool								nativeStopCalling = false;
			bool								destroyStarted = false;
			bool								destroyAdaptersRetained = false;

			Lifecycle(
				HttpRequestServer* _owner,
				Ptr<IAsyncSocketServer> _server,
				const Func<Ptr<IHttpRequestTimeoutController>()>& _timeoutControllerFactory
				)
				: owner(_owner)
				, server(_server)
				, timeoutControllerFactory(_timeoutControllerFactory)
			{
			}
		};

		class SocketServerCallback
			: public Object
			, public virtual IAsyncSocketServerCallback
		{
		private:
			Ptr<Lifecycle>						lifecycle;
			CriticalSection						lockSelf;
			Ptr<SocketServerCallback>			selfReference;

		public:
			SocketServerCallback(Ptr<Lifecycle> _lifecycle)
				: lifecycle(_lifecycle)
			{
			}

			void InitializeSelf(Ptr<SocketServerCallback> self)
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
				Ptr<SocketServerCallback> self;
				CS_LOCK(lockSelf)
				{
					self = selfReference;
				}
				return self
					? Impl::OnSocketClientConnected(lifecycle, connection)
					: WaitForClientResult::Reject;
			}

			void OnServerStopped() override
			{
				Ptr<SocketServerCallback> self;
				CS_LOCK(lockSelf)
				{
					self = selfReference;
				}
				if (self)
				{
					Impl::OnSocketServerStopped(lifecycle);
				}
			}
		};

		Ptr<Lifecycle>						lifecycle;
		Ptr<SocketServerCallback>			callback;

		static void StopConnections(Ptr<Lifecycle> state, bool retainAdapters = false)
		{
			collections::List<Ptr<HttpRequestConnection>> stoppingConnections;
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

		static void FinalizeDestroyedOwner(Ptr<Lifecycle> state)
		{
			CS_LOCK(state->lockState)
			{
				if (state->destroyStarted && state->destroyAdaptersRetained)
				{
					state->owner = nullptr;
					state->connections.Clear();
				}
			}
		}

		static void QueueDeferredStop(Ptr<Lifecycle> state, Ptr<SocketServerCallback> retainedCallback)
		{
			auto finalize = Func<void()>([state, retainedCallback]()
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
					state->server->Stop();
				}
				catch (...)
				{
				}
				try
				{
					StopConnections(state);
				}
				catch (...)
				{
				}
				state->callbackDomain->WaitForCallbacks(0);
				FinalizeDestroyedOwner(state);

				CS_LOCK(state->lockState)
				{
					state->nativeStopCalling = false;
					state->cvState.WakeAllPendings();
				}
				retainedCallback->ReleaseSelfReference();
			});
			if (!ThreadPoolLite::Queue(finalize))
			{
				// The callback self-reference keeps all deferred state alive if the
				// operating system cannot start either asynchronous cleanup path.
				if (!Thread::CreateAndStart(finalize)) return;
			}
		}

		static WaitForClientResult OnSocketClientConnected(Ptr<Lifecycle> state, IAsyncSocketConnection* connection)
		{
			HttpRequestCallbackDomain::CallbackFrame callbackFrame(state->callbackDomain);
			HttpRequestServer* owner = nullptr;
			CS_LOCK(state->lockState)
			{
				if (!state->stopStarted)
				{
					owner = state->owner;
				}
			}
			if (!owner)
			{
				return WaitForClientResult::Reject;
			}

			Ptr<IHttpRequestTimeoutController> timeoutController;
			try
			{
				if (state->timeoutControllerFactory)
				{
					timeoutController = state->timeoutControllerFactory();
					CHECK_ERROR(timeoutController, L"The HTTP request timeout controller factory returned null.");
				}
			}
			catch (...)
			{
				return WaitForClientResult::Reject;
			}

			auto httpConnection = Ptr(new HttpRequestConnection(
				connection,
				HttpRequestConnectionDirection::Server,
				state->callbackDomain,
				timeoutController
				));
			auto connectionObject = httpConnection.Obj();
			httpConnection->RetainUntilStopped(httpConnection, Func<void()>([state, connectionObject]()
			{
				CS_LOCK(state->lockState)
				{
					state->connections.Remove(connectionObject);
					state->cvState.WakeAllPendings();
				}
			}));
			bool invoke = false;
			CS_LOCK(state->lockState)
			{
				if (!state->stopStarted && state->owner == owner)
				{
					state->connections.Add(httpConnection);
					invoke = true;
				}
			}
			if (!invoke)
			{
				httpConnection->StopWithRetainedAdapter(httpConnection);
				return WaitForClientResult::Reject;
			}

			auto result = WaitForClientResult::Reject;
			try
			{
				result = owner->OnClientConnected(httpConnection.Obj());
			}
			catch (...)
			{
				result = WaitForClientResult::Reject;
			}

			bool accepted = false;
			CS_LOCK(state->lockState)
			{
				accepted = result == WaitForClientResult::Accept && !state->stopStarted;
				if (!accepted)
				{
					state->connections.Remove(httpConnection.Obj());
				}
			}
			if (!accepted)
			{
				httpConnection->StopWithRetainedAdapter(httpConnection);
			}
			return accepted ? WaitForClientResult::Accept : WaitForClientResult::Reject;
		}

		static void OnSocketServerStopped(Ptr<Lifecycle> state)
		{
			HttpRequestCallbackDomain::CallbackFrame callbackFrame(state->callbackDomain);
			HttpRequestServer* owner = nullptr;
			CS_LOCK(state->lockState)
			{
				if (!state->stopStarted && !state->unexpectedStopNotified)
				{
					state->unexpectedStopNotified = true;
					owner = state->owner;
				}
			}
			if (owner)
			{
				try
				{
					owner->OnServerStopped();
				}
				catch (...)
				{
				}
			}
		}

	public:
		Impl(
			HttpRequestServer* owner,
			Ptr<IAsyncSocketServer> server,
			const Func<Ptr<IHttpRequestTimeoutController>()>& timeoutControllerFactory
			)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::HttpRequestServer::HttpRequestServer(Ptr<IAsyncSocketServer>)#"
			CHECK_ERROR(server, ERROR_MESSAGE_PREFIX L"Requires a server.");
			lifecycle = Ptr(new Lifecycle(owner, server, timeoutControllerFactory));
			callback = Ptr(new SocketServerCallback(lifecycle));
#undef ERROR_MESSAGE_PREFIX
		}

		~Impl()
		{
			Destroy();
		}

		void Start()
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::HttpRequestServer::Start()#"
			auto state = lifecycle;
			bool canStart = false;
			CS_LOCK(state->lockState)
			{
				if (!state->startCalled && !state->stopStarted)
				{
					state->startCalled = true;
					canStart = true;
				}
			}
			CHECK_ERROR(canStart, ERROR_MESSAGE_PREFIX L"Can only be called once before stopping.");

			callback->InitializeSelf(callback);
			try
			{
				state->server->Start(callback.Obj());
			}
			catch (...)
			{
				try
				{
					Stop();
				}
				catch (...)
				{
				}
				throw;
			}
#undef ERROR_MESSAGE_PREFIX
		}

		void Stop()
		{
			auto state = lifecycle;
			auto retainedCallback = callback;
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
				state->server->Stop();
				StopConnections(state);
				state->callbackDomain->WaitForCallbacks(firstStop ? callbackDepth : 0);
				deferFinalization = firstStop && callbackDepth > 0;
				if (!deferFinalization)
				{
					FinalizeDestroyedOwner(state);
					retainedCallback->ReleaseSelfReference();
				}
			}
			catch (...)
			{
				if (!firstStop || callbackDepth == 0)
				{
					retainedCallback->ReleaseSelfReference();
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
				QueueDeferredStop(state, retainedCallback);
			}
		}

		void Destroy()
		{
			auto state = lifecycle;
			bool execute = false;
			CS_LOCK(state->lockState)
			{
				if (!state->destroyStarted)
				{
					state->destroyStarted = true;
					execute = true;
				}
			}
			if (!execute)
			{
				return;
			}

			Stop();
			StopConnections(state, true);
			CS_LOCK(state->lockState)
			{
				state->destroyAdaptersRetained = true;
			}
			if (state->callbackDomain->CurrentCallbackDepth() == 0)
			{
				state->callbackDomain->WaitForCallbacks(0);
				FinalizeDestroyedOwner(state);
			}
		}

		bool IsStopped()
		{
			return lifecycle->server->IsStopped();
		}
	};

/***********************************************************************
HttpRequestServer
***********************************************************************/

	HttpRequestServer::HttpRequestServer(Ptr<IAsyncSocketServer> server)
		: HttpRequestServer(server, {})
	{
	}

	HttpRequestServer::HttpRequestServer(
		Ptr<IAsyncSocketServer> server,
		const Func<Ptr<IHttpRequestTimeoutController>()>& timeoutControllerFactory
		)
		: impl(Ptr(new Impl(this, server, timeoutControllerFactory)))
	{
	}

	HttpRequestServer::~HttpRequestServer()
	{
		impl->Destroy();
	}

	WaitForClientResult HttpRequestServer::OnClientConnected(IHttpRequestConnection*)
	{
		return WaitForClientResult::Accept;
	}

	void HttpRequestServer::OnServerStopped()
	{
		Stop();
	}

	void HttpRequestServer::Start()
	{
		impl->Start();
	}

	void HttpRequestServer::Stop()
	{
		impl->Stop();
	}

	bool HttpRequestServer::IsStopped()
	{
		return impl->IsStopped();
	}
}
