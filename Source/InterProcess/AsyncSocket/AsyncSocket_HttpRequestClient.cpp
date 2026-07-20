#include "AsyncSocket_HttpRequestClient.h"

namespace vl::inter_process::async_tcp_socket
{
/***********************************************************************
HttpRequestClient::Impl
***********************************************************************/

	class HttpRequestClient::Impl : public Object
	{
	private:
		class DeferredClientRelease : public Object
		{
		private:
			CriticalSection					lockState;
			Ptr<DeferredClientRelease>		selfReference;
			Ptr<IAsyncSocketClient>			retainedClient;

		public:
			DeferredClientRelease(Ptr<IAsyncSocketClient> client)
				: retainedClient(client)
			{
			}

			void InitializeSelf(Ptr<DeferredClientRelease> self)
			{
				CS_LOCK(lockState)
				{
					selfReference = self;
				}
			}

			void Run()
			{
				Ptr<IAsyncSocketClient> client;
				CS_LOCK(lockState)
				{
					client = retainedClient;
				}
				try
				{
					client->GetConnection()->Stop();
				}
				catch (...)
				{
				}
				CS_LOCK(lockState)
				{
					retainedClient = nullptr;
					selfReference = nullptr;
				}
			}
		};

		Ptr<IAsyncSocketClient>				client;
		Ptr<HttpRequestConnection>			connection;

		static void QueueDeferredRelease(Ptr<DeferredClientRelease> deferredRelease)
		{
			auto finalize = Func<void()>([deferredRelease]()
			{
				deferredRelease->Run();
			});
			if (!ThreadPoolLite::Queue(finalize))
			{
				// The holder self-reference keeps the native client alive if the
				// operating system cannot start either asynchronous cleanup path.
				if (!Thread::CreateAndStart(finalize)) return;
			}
		}

	public:
		Impl(Ptr<IAsyncSocketClient> _client)
			: client(_client)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::HttpRequestClient::HttpRequestClient(Ptr<IAsyncSocketClient>)#"
			CHECK_ERROR(client, ERROR_MESSAGE_PREFIX L"Requires a client.");
			connection = Ptr(new HttpRequestConnection(
				client->GetConnection(),
				HttpRequestConnectionDirection::Client,
				nullptr,
				nullptr,
				true
				));
#undef ERROR_MESSAGE_PREFIX
		}

		~Impl()
		{
			auto deferFinalization = connection->IsInsideCallback();
			Ptr<DeferredClientRelease> deferredRelease;
			if (deferFinalization)
			{
				deferredRelease = Ptr(new DeferredClientRelease(client));
				deferredRelease->InitializeSelf(deferredRelease);
			}
			connection->StopWithRetainedAdapter(connection);
			if (deferFinalization)
			{
				QueueDeferredRelease(deferredRelease);
			}
		}

		IHttpRequestConnection* GetConnection()
		{
			return connection.Obj();
		}

		void WaitForServer()
		{
			client->WaitForServer();
		}

		ClientStatus GetStatus()
		{
			return client->GetStatus();
		}
	};

/***********************************************************************
HttpRequestClient
***********************************************************************/

	HttpRequestClient::HttpRequestClient(Ptr<IAsyncSocketClient> client)
		: impl(Ptr(new Impl(client)))
	{
	}

	HttpRequestClient::~HttpRequestClient()
	{
	}

	IHttpRequestConnection* HttpRequestClient::GetConnection()
	{
		return impl->GetConnection();
	}

	void HttpRequestClient::WaitForServer()
	{
		impl->WaitForServer();
	}

	ClientStatus HttpRequestClient::GetStatus()
	{
		return impl->GetStatus();
	}
}
