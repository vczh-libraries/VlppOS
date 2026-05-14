#include "../../Source/InterProcess/TextNetworkProtocol.h"
#ifdef VCZH_MSVC
#include "../../Source/InterProcess/Windows/NamedPipe.Windows.h"
#include "../../Source/InterProcess/Windows/HttpClient.Windows.h"
#include "../../Source/InterProcess/Windows/HttpServer.Windows.h"
#endif

using namespace vl;
using namespace vl::collections;
using namespace vl::inter_process;

namespace mynamespace
{
	class TimeoutThread : public Thread
	{
	public:
		atomic_vint			threadCounter = 0;
		bool				timeout = false;

	protected:

		void Run() override
		{
			auto startTime = DateTime::LocalTime();
			while (DateTime::LocalTime().osMilliseconds - startTime.osMilliseconds < 5000)
			{
				if (threadCounter == 3) return;
			}
			timeout = true;
		}
	};

	struct ChatData
	{
		SpinLock						lockServer, lockTom, lockJerry;
		EventObject						eventServer, eventTom, eventJerry;
		List<WString>					chatTom, chatJerry;
		INetworkProtocolConnection*		connectionTom = nullptr;
		INetworkProtocolConnection*		connectionJerry = nullptr;

		ChatData()
		{
			eventServer.CreateManualUnsignal(false);
			eventTom.CreateManualUnsignal(false);
			eventJerry.CreateManualUnsignal(false);
		}
	};

	class NetworkProtocolCallback : public Object, public virtual INetworkProtocolCallback
	{
	protected:
		ChatData*						chatData = nullptr;
		INetworkProtocolConnection*		connection = nullptr;

	public:
		NetworkProtocolCallback(ChatData& _chatData)
			:chatData(&_chatData)
		{
		}

		void OnReadError(const WString& error) override {}
		void OnConnected() override {}
		void OnDisconnected() override {}

		void OnInstalled(INetworkProtocolConnection* _connection) override
		{
			connection = _connection;
		}
	};

	class ServerCallback : public NetworkProtocolCallback
	{
	public:
		ServerCallback(ChatData& _chatData)
			:NetworkProtocolCallback(_chatData)
		{
		}

		void OnReadString(const WString& str) override
		{
		}
	};

	class TomCallback : public NetworkProtocolCallback
	{
	public:
		TomCallback(ChatData& _chatData)
			:NetworkProtocolCallback(_chatData)
		{
		}

		void OnReadString(const WString& str) override
		{
		}
	};

	class JerryCallback : public NetworkProtocolCallback
	{
	public:
		JerryCallback(ChatData& _chatData)
			:NetworkProtocolCallback(_chatData)
		{
		}

		void OnReadString(const WString& str) override
		{
		}
	};

	void RunTextNetworkProtocol(
		Func<Ptr<INetworkProtocolServer>()> createServer,
		Func<Ptr<INetworkProtocolClient>()> createClient
		)
	{
		auto timeoutThread = Ptr(new TimeoutThread);
		ChatData chatData;

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				ServerCallback callback1(chatData), callback2(chatData);
				auto server = createServer();
				server->WaitForClient()->InstallCallback(&callback1);
				server->WaitForClient()->InstallCallback(&callback2);
				chatData.eventServer.Wait();
				Thread::Sleep(1000);
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				TomCallback callback(chatData);
				auto client = createClient();
				client->GetConnection()->InstallCallback(&callback);
				client->WaitForServer();
				chatData.eventTom.Wait();
				Thread::Sleep(1000);
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				JerryCallback callback(chatData);
				auto client = createClient();
				client->GetConnection()->InstallCallback(&callback);
				client->WaitForServer();
				chatData.eventJerry.Wait();
				Thread::Sleep(1000);
			}
			timeoutThread->threadCounter++;
		});

		timeoutThread->Start();
		timeoutThread->Wait();
		TEST_ASSERT(!timeoutThread->timeout);
	}
}
using namespace mynamespace;

TEST_FILE
{
#ifdef VCZH_MSVC
	TEST_CASE(L"NamedPipe")
	{
		RunTextNetworkProtocol(
			[]()->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new NamedPipeServer(L"VlppOSTestPipe")); },
			[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new NamedPipeClient(L"VlppOSTestPipe")); }
		);
	});

	TEST_CASE(L"HttpServer")
	{
		RunTextNetworkProtocol(
			[]()->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new HttpServer(L"/VlppOSTestHttpServer", 8765)); },
			[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new HttpClient(L"/VlppOSTestHttpServer", 8765)); }
		);
	});
#endif
}