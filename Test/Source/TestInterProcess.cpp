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
				dest += itow(item.Length()) + L":" + item;
			}
		}

		static void Deserialize(const ContextType&, const DestType& dest, SourceType& source)
		{
			source.Clear();
			const wchar_t* reading = dest.Buffer();
			const wchar_t* end = reading + dest.Length();
			while (reading < end)
			{
				auto delimiter = wcschr(reading, L':');
				CHECK_ERROR(delimiter && delimiter < end, L"WStringListSerializer received an invalid package.");
				auto length = wtoi(WString::CopyFrom(reading, (vint)(delimiter - reading)));
				CHECK_ERROR(length >= 0 && delimiter + 1 + length <= end, L"WStringListSerializer received an invalid package length.");
				source.Add(WString::CopyFrom(delimiter + 1, length));
				reading = delimiter + 1 + length;
			}
		}
	};

	struct ChannelChatData
	{
		EventObject						eventServer, eventTom, eventJerry;
		SpinLock						lockServer;
		vint							clientId1 = -1;
		vint							clientId2 = -1;
		vint							serverClientId = -1;
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
	{
		using Base = NetworkProtocolChannelServer<WString, WStringListSerializer>;

	public:
		ChannelServer(Ptr<INetworkProtocolServer> server)
			: Base(server)
		{
		}

		bool OnClientConnected(vint clientId, const IChannelClient<WString>::ChannelNameList& availableChannels) override
		{
			CHECK_ERROR(availableChannels.Contains(ChatChannelName), L"Channel client should provide the chat channel.");
			return true;
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
		vint							serverClientId = -1;
		vint							anotherClientId = -1;

	public:
		ChannelClientBase(Ptr<INetworkProtocolClient> client, ChannelChatData& _chatData)
			: Base(client)
			, chatData(&_chatData)
		{
			InitializeChannel();
		}

		ChannelClientBase(ChannelChatData& _chatData)
			: Base()
			, chatData(&_chatData)
		{
			InitializeChannel();
		}

		const IChannelClient<WString>::ChannelNameList& OnGetChannelNames() override
		{
			return channelNames.Keys();
		}

		void OnConnected(vint clientId) override
		{
			this->clientId = clientId;
		}

	protected:
		void InitializeChannel()
		{
			channelNames.Add(ChatChannelName, nullptr);
			auto&& channels = GetChannels();
			auto index = channels.Keys().IndexOf(ChatChannelName);
			CHECK_ERROR(index != -1, L"Channel client should provide the chat channel.");
			channel = channels.Values()[index];
			channel->Initialize(this);
		}

		void RememberClientIds(vint senderClientId, const WString& package)
		{
			CHECK_ERROR(senderClientId > 0 && senderClientId != clientId, L"Channel client should receive client ids from the server channel client.");
			auto firstDelimiter = wcschr(package.Buffer(), L';');
			CHECK_ERROR(firstDelimiter, L"Channel client should receive two client ids.");
			auto secondBegin = firstDelimiter + 1;
			auto secondDelimiter = wcschr(secondBegin, L';');
			CHECK_ERROR(secondDelimiter && secondDelimiter[1] == 0, L"Channel client should receive two client ids.");

			auto clientId1 = wtoi(WString::CopyFrom(package.Buffer(), (vint)(firstDelimiter - package.Buffer())));
			auto clientId2 = wtoi(WString::CopyFrom(secondBegin, (vint)(secondDelimiter - secondBegin)));
			CHECK_ERROR(clientId1 > 0 && clientId2 > 0 && clientId1 != clientId2, L"Channel client should receive valid client ids.");
			CHECK_ERROR(clientId == clientId1 || clientId == clientId2, L"Channel client should find itself in the received client ids.");

			serverClientId = senderClientId;
			anotherClientId = clientId == clientId1 ? clientId2 : clientId1;
		}

		void SendTo(vint receiverClientId, const WString& package)
		{
			bool disconnected = false;
			channel->SendToClient(clientId, receiverClientId, package);
			channel->BatchWrite(disconnected);
			CHECK_ERROR(!disconnected, L"Channel client should be connected when sending.");
		}
	};

	class ServerChannelClient : public ChannelClientBase
	{
	private:
		vint							clientId1 = -1;
		vint							clientId2 = -1;

	public:
		ServerChannelClient(ChannelChatData& chatData, vint _clientId1, vint _clientId2)
			: ChannelClientBase(chatData)
			, clientId1(_clientId1)
			, clientId2(_clientId2)
		{
		}

		void OnConnected(vint clientId) override
		{
			ChannelClientBase::OnConnected(clientId);
			{
				SPIN_LOCK(chatData->lockServer)
				{
					chatData->clientId1 = clientId1;
					chatData->clientId2 = clientId2;
					chatData->serverClientId = clientId;
				}
			}

			bool disconnected = false;
			channel->BroadcastFromClient(clientId, itow(clientId1) + L";" + itow(clientId2) + L";");
			channel->BatchWrite(disconnected);
			CHECK_ERROR(!disconnected, L"Server channel client should broadcast client ids.");
		}

		void OnRead(vint senderClientId, const WString& package) override
		{
			CHECK_ERROR(package == L"Stop", L"Server channel client should only receive Stop.");
			bool stopServer = false;
			SPIN_LOCK(chatData->lockServer)
			{
				if (senderClientId == clientId1)
				{
					chatData->client1Stopped = true;
				}
				else if (senderClientId == clientId2)
				{
					chatData->client2Stopped = true;
				}
				else
				{
					CHECK_FAIL(L"Server channel client should only receive Stop from known clients.");
				}
				stopServer = chatData->client1Stopped && chatData->client2Stopped;
			}

			if (stopServer)
			{
				chatData->eventServer.Signal();
			}
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
				RememberClientIds(senderClientId, package);
				SendTo(anotherClientId, L"Hello");
			}
			else if (package == L"Good")
			{
				CHECK_ERROR(senderClientId == anotherClientId, L"Tom should receive Good from another client.");
				SendTo(serverClientId, L"Stop");
				chatData->eventTom.Signal();
			}
			else
			{
				CHECK_FAIL(L"Tom received an unexpected package.");
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
				RememberClientIds(senderClientId, package);
			}
			else if (package == L"Hello")
			{
				CHECK_ERROR(senderClientId == anotherClientId, L"Jerry should receive Hello from another client.");
				SendTo(anotherClientId, L"Good");
				SendTo(serverClientId, L"Stop");
				chatData->eventJerry.Signal();
			}
			else
			{
				CHECK_FAIL(L"Jerry received an unexpected package.");
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
				auto server = Ptr(new ChannelServer(createServer()));
				auto clientId1 = server->WaitForClient();
				auto clientId2 = server->WaitForClient();
				CHECK_ERROR(clientId1 != clientId2, L"Channel server should assign different client ids.");
				auto serverClient = Ptr(new ServerChannelClient(chatData, clientId1, clientId2));
				auto serverClientId = server->ConnectLocalClient(serverClient);
				CHECK_ERROR(serverClientId > 0 && serverClientId != clientId1 && serverClientId != clientId2, L"Channel server should assign a different id to the server channel client.");
				CHECK_ERROR(server->IsLocalClient(serverClientId), L"Channel server should recognize the server channel client as local.");
				CHECK_ERROR(server->GetClientIds().Count() == 3, L"Channel server should have three client ids.");
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
