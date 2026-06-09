#include "../../Source/InterProcess/NetworkProtocolChannel.h"
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
	constexpr vint InterProcessTestRepeatCount = 20;

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

		// covers connectionTom, connectionJerry, tomStopped and jerryStopped
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
		void OnLocalError(const WString& error, bool fatal) override {}
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

	class TextServerCallbackHost
	{
	protected:
		ChatData*						chatData = nullptr;
		ServerCallback					callback1;
		ServerCallback					callback2;

		// covers acceptedConnections
		SpinLock						lockAcceptedConnections;
		vint							acceptedConnections = 0;

		WaitForClientResult AcceptTextConnection(INetworkProtocolConnection* connection)
		{
			ServerCallback* callback = nullptr;
			{
				SPIN_LOCK(lockAcceptedConnections)
				{
					if (acceptedConnections == 0)
					{
						callback = &callback1;
					}
					else if (acceptedConnections == 1)
					{
						callback = &callback2;
					}
					else
					{
						return WaitForClientResult::Reject;
					}
					acceptedConnections++;
				}
			}

			connection->InstallCallback(callback);
			connection->BeginReadingLoopUnsafe();
			return WaitForClientResult::Accept;
		}

	public:
		TextServerCallbackHost(ChatData& _chatData)
			: chatData(&_chatData)
			, callback1(_chatData)
			, callback2(_chatData)
		{
		}
	};

	void RunTextNetworkProtocol(
		Func<Ptr<INetworkProtocolServer>(ChatData&)> createServer,
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
				auto server = createServer(chatData);
				server->Start();
				CHECK_ERROR(!server->IsStopped(), L"Server should not be stopped before accepting clients.");
				chatData.eventServer.Wait();
				CHECK_ERROR(!server->IsStopped(), L"Server should not be stopped before Stop.");
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
				CHECK_ERROR(client->GetStatus() == ClientStatus::Connected, L"Client should still be connected before Stop.");
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
				CHECK_ERROR(client->GetStatus() == ClientStatus::Connected, L"Client should still be connected before Stop.");
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
		EventObject						eventClientsConnected, eventServer, eventTom, eventJerry;

		// covers clientId1, clientId2, serverClientId, client1Stopped, client2Stopped, clientId1ReceivedHello and clientId2ReceivedHello
		SpinLock						lockServer;
		vint							clientId1 = -1;
		vint							clientId2 = -1;
		vint							serverClientId = -1;
		bool							client1Stopped = false;
		bool							client2Stopped = false;
		bool							clientId1ReceivedHello = false;
		bool							clientId2ReceivedHello = false;

		ChannelChatData()
		{
			eventClientsConnected.CreateManualUnsignal(false);
			eventServer.CreateManualUnsignal(false);
			eventTom.CreateManualUnsignal(false);
			eventJerry.CreateManualUnsignal(false);
		}
	};

	class ChannelServer
		: public NetworkProtocolChannelServer<WString, WStringListSerializer>
	{
		using Base = NetworkProtocolChannelServer<WString, WStringListSerializer>;

	private:
		ChannelChatData*				chatData = nullptr;

	public:
		using Base::OnClientConnected;

		ChannelServer(ChannelChatData& _chatData)
			: chatData(&_chatData)
		{
		}

		WaitForClientResult OnClientConnected(vint clientId, const IChannelClient<WString>::ChannelNameList& availableChannels) override
		{
			CHECK_ERROR(availableChannels.Contains(ChatChannelName), L"Channel client should provide the chat channel.");
			return WaitForClientResult::Accept;
		}
	};

	using NetworkChannelClient = NetworkProtocolChannelClient<WString, WStringListSerializer>;
	using LocalNetworkChannelClient = NetworkProtocolLocalChannelClient<WString, WStringListSerializer>;

	template<typename TBase>
	class ChannelClientBase
		: public TBase
		, public virtual IChannelReader<WString>
	{
		using Base = TBase;

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
			auto&& channels = this->GetChannels();
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
			channel->SendToClient(receiverClientId, package);
			channel->BatchWrite(disconnected);
			CHECK_ERROR(!disconnected, L"Channel client should be connected when sending.");
		}

		void RememberHelloFromServer(vint senderClientId)
		{
			CHECK_ERROR(senderClientId == serverClientId, L"Channel client should receive server hello from the server channel client.");
			SPIN_LOCK(chatData->lockServer)
			{
				if (clientId == chatData->clientId1)
				{
					chatData->clientId1ReceivedHello = true;
				}
				else if (clientId == chatData->clientId2)
				{
					chatData->clientId2ReceivedHello = true;
				}
				else
				{
					CHECK_FAIL(L"Channel client should record server hello for a known client id.");
				}
			}
		}
	};

	class ServerChannelClient : public ChannelClientBase<LocalNetworkChannelClient>
	{
	private:
		vint							clientId1 = -1;
		vint							clientId2 = -1;

	public:
		ServerChannelClient(ChannelChatData& chatData, vint _clientId1, vint _clientId2)
			: ChannelClientBase<LocalNetworkChannelClient>(chatData)
			, clientId1(_clientId1)
			, clientId2(_clientId2)
		{
		}

		void OnConnected(vint clientId) override
		{
			ChannelClientBase<LocalNetworkChannelClient>::OnConnected(clientId);
			{
				SPIN_LOCK(chatData->lockServer)
				{
					chatData->clientId1 = clientId1;
					chatData->clientId2 = clientId2;
					chatData->serverClientId = clientId;
				}
			}

			bool disconnected = false;
			channel->BroadcastFromClient(itow(clientId1) + L";" + itow(clientId2) + L";");

			collections::List<vint> blockedReceivers;
			blockedReceivers.Add(clientId1);
			channel->BroadcastFromClient(L"Hello Jerry from Server", blockedReceivers);
			blockedReceivers.Clear();
			blockedReceivers.Add(clientId2);
			channel->BroadcastFromClient(L"Hello Tom from Server", blockedReceivers);

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

	class TomChannelClient : public ChannelClientBase<NetworkChannelClient>
	{
	private:
		bool							receivedGood = false;
		bool							receivedHelloFromServer = false;
		bool							stopped = false;

		void TryStop()
		{
			if (receivedGood && receivedHelloFromServer && !stopped)
			{
				stopped = true;
				SendTo(serverClientId, L"Stop");
				chatData->eventTom.Signal();
			}
		}

	public:
		TomChannelClient(Ptr<INetworkProtocolClient> client, ChannelChatData& chatData)
			: ChannelClientBase<NetworkChannelClient>(client, chatData)
		{
		}

		void OnConnected(vint clientId) override
		{
			ChannelClientBase<NetworkChannelClient>::OnConnected(clientId);
			SPIN_LOCK(chatData->lockServer)
			{
				chatData->clientId1 = clientId;
				if (chatData->clientId2 != -1)
				{
					chatData->eventClientsConnected.Signal();
				}
			}
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
				receivedGood = true;
				TryStop();
			}
			else if (package == L"Hello Tom from Server")
			{
				RememberHelloFromServer(senderClientId);
				receivedHelloFromServer = true;
				TryStop();
			}
			else
			{
				CHECK_FAIL(L"Tom received an unexpected package.");
			}
		}
	};

	class JerryChannelClient : public ChannelClientBase<NetworkChannelClient>
	{
	private:
		bool							receivedHello = false;
		bool							receivedHelloFromServer = false;
		bool							stopped = false;

		void TryStop()
		{
			if (receivedHello && receivedHelloFromServer && !stopped)
			{
				stopped = true;
				SendTo(serverClientId, L"Stop");
				chatData->eventJerry.Signal();
			}
		}

	public:
		JerryChannelClient(Ptr<INetworkProtocolClient> client, ChannelChatData& chatData)
			: ChannelClientBase<NetworkChannelClient>(client, chatData)
		{
		}

		void OnConnected(vint clientId) override
		{
			ChannelClientBase<NetworkChannelClient>::OnConnected(clientId);
			SPIN_LOCK(chatData->lockServer)
			{
				chatData->clientId2 = clientId;
				if (chatData->clientId1 != -1)
				{
					chatData->eventClientsConnected.Signal();
				}
			}
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
				receivedHello = true;
				TryStop();
			}
			else if (package == L"Hello Jerry from Server")
			{
				RememberHelloFromServer(senderClientId);
				receivedHelloFromServer = true;
				TryStop();
			}
			else
			{
				CHECK_FAIL(L"Jerry received an unexpected package.");
			}
		}
	};

	void RunNetworkProtocolChannel(
		Func<Ptr<IChannelServer<WString>>(ChannelChatData&)> createServer,
		Func<Ptr<INetworkProtocolClient>()> createClient
		)
	{
		auto timeoutThread = Ptr(new TimeoutThread);
		ChannelChatData chatData;

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				auto server = createServer(chatData);
				server->Start();
				chatData.eventClientsConnected.Wait();
				vint clientId1 = -1;
				vint clientId2 = -1;
				SPIN_LOCK(chatData.lockServer)
				{
					clientId1 = chatData.clientId1;
					clientId2 = chatData.clientId2;
				}
				CHECK_ERROR(clientId1 > 0 && clientId2 > 0 && clientId1 != clientId2, L"Channel server should assign different client ids.");
				auto serverClient = Ptr(new ServerChannelClient(chatData, clientId1, clientId2));
				auto serverClientId = server->ConnectLocalClient(serverClient);
				CHECK_ERROR(serverClientId > 0 && serverClientId != clientId1 && serverClientId != clientId2, L"Channel server should assign a different id to the server channel client.");
				CHECK_ERROR(server->IsLocalClient(serverClientId), L"Channel server should recognize the server channel client as local.");
				CHECK_ERROR(server->GetClientIds().Count() == 3, L"Channel server should have three client ids.");
				chatData.eventServer.Wait();
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
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				auto client = Ptr(new JerryChannelClient(createClient(), chatData));
				client->WaitForServer();
				chatData.eventJerry.Wait();
			}
			timeoutThread->threadCounter++;
		});

		timeoutThread->Start();
		timeoutThread->Wait();

		TEST_ASSERT(!timeoutThread->timeout);
		SPIN_LOCK(chatData.lockServer)
		{
			TEST_ASSERT(chatData.clientId1ReceivedHello);
			TEST_ASSERT(chatData.clientId2ReceivedHello);
		}
	}
}
using namespace mynamespace;

#ifdef VCZH_MSVC

namespace mynamespace
{
	class NamedPipeTextServer : protected TextServerCallbackHost, public NamedPipeServer
	{
	public:
		NamedPipeTextServer(ChatData& chatData, const WString& pipeName)
			: TextServerCallbackHost(chatData)
			, NamedPipeServer(pipeName)
		{
		}

		WaitForClientResult OnClientConnected(INetworkProtocolConnection* connection) override
		{
			return AcceptTextConnection(connection);
		}
	};

	class HttpTextServer : protected TextServerCallbackHost, public HttpServer
	{
	public:
		HttpTextServer(ChatData& chatData, const WString& baseUrl, vint port)
			: TextServerCallbackHost(chatData)
			, HttpServer(baseUrl, port)
		{
		}

		WaitForClientResult OnClientConnected(INetworkProtocolConnection* connection) override
		{
			return AcceptTextConnection(connection);
		}
	};

	class NamedPipeChannelServer : public ChannelServer, public NamedPipeServer
	{
	public:
		NamedPipeChannelServer(ChannelChatData& chatData, const WString& pipeName)
			: ChannelServer(chatData)
			, NamedPipeServer(pipeName)
		{
		}

		WaitForClientResult OnClientConnected(INetworkProtocolConnection* connection) override
		{
			return ChannelServer::OnClientConnected(connection);
		}

		void Start() override
		{
			ChannelServer::Start();
			NamedPipeServer::Start();
		}

		void Stop() override
		{
			ChannelServer::Stop();
			NamedPipeServer::Stop();
		}

		bool IsStopped() override
		{
			return ChannelServer::IsStopped() || NamedPipeServer::IsStopped();
		}
	};

	class HttpChannelServer : public ChannelServer, public HttpServer
	{
	public:
		HttpChannelServer(ChannelChatData& chatData, const WString& baseUrl, vint port)
			: ChannelServer(chatData)
			, HttpServer(baseUrl, port)
		{
		}

		WaitForClientResult OnClientConnected(INetworkProtocolConnection* connection) override
		{
			return ChannelServer::OnClientConnected(connection);
		}

		void Start() override
		{
			ChannelServer::Start();
			HttpServer::Start();
		}

		void Stop() override
		{
			HttpServer::Stop();
			ChannelServer::Stop();
		}

		bool IsStopped() override
		{
			return ChannelServer::IsStopped() || HttpServer::IsStopped();
		}
	};
}

#endif

TEST_FILE
{
	TEST_CASE(L"NetworkPackage ExtraClientIds")
	{
		NetworkPackage::ClientIdList extraClientIds;
		extraClientIds.Add(1);
		extraClientIds.Add(2);

		auto broadcastPackageString = NetworkPackage::ToString(NetworkPackage::Create({}, extraClientIds, ChatChannelName, L"Message"));
		TEST_ASSERT(broadcastPackageString == L",1,2;Chat;Message");

		NetworkPackage broadcastPackage;
		NetworkPackage::Parse(broadcastPackageString, broadcastPackage);
		TEST_ASSERT(!broadcastPackage.clientId);
		TEST_ASSERT(broadcastPackage.extraClientIds);
		TEST_ASSERT(broadcastPackage.extraClientIds.Value().Count() == 2);
		TEST_ASSERT(broadcastPackage.extraClientIds.Value()[0] == 1);
		TEST_ASSERT(broadcastPackage.extraClientIds.Value()[1] == 2);
		TEST_ASSERT(broadcastPackage.channelName == ChatChannelName);
		TEST_ASSERT(broadcastPackage.messageBody == L"Message");

		auto directPackageString = NetworkPackage::ToString(NetworkPackage::Create(3, extraClientIds, ChatChannelName, L"Message"));
		TEST_ASSERT(directPackageString == L"3,1,2;Chat;Message");

		NetworkPackage directPackage;
		NetworkPackage::Parse(directPackageString, directPackage);
		TEST_ASSERT(directPackage.clientId);
		TEST_ASSERT(directPackage.clientId.Value() == 3);
		TEST_ASSERT(directPackage.extraClientIds);
		TEST_ASSERT(directPackage.extraClientIds.Value().Count() == 2);
		TEST_ASSERT(directPackage.extraClientIds.Value()[0] == 1);
		TEST_ASSERT(directPackage.extraClientIds.Value()[1] == 2);
		TEST_ASSERT(directPackage.channelName == ChatChannelName);
		TEST_ASSERT(directPackage.messageBody == L"Message");

		NetworkPackage emptyPackage;
		NetworkPackage::Parse(NetworkPackage::ToString(NetworkPackage::Create({}, ChatChannelName, L"Message")), emptyPackage);
		TEST_ASSERT(!emptyPackage.clientId);
		TEST_ASSERT(!emptyPackage.extraClientIds);
		TEST_ASSERT(emptyPackage.channelName == ChatChannelName);
		TEST_ASSERT(emptyPackage.messageBody == L"Message");
	});

#ifdef VCZH_MSVC
	TEST_CASE(L"NamedPipe (NetworkProtocol)")
	{
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			RunTextNetworkProtocol(
				[](ChatData& chatData)->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new NamedPipeTextServer(chatData, L"VlppOSTestPipe")); },
				[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new NamedPipeClient(L"VlppOSTestPipe")); }
			);
		}
	});

	TEST_CASE(L"HttpServer (NetworkProtocol)")
	{
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			RunTextNetworkProtocol(
				[](ChatData& chatData)->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new HttpTextServer(chatData, L"/VlppOSTestHttpServer", 8765)); },
				[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new HttpClient(L"/VlppOSTestHttpServer", 8765)); }
			);
		}
	});

	TEST_CASE(L"NamedPipe (Channel)")
	{
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			RunNetworkProtocolChannel(
				[](ChannelChatData& chatData)->Ptr<IChannelServer<WString>> { return Ptr<IChannelServer<WString>>(new NamedPipeChannelServer(chatData, L"VlppOSTestPipeChannel")); },
				[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new NamedPipeClient(L"VlppOSTestPipeChannel")); }
			);
		}
	});

	TEST_CASE(L"HttpServer (Channel)")
	{
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			RunNetworkProtocolChannel(
				[](ChannelChatData& chatData)->Ptr<IChannelServer<WString>> { return Ptr<IChannelServer<WString>>(new HttpChannelServer(chatData, L"/VlppOSTestHttpServerChannel", 8766)); },
				[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new HttpClient(L"/VlppOSTestHttpServerChannel", 8766)); }
			);
		}
	});
#endif
}
