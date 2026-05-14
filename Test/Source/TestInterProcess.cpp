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
		EventObject						eventServer, eventTom, eventJerry;
		SpinLock						lockServer;
		INetworkProtocolConnection*		connectionTom = nullptr;
		INetworkProtocolConnection*		connectionJerry = nullptr;
		bool							tomStopped = false;
		bool							jerryStopped = false;

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
	protected:
		WString						name;

	public:
		ServerCallback(ChatData& _chatData)
			:NetworkProtocolCallback(_chatData)
		{
		}

		void OnReadString(const WString& str) override
		{
			if (str == L"Tom" || str == L"Jerry")
			{
				name = str;
				SPIN_LOCK(chatData->lockServer)
				{
					if (str == L"Tom") chatData->connectionTom = connection;
					if (str == L"Jerry") chatData->connectionJerry = connection;
					if (chatData->connectionTom && chatData->connectionJerry)
					{
						chatData->connectionTom->SendString(L"OK");
						chatData->connectionJerry->SendString(L"OK");
					}
				}
			}
			else if (str == L"Stop")
			{
				SPIN_LOCK(chatData->lockServer)
				{
					if (connection == chatData->connectionTom) chatData->tomStopped = true;
					if (connection == chatData->connectionJerry) chatData->jerryStopped = true;
					if (chatData->tomStopped && chatData->jerryStopped)
					{
						chatData->eventServer.Signal();
					}
				}
			}
			else
			{
				if (str.Length() >= 4 && str.Left(4) == L"Tom:")
				{
					chatData->connectionTom->SendString(name + L">" + str.Sub(4, str.Length() - 4));
				}
				if (str.Length() >= 6 && str.Left(6) == L"Jerry:")
				{
					chatData->connectionJerry->SendString(name + L">" + str.Sub(6, str.Length() - 6));
				}
			}
		}
	};

	class TomCallback : public NetworkProtocolCallback
	{
	public:
		TomCallback(ChatData& _chatData)
			:NetworkProtocolCallback(_chatData)
		{
		}

		void OnConnected() override
		{
			connection->SendString(L"Tom");
		}

		void OnReadString(const WString& str) override
		{
			if (str == L"OK")
			{
				connection->SendString(L"Jerry:Hello");
			}
			else if (str == L"Jerry>Good")
			{
				connection->SendString(L"Stop");
				chatData->eventTom.Signal();
			}
		}
	};

	class JerryCallback : public NetworkProtocolCallback
	{
	public:
		JerryCallback(ChatData& _chatData)
			:NetworkProtocolCallback(_chatData)
		{
		}

		void OnConnected() override
		{
			connection->SendString(L"Jerry");
		}

		void OnReadString(const WString& str) override
		{
			if(str==L"Tom>Hello")
			{
				connection->SendString(L"Tom:Good");
				connection->SendString(L"Stop");
				chatData->eventJerry.Signal();
			}
		}
	};

	void RunTextNetworkProtocol(
		Func<Ptr<INetworkProtocolServer>()> createServer,
		Func<Ptr<INetworkProtocolClient>()> createClient
		)
	{
		/*
		* This test case tests multiple inter-process communication implementations.
		* Each client will send its name to the server, when the server receives both, OK is sent back.
		* Tom when receiving OK, sends Hello to Jerry, when receiving Good, sends Stop to the Server and ends.
		* Jerry when receiving Hello, sends Good to Tom and Stop to the Server and ends.
		* Both sends Stop to the server, when both are received, the server ends.
		* 
		* Ending means signaling a perticular event, and each thread will wait for one more second to stop.
		* If the whole process cannot end in 5 seconds, the timeout thread will signal the test case and the test case will fail.
		*/
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

		// Failure here means not all threads have stopped, at least one may be blocked forever.
		// To debug this, you can change the wait time from 5000 to 500000, so the timeout will not trigger in 500 seconds
		// By doing this, you have plenty of time to use the debugger.
		// Remember to change it back after finishing debugging.
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