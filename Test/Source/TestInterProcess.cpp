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
				CHECK_ERROR(!server->IsStopped(), L"Server should not be stopped before accepting clients.");
				auto connection1 = server->WaitForClient();
				CHECK_ERROR(!server->IsStopped(), L"Server should not be stopped after accepting the first client.");
				auto connection2 = server->WaitForClient();
				CHECK_ERROR(!server->IsStopped(), L"Server should not be stopped after accepting the second client.");
				connection1->InstallCallback(&callback1);
				connection2->InstallCallback(&callback2);
				connection1->BeginReadingLoopUnsafe();
				connection2->BeginReadingLoopUnsafe();
				chatData.eventServer.Wait();
				CHECK_ERROR(!server->IsStopped(), L"Server should not be stopped before sleeping.");
				Thread::Sleep(1000);
				CHECK_ERROR(!server->IsStopped(), L"Server should not be stopped after sleeping.");
				server->Stop();
				CHECK_ERROR(server->IsStopped(), L"Server should be stopped after Stop.");
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				TomCallback callback(chatData);
				auto client = createClient();
				CHECK_ERROR(client->GetStatus() == ClientStatus::Ready, L"Client should be ready before connecting.");
				client->GetConnection()->InstallCallback(&callback);
				client->WaitForServer();
				CHECK_ERROR(client->GetStatus() == ClientStatus::Connected, L"Client should be connected after WaitForServer.");
				client->GetConnection()->BeginReadingLoopUnsafe();
				chatData.eventTom.Wait();
				CHECK_ERROR(client->GetStatus() == ClientStatus::Connected, L"Client should still be connected before sleeping.");
				Thread::Sleep(1000);
				CHECK_ERROR(client->GetStatus() == ClientStatus::Connected, L"Client should still be connected after sleeping.");
				client->GetConnection()->Stop();
				CHECK_ERROR(client->GetStatus() == ClientStatus::Disconnected, L"Client should be disconnected after Stop.");
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				JerryCallback callback(chatData);
				auto client = createClient();
				CHECK_ERROR(client->GetStatus() == ClientStatus::Ready, L"Client should be ready before connecting.");
				client->GetConnection()->InstallCallback(&callback);
				client->WaitForServer();
				CHECK_ERROR(client->GetStatus() == ClientStatus::Connected, L"Client should be connected after WaitForServer.");
				client->GetConnection()->BeginReadingLoopUnsafe();
				chatData.eventJerry.Wait();
				CHECK_ERROR(client->GetStatus() == ClientStatus::Connected, L"Client should still be connected before sleeping.");
				Thread::Sleep(1000);
				CHECK_ERROR(client->GetStatus() == ClientStatus::Connected, L"Client should still be connected after sleeping.");
				client->GetConnection()->Stop();
				CHECK_ERROR(client->GetStatus() == ClientStatus::Disconnected, L"Client should be disconnected after Stop.");
			}
			timeoutThread->threadCounter++;
		});

		timeoutThread->Start();
		timeoutThread->Wait();

		// Failure here means not all threads have stopped, at least one may be blocked forever.
		// It is highly recommended to debug the test case when it fails.
		// To debug this, you can change the wait time from 5000 to 500000, so the timeout will not trigger in 500 seconds
		// By doing this, you have plenty of time to use the debugger.
		// Remember to change it back after finishing debugging.
		TEST_ASSERT(!timeoutThread->timeout);
	}
}

namespace mynamespace
{
	constexpr const wchar_t* ChatChannelName = L"Chat";

	struct WStringListSerializer
	{
		using SourceType = List<WString>;
		using DestType = WString;
		using ContextType = std::nullptr_t;

		static void Serialize(const ContextType&, const SourceType& source, DestType& dest)
		{
			dest = WString::Empty;
			for (auto&& item : source)
			{
				if (dest.Length() > 0)
				{
					dest += L";";
				}
				dest += item;
			}
		}

		static void Deserialize(const ContextType&, const DestType& dest, SourceType& source)
		{
			source.Clear();
			if (dest.Length() == 0)
			{
				return;
			}

			const wchar_t* reading = dest.Buffer();
			while (true)
			{
				auto delimiter = wcschr(reading, L';');
				source.Add(
					delimiter
					? WString::CopyFrom(reading, (vint)(delimiter - reading))
					: WString::CopyFrom(reading, (vint)wcslen(reading))
					);
				if (!delimiter)
				{
					break;
				}
				reading = delimiter + 1;
			}
		}
	};

	struct ChannelChatData
	{
		EventObject						eventServer, eventTom, eventJerry;
		SpinLock						lockServer;
		vint							clientId1 = -1;
		vint							clientId2 = -1;
		bool							anotherClientIdSent = false;
		bool							client1Stopped = false;
		bool							client2Stopped = false;

		ChannelChatData()
		{
			eventServer.CreateManualUnsignal(false);
			eventTom.CreateManualUnsignal(false);
			eventJerry.CreateManualUnsignal(false);
		}
	};

	class ChannelServer
		: public NetworkProtocolChannelServer<WString, WStringListSerializer>
		, public virtual IChannelReader<WString>
	{
		using Base = NetworkProtocolChannelServer<WString, WStringListSerializer>;

	private:
		ChannelChatData*				chatData = nullptr;
		IChannel<WString>*				channel = nullptr;

	public:
		ChannelServer(Ptr<INetworkProtocolServer> server, ChannelChatData& _chatData)
			: Base(server)
			, chatData(&_chatData)
		{
			channel = GetChannel(ChatChannelName);
			channel->Initialize(this);
		}

		bool OnClientConnected(vint clientId, const IChannelClient<WString>::ChannelNameList& availableChannels) override
		{
			CHECK_ERROR(availableChannels.Contains(ChatChannelName), L"Channel client should provide the chat channel.");
			return true;
		}

		void OnRead(vint senderClientId, const WString& package) override
		{
			bool sendAnotherClientId = false;
			bool stopServer = false;
			vint clientId1 = -1;
			vint clientId2 = -1;

			SPIN_LOCK(chatData->lockServer)
			{
				CHECK_ERROR(senderClientId != AdminClientId, L"Channel server should not receive a client message from AdminClientId.");
				if (package == L"Hello Server")
				{
					if (chatData->clientId1 == -1)
					{
						chatData->clientId1 = senderClientId;
					}
					else if (chatData->clientId1 != senderClientId)
					{
						CHECK_ERROR(chatData->clientId2 == -1 || chatData->clientId2 == senderClientId, L"Channel server should receive Hello Server from at most two clients.");
						chatData->clientId2 = senderClientId;
					}
				}
				else if (package == L"Stop")
				{
					if (senderClientId == chatData->clientId1)
					{
						chatData->client1Stopped = true;
					}
					else if (senderClientId == chatData->clientId2)
					{
						chatData->client2Stopped = true;
					}
					else
					{
						CHECK_FAIL(L"Channel server should only receive Stop from known clients.");
					}
				}

				if (chatData->clientId1 != -1 && chatData->clientId2 != -1 && !chatData->anotherClientIdSent)
				{
					chatData->anotherClientIdSent = true;
					sendAnotherClientId = true;
					clientId1 = chatData->clientId1;
					clientId2 = chatData->clientId2;
				}
				stopServer = chatData->client1Stopped && chatData->client2Stopped;
			}

			if (sendAnotherClientId)
			{
				bool disconnected = false;
				channel->SendToClient(AdminClientId, clientId1, itow(clientId2));
				channel->SendToClient(AdminClientId, clientId2, itow(clientId1));
				channel->BatchWrite(disconnected);
				CHECK_ERROR(!disconnected, L"Channel server should send another client id to both clients.");
			}
			if (stopServer)
			{
				chatData->eventServer.Signal();
			}
		}
	};

	class ChannelClientBase
		: public NetworkProtocolChannelClient<WString, WStringListSerializer>
		, public virtual IChannelReader<WString>
	{
		using Base = NetworkProtocolChannelClient<WString, WStringListSerializer>;

	protected:
		ChannelChatData*				chatData = nullptr;
		IChannelClient<WString>::ChannelMap
										channelNames;
		IChannel<WString>*				channel = nullptr;
		vint							clientId = -1;
		vint							anotherClientId = -1;

	public:
		ChannelClientBase(Ptr<INetworkProtocolClient> client, ChannelChatData& _chatData)
			: Base(client)
			, chatData(&_chatData)
		{
			channelNames.Add(ChatChannelName, nullptr);
			auto&& channels = GetChannels();
			auto index = channels.Keys().IndexOf(ChatChannelName);
			CHECK_ERROR(index != -1, L"Channel client should provide the chat channel.");
			channel = channels.Values()[index];
			channel->Initialize(this);
		}

		const IChannelClient<WString>::ChannelNameList& OnGetChannelNames() override
		{
			return channelNames.Keys();
		}

		void OnConnected(vint clientId) override
		{
			this->clientId = clientId;
			SendTo(AdminClientId, L"Hello Server");
		}

	protected:
		void RememberAnotherClientId(vint senderClientId, const WString& package)
		{
			CHECK_ERROR(senderClientId == AdminClientId, L"Channel client should receive another client id from AdminClientId.");
			anotherClientId = wtoi(package);
			CHECK_ERROR(anotherClientId > 0 && anotherClientId != clientId, L"Channel client should receive a valid another client id.");
		}

		void SendTo(vint receiverClientId, const WString& package)
		{
			bool disconnected = false;
			channel->SendToClient(clientId, receiverClientId, package);
			channel->BatchWrite(disconnected);
			CHECK_ERROR(!disconnected, L"Channel client should be connected when sending.");
		}
	};

	class TomChannelClient : public ChannelClientBase
	{
	public:
		TomChannelClient(Ptr<INetworkProtocolClient> client, ChannelChatData& chatData)
			: ChannelClientBase(client, chatData)
		{
		}

		void OnRead(vint senderClientId, const WString& package) override
		{
			if (anotherClientId == -1)
			{
				RememberAnotherClientId(senderClientId, package);
				SendTo(anotherClientId, L"Hello");
			}
			else if (package == L"Good")
			{
				CHECK_ERROR(senderClientId == anotherClientId, L"Tom should receive Good from another client.");
				SendTo(AdminClientId, L"Stop");
				chatData->eventTom.Signal();
			}
		}
	};

	class JerryChannelClient : public ChannelClientBase
	{
	public:
		JerryChannelClient(Ptr<INetworkProtocolClient> client, ChannelChatData& chatData)
			: ChannelClientBase(client, chatData)
		{
		}

		void OnRead(vint senderClientId, const WString& package) override
		{
			if (anotherClientId == -1)
			{
				RememberAnotherClientId(senderClientId, package);
			}
			else if (package == L"Hello")
			{
				CHECK_ERROR(senderClientId == anotherClientId, L"Jerry should receive Hello from another client.");
				SendTo(anotherClientId, L"Good");
				SendTo(AdminClientId, L"Stop");
				chatData->eventJerry.Signal();
			}
		}
	};

	void RunNetworkProtocolChannel(
		Func<Ptr<INetworkProtocolServer>()> createServer,
		Func<Ptr<INetworkProtocolClient>()> createClient
		)
	{
		auto timeoutThread = Ptr(new TimeoutThread);
		ChannelChatData chatData;

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				auto server = Ptr(new ChannelServer(createServer(), chatData));
				auto clientId1 = server->WaitForClient();
				auto clientId2 = server->WaitForClient();
				CHECK_ERROR(clientId1 != clientId2, L"Channel server should assign different client ids.");
				chatData.eventServer.Wait();
				Thread::Sleep(1000);
				server->Stop();
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				auto client = Ptr(new TomChannelClient(createClient(), chatData));
				client->WaitForServer();
				chatData.eventTom.Wait();
				Thread::Sleep(1000);
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				auto client = Ptr(new JerryChannelClient(createClient(), chatData));
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
	TEST_CASE(L"NamedPipe (NetworkProtocol)")
	{
		RunTextNetworkProtocol(
			[]()->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new NamedPipeServer(L"VlppOSTestPipe")); },
			[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new NamedPipeClient(L"VlppOSTestPipe")); }
		);
	});

	TEST_CASE(L"HttpServer (NetworkProtocol)")
	{
		RunTextNetworkProtocol(
			[]()->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new HttpServer(L"/VlppOSTestHttpServer", 8765)); },
			[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new HttpClient(L"/VlppOSTestHttpServer", 8765)); }
		);
	});

	TEST_CASE(L"NamedPipe (Channel)")
	{
		RunNetworkProtocolChannel(
			[]()->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new NamedPipeServer(L"VlppOSTestPipeChannel")); },
			[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new NamedPipeClient(L"VlppOSTestPipeChannel")); }
		);
	});

	TEST_CASE(L"HttpServer (Channel)")
	{
		RunNetworkProtocolChannel(
			[]()->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new HttpServer(L"/VlppOSTestHttpServerChannel", 8766)); },
			[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new HttpClient(L"/VlppOSTestHttpServerChannel", 8766)); }
		);
	});
#endif
}
