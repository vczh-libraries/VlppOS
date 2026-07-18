#include "../../Source/InterProcess/NetworkProtocolChannel.h"
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket_HttpClient.h"
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket_HttpServer.h"
#if defined VCZH_MSVC
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.Windows.h"
#include "../../Source/InterProcess/Windows/NamedPipe.Windows.h"
#include "../../Source/InterProcess/Windows/HttpClient.Windows.h"
#include "../../Source/InterProcess/Windows/HttpServer.Windows.h"
#elif defined VCZH_GCC && defined VCZH_APPLE
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.macOS.h"
#elif defined VCZH_GCC && !defined VCZH_APPLE
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.Linux.h"
#endif
#include <utility>

using namespace vl;
using namespace vl::collections;
using namespace vl::inter_process;
#ifdef VCZH_MSVC
using namespace vl::inter_process::named_pipe;
using namespace vl::inter_process::windows_http;
#elif defined VCZH_GCC && defined VCZH_APPLE
#elif defined VCZH_GCC && !defined VCZH_APPLE
#endif

namespace vl::inter_process::async_tcp_socket
{
	extern void SetSocketHttpServerListenerFactoryForTesting(const Func<Ptr<IAsyncSocketServer>(vint)>& factory);
	extern void ResetSocketHttpServerListenerFactoryForTesting();
	extern void SetSocketHttpServerPollCallbacksForTesting(
		const Func<void(const WString&)>& claimed,
		const Func<void(const WString&, bool)>& completed,
		const Func<void(const WString&)>& registered
		);
	extern void ResetSocketHttpServerPollCallbacksForTesting();
	extern void SetSocketHttpClientReceiveSubmittedCallbackForTesting(const Func<void()>& callback);
	extern void ResetSocketHttpClientReceiveSubmittedCallbackForTesting();
	extern void SetSocketHttpClientFatalStopCallbacksForTesting(
		const Func<void()>& fatalReserved,
		const Func<void()>& stopStarted
		);
	extern void ResetSocketHttpClientFatalStopCallbacksForTesting();
}

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
		EventObject						eventServer, eventTom, eventJerry, eventClientsStopped;
		atomic_vint					clientStopCounter = 0;

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
			eventClientsStopped.CreateManualUnsignal(false);
		}

		void NotifyClientStopped()
		{
			auto stoppedClients = ++clientStopCounter;
			CHECK_ERROR(stoppedClients <= 2, L"Each protocol client should stop only once.");
			if (stoppedClients == 2)
			{
				CHECK_ERROR(eventClientsStopped.Signal(), L"Failed to signal that both protocol clients stopped.");
			}
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
		Func<Ptr<INetworkProtocolClient>()> createClient,
		bool synchronizeServerStartup = false
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
		EventObject eventServerStarted;
		if (synchronizeServerStartup)
		{
			CHECK_ERROR(eventServerStarted.CreateManualUnsignal(false), L"Failed to create the server-started event.");
		}

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				auto server = createServer(chatData);
				server->Start();
				if (synchronizeServerStartup)
				{
					CHECK_ERROR(eventServerStarted.Signal(), L"Failed to signal the server-started event.");
				}
				CHECK_ERROR(!server->IsStopped(), L"Server should not be stopped before accepting clients.");
				chatData.eventServer.Wait();
				CHECK_ERROR(!server->IsStopped(), L"Server should not be stopped before Stop.");
				CHECK_ERROR(chatData.eventClientsStopped.Wait(), L"Failed to wait until both protocol clients stopped.");
				server->Stop();
				CHECK_ERROR(server->IsStopped(), L"Server should be stopped after Stop.");
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				if (synchronizeServerStartup)
				{
					CHECK_ERROR(eventServerStarted.Wait(), L"Failed to wait for the server-started event.");
				}
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
				client->GetConnection()->InstallCallback(nullptr);
				chatData.NotifyClientStopped();
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				if (synchronizeServerStartup)
				{
					CHECK_ERROR(eventServerStarted.Wait(), L"Failed to wait for the server-started event.");
				}
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
				client->GetConnection()->InstallCallback(nullptr);
				chatData.NotifyClientStopped();
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
		EventObject						eventClientsConnected, eventClientIdsReceived, eventServer, eventTom, eventJerry;

		// covers clientId1, clientId2, serverClientId, client1ReceivedIds, client2ReceivedIds, client1Stopped, client2Stopped, clientId1ReceivedHello and clientId2ReceivedHello
		SpinLock						lockServer;
		vint							clientId1 = -1;
		vint							clientId2 = -1;
		vint							serverClientId = -1;
		bool							client1ReceivedIds = false;
		bool							client2ReceivedIds = false;
		bool							client1Stopped = false;
		bool							client2Stopped = false;
		bool							clientId1ReceivedHello = false;
		bool							clientId2ReceivedHello = false;
		vint							networkClientConnections = 0;
		bool							localClientConnected = false;

		ChannelChatData()
		{
			eventClientsConnected.CreateManualUnsignal(false);
			eventClientIdsReceived.CreateManualUnsignal(false);
			eventServer.CreateManualUnsignal(false);
			eventTom.CreateManualUnsignal(false);
			eventJerry.CreateManualUnsignal(false);
		}
	};

	template<typename TServerBase>
	class ChannelServer
		: public NetworkProtocolChannelServer<WString, WStringListSerializer, TServerBase>
	{
		using Base = NetworkProtocolChannelServer<WString, WStringListSerializer, TServerBase>;

	private:
		ChannelChatData*				chatData = nullptr;

	public:
		using Base::OnClientConnected;

		template<typename... TArgs>
		ChannelServer(ChannelChatData& _chatData, TArgs&&... args)
			: Base(std::forward<TArgs>(args)...)
			, chatData(&_chatData)
		{
		}

		WaitForClientResult OnClientConnected(vint clientId, const IChannelClient<WString>::ChannelNameList& availableChannels, IChannelClient<WString>* localClient) override
		{
			CHECK_ERROR(availableChannels.Contains(ChatChannelName), L"Channel client should provide the chat channel.");
			SPIN_LOCK(chatData->lockServer)
			{
				if (localClient)
				{
					chatData->localClientConnected = true;
				}
				else
				{
					chatData->networkClientConnections++;
				}
			}
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

			bool clientsReceivedIds = false;
			SPIN_LOCK(chatData->lockServer)
			{
				if (clientId == chatData->clientId1)
				{
					CHECK_ERROR(!chatData->client1ReceivedIds, L"Tom should receive client ids only once.");
					chatData->client1ReceivedIds = true;
				}
				else if (clientId == chatData->clientId2)
				{
					CHECK_ERROR(!chatData->client2ReceivedIds, L"Jerry should receive client ids only once.");
					chatData->client2ReceivedIds = true;
				}
				else
				{
					CHECK_FAIL(L"Channel client should record ids for a known client id.");
				}
				clientsReceivedIds = chatData->client1ReceivedIds && chatData->client2ReceivedIds;
			}
			if (clientsReceivedIds)
			{
				CHECK_ERROR(chatData->eventClientIdsReceived.Signal(), L"Failed to signal that both channel clients received their ids.");
			}
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

		void SendHello()
		{
			CHECK_ERROR(anotherClientId > 0, L"Tom should receive client ids before sending Hello.");
			SendTo(anotherClientId, L"Hello");
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
		Func<Ptr<INetworkProtocolClient>()> createClient,
		bool synchronizeServerStartup = false
		)
	{
		auto timeoutThread = Ptr(new TimeoutThread);
		ChannelChatData chatData;
		EventObject eventServerStarted;
		if (synchronizeServerStartup)
		{
			CHECK_ERROR(eventServerStarted.CreateManualUnsignal(false), L"Failed to create the server-started event.");
		}

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				auto server = createServer(chatData);
				server->Start();
				if (synchronizeServerStartup)
				{
					CHECK_ERROR(eventServerStarted.Signal(), L"Failed to signal the server-started event.");
				}
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
				if (synchronizeServerStartup)
				{
					CHECK_ERROR(eventServerStarted.Wait(), L"Failed to wait for the server-started event.");
				}
				auto client = Ptr(new TomChannelClient(createClient(), chatData));
				client->WaitForServer();
				CHECK_ERROR(chatData.eventClientIdsReceived.Wait(), L"Failed to wait until both channel clients received their ids.");
				client->SendHello();
				chatData.eventTom.Wait();
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				if (synchronizeServerStartup)
				{
					CHECK_ERROR(eventServerStarted.Wait(), L"Failed to wait for the server-started event.");
				}
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
			TEST_ASSERT(chatData.networkClientConnections == 2);
			TEST_ASSERT(chatData.localClientConnected);
		}
	}
}
using namespace mynamespace;

namespace mynamespace
{
	constexpr vint SocketHttpFocusedTimeout = 30000;

	template<typename TNativeServer>
	class SocketHttpListenerFactoryScope
	{
	public:
		SocketHttpListenerFactoryScope()
		{
			async_tcp_socket::SetSocketHttpServerListenerFactoryForTesting(Func<Ptr<async_tcp_socket::IAsyncSocketServer>(vint)>([](vint port)
			{
				return Ptr<async_tcp_socket::IAsyncSocketServer>(new TNativeServer(port));
			}));
		}

		~SocketHttpListenerFactoryScope()
		{
			try
			{
				async_tcp_socket::ResetSocketHttpServerListenerFactoryForTesting();
			}
			catch (...)
			{
			}
		}
	};

	class SocketHttpPollHookScope
	{
	public:
		SocketHttpPollHookScope(
			const Func<void(const WString&)>& claimed,
			const Func<void(const WString&, bool)>& completed,
			const Func<void(const WString&)>& registered = {}
			)
		{
			async_tcp_socket::SetSocketHttpServerPollCallbacksForTesting(claimed, completed, registered);
		}

		~SocketHttpPollHookScope()
		{
			try
			{
				async_tcp_socket::ResetSocketHttpServerPollCallbacksForTesting();
			}
			catch (...)
			{
			}
		}
	};

	class SocketHttpReceiveSubmissionHookScope
	{
	public:
		SocketHttpReceiveSubmissionHookScope(const Func<void()>& callback)
		{
			async_tcp_socket::SetSocketHttpClientReceiveSubmittedCallbackForTesting(callback);
		}

		~SocketHttpReceiveSubmissionHookScope()
		{
			try
			{
				async_tcp_socket::ResetSocketHttpClientReceiveSubmittedCallbackForTesting();
			}
			catch (...)
			{
			}
		}
	};

	class SocketHttpFatalStopHookScope
	{
	public:
		SocketHttpFatalStopHookScope(
			const Func<void()>& fatalReserved,
			const Func<void()>& stopStarted
			)
		{
			async_tcp_socket::SetSocketHttpClientFatalStopCallbacksForTesting(fatalReserved, stopStarted);
		}

		~SocketHttpFatalStopHookScope()
		{
			try
			{
				async_tcp_socket::ResetSocketHttpClientFatalStopCallbacksForTesting();
			}
			catch (...)
			{
			}
		}
	};

	class SignalSocketHttpEventOnExit
	{
	private:
		EventObject*						eventObject = nullptr;

	public:
		SignalSocketHttpEventOnExit(EventObject& value)
			: eventObject(&value)
		{
		}

		~SignalSocketHttpEventOnExit()
		{
			eventObject->Signal();
		}
	};

	class SocketHttpTextServer
		: protected TextServerCallbackHost
		, public async_tcp_socket::SocketHttpServer
	{
	public:
		SocketHttpTextServer(ChatData& chatData, const WString& baseUrl, vint port)
			: TextServerCallbackHost(chatData)
			, SocketHttpServer(baseUrl, port)
		{
		}

		~SocketHttpTextServer()
		{
			Stop();
		}

		WaitForClientResult OnClientConnected(INetworkProtocolConnection* connection) override
		{
			return AcceptTextConnection(connection);
		}
	};

	class SocketHttpChannelServer : public ChannelServer<async_tcp_socket::SocketHttpServer>
	{
		using Base = ChannelServer<async_tcp_socket::SocketHttpServer>;

	public:
		SocketHttpChannelServer(ChannelChatData& chatData, const WString& baseUrl, vint port)
			: Base(chatData, baseUrl, port)
		{
		}

		~SocketHttpChannelServer()
		{
			this->Stop();
		}
	};

	class FocusedProtocolCallback : public Object, public virtual INetworkProtocolCallback
	{
	protected:
		INetworkProtocolConnection*		connection = nullptr;

	public:
		EventObject						eventInstalled;
		EventObject						eventRead;
		EventObject						eventDisconnected;
		atomic_vint					readCount = 0;
		atomic_vint					disconnectedCount = 0;

		FocusedProtocolCallback()
		{
			CHECK_ERROR(eventInstalled.CreateManualUnsignal(false), L"Failed to create the SocketHttp installed event.");
			CHECK_ERROR(eventRead.CreateManualUnsignal(false), L"Failed to create the SocketHttp read event.");
			CHECK_ERROR(eventDisconnected.CreateManualUnsignal(false), L"Failed to create the SocketHttp disconnected event.");
		}

		void OnInstalled(INetworkProtocolConnection* value) override
		{
			connection = value;
			eventInstalled.Signal();
		}

		INetworkProtocolConnection* Connection()
		{
			return connection;
		}

		void OnDisconnected() override
		{
			disconnectedCount++;
			eventDisconnected.Signal();
		}
	};

	class ExactMessageCallback : public FocusedProtocolCallback
	{
	private:
		WString							expected;
		WString							reply;

	public:
		bool							exact = false;

		ExactMessageCallback(const WString& _expected, const WString& _reply = WString::Empty)
			: expected(_expected)
			, reply(_reply)
		{
		}

		void OnReadString(const WString& value) override
		{
			exact = value == expected;
			readCount++;
			if (reply != WString::Empty)
			{
				connection->SendString(reply);
			}
			eventRead.Signal();
		}
	};

	class StopActionCallback : public FocusedProtocolCallback
	{
	private:
		WString							expected;
		Func<void()>					stopAction;

	public:
		bool							exact = false;
		bool							stopReturned = false;

		StopActionCallback(const WString& _expected)
			: expected(_expected)
		{
		}

		void SetStopAction(const Func<void()>& value)
		{
			stopAction = value;
		}

		void OnReadString(const WString& value) override
		{
			exact = value == expected;
			readCount++;
			stopAction();
			stopReturned = true;
			eventRead.Signal();
		}
	};

	class ThrowingDisconnectCallback : public FocusedProtocolCallback
	{
	public:
		EventObject						eventSecondStopReturned;
		atomic_vint					disconnectThrows = 0;
		atomic_vint					secondStopReturns = 0;
		atomic_vint					secondStopErrors = 0;

		ThrowingDisconnectCallback()
		{
			CHECK_ERROR(eventSecondStopReturned.CreateManualUnsignal(false), L"Failed to create the throwing-disconnect second-Stop event.");
		}

		void OnReadString(const WString&) override
		{
			readCount++;
		}

		void OnDisconnected() override
		{
			FocusedProtocolCallback::OnDisconnected();
			disconnectThrows++;
			throw Error(L"Expected throwing SocketHttp OnDisconnected callback.");
		}
	};

	class SingleConnectionSocketHttpServer : public async_tcp_socket::SocketHttpServer
	{
	private:
		INetworkProtocolCallback*		callback = nullptr;

	public:
		SingleConnectionSocketHttpServer(const WString& baseUrl, vint port, INetworkProtocolCallback* _callback)
			: SocketHttpServer(baseUrl, port)
			, callback(_callback)
		{
		}

		~SingleConnectionSocketHttpServer()
		{
			Stop();
		}

		WaitForClientResult OnClientConnected(INetworkProtocolConnection* connection) override
		{
			connection->InstallCallback(callback);
			connection->BeginReadingLoopUnsafe();
			return WaitForClientResult::Accept;
		}
	};

	class SocketHttpStopRaceState : public Object
	{
	public:
		EventObject						eventPollRegistered;
		EventObject						eventPollClaimed;
		EventObject						eventReleasePoll;
		EventObject						eventWholeStopEntered;
		EventObject						eventLocalStopReturned;
		EventObject						eventWholeStopReturned;
		atomic_vint					pollRegisteredCount = 0;
		atomic_vint					pollClaimedCount = 0;
		atomic_vint					pollCompletedCount = 0;
		atomic_vint					pollReleasedCount = 0;
		atomic_vint					localStopReturns = 0;
		atomic_vint					wholeStopReturns = 0;
		atomic_vint					sendReturns = 0;
		atomic_vint					stopErrors = 0;

		SocketHttpStopRaceState()
		{
			CHECK_ERROR(eventPollRegistered.CreateManualUnsignal(false), L"Failed to create the Stop-race poll-registered event.");
			CHECK_ERROR(eventPollClaimed.CreateManualUnsignal(false), L"Failed to create the Stop-race poll-claimed event.");
			CHECK_ERROR(eventReleasePoll.CreateManualUnsignal(false), L"Failed to create the Stop-race release-poll event.");
			CHECK_ERROR(eventWholeStopEntered.CreateManualUnsignal(false), L"Failed to create the Stop-race whole-Stop-entered event.");
			CHECK_ERROR(eventLocalStopReturned.CreateManualUnsignal(false), L"Failed to create the Stop-race local-Stop-returned event.");
			CHECK_ERROR(eventWholeStopReturned.CreateManualUnsignal(false), L"Failed to create the Stop-race whole-Stop-returned event.");
		}

		void PollRegistered()
		{
			pollRegisteredCount++;
			eventPollRegistered.Signal();
		}

		void PollClaimed()
		{
			pollClaimedCount++;
			eventPollClaimed.Signal();
			eventReleasePoll.Wait();
			pollReleasedCount++;
		}

		void PollCompleted()
		{
			pollCompletedCount++;
		}
	};

	class SocketHttpStopRaceServer : public SingleConnectionSocketHttpServer
	{
	private:
		Ptr<SocketHttpStopRaceState>		state;

	protected:
		void OnHttpServerStopping() override
		{
			state->eventWholeStopEntered.Signal();
			async_tcp_socket::SocketHttpServer::OnHttpServerStopping();
		}

	public:
		SocketHttpStopRaceServer(const WString& baseUrl, vint port, INetworkProtocolCallback* callback, Ptr<SocketHttpStopRaceState> _state)
			: SingleConnectionSocketHttpServer(baseUrl, port, callback)
			, state(_state)
		{
		}

		~SocketHttpStopRaceServer()
		{
			Stop();
		}
	};

	class SocketHttpStopThreadScope
	{
	private:
		EventObject*						eventRelease = nullptr;
		Thread*							first = nullptr;
		Thread*							second = nullptr;
		Thread*							third = nullptr;

		static void Join(Thread*& thread)
		{
			if (thread)
			{
				thread->Wait();
				delete thread;
				thread = nullptr;
			}
		}

	public:
		SocketHttpStopThreadScope(EventObject* _eventRelease = nullptr)
			: eventRelease(_eventRelease)
		{
		}

		~SocketHttpStopThreadScope()
		{
			if (eventRelease) eventRelease->Signal();
			Join(first);
			Join(second);
			Join(third);
		}

		void SetFirst(Thread* thread)
		{
			first = thread;
		}

		void SetSecond(Thread* thread)
		{
			second = thread;
		}

		void SetThird(Thread* thread)
		{
			third = thread;
		}

		void JoinAll()
		{
			Join(first);
			Join(second);
			Join(third);
		}
	};

	class SocketHttpReentrantFollowerState : public Object
	{
	public:
		async_tcp_socket::SocketHttpServer*	server = nullptr;
		EventObject						eventNestedStopReturned;
		EventObject						eventOuterStopReturned;
		atomic_vint					disconnectEntered = 0;
		atomic_vint					disconnectCompleted = 0;
		atomic_vint					nestedStopCalls = 0;
		atomic_vint					nestedStopReturns = 0;
		atomic_vint					outerStopReturns = 0;
		atomic_vint					nestedSawOtherCompleted = 0;
		atomic_vint					stopErrors = 0;

		SocketHttpReentrantFollowerState()
		{
			CHECK_ERROR(eventNestedStopReturned.CreateManualUnsignal(false), L"Failed to create the reentrant-follower nested-Stop event.");
			CHECK_ERROR(eventOuterStopReturned.CreateManualUnsignal(false), L"Failed to create the reentrant-follower outer-Stop event.");
		}
	};

	class SocketHttpReentrantFollowerCallback : public FocusedProtocolCallback
	{
	private:
		Ptr<SocketHttpReentrantFollowerState>	state;

	public:
		SocketHttpReentrantFollowerCallback(Ptr<SocketHttpReentrantFollowerState> _state)
			: state(_state)
		{
		}

		void OnReadString(const WString&) override
		{
			readCount++;
		}

		void OnDisconnected() override
		{
			auto ordinal = ++state->disconnectEntered;
			if (ordinal == 1)
			{
				state->nestedStopCalls++;
				try
				{
					state->server->Stop();
					state->nestedStopReturns++;
					if (state->disconnectCompleted == 1)
					{
						state->nestedSawOtherCompleted++;
					}
				}
				catch (...)
				{
					state->stopErrors++;
				}
				state->eventNestedStopReturned.Signal();
			}

			FocusedProtocolCallback::OnDisconnected();
			state->disconnectCompleted++;
		}
	};

	class TwoConnectionSocketHttpServer : public async_tcp_socket::SocketHttpServer
	{
	private:
		INetworkProtocolCallback*			callbacks[2] = {};
		atomic_vint						connectionCount = 0;

	public:
		TwoConnectionSocketHttpServer(
			const WString& baseUrl,
			vint port,
			INetworkProtocolCallback* firstCallback,
			INetworkProtocolCallback* secondCallback
			)
			: SocketHttpServer(baseUrl, port)
		{
			callbacks[0] = firstCallback;
			callbacks[1] = secondCallback;
		}

		~TwoConnectionSocketHttpServer()
		{
			Stop();
		}

		WaitForClientResult OnClientConnected(INetworkProtocolConnection* connection) override
		{
			auto index = connectionCount++;
			if (index >= 2) return WaitForClientResult::Reject;
			connection->InstallCallback(callbacks[index]);
			connection->BeginReadingLoopUnsafe();
			return WaitForClientResult::Accept;
		}
	};

	async_tcp_socket::HttpField CreateSocketHttpFieldWithRawName(const WString& name, const WString& value)
	{
		auto field = async_tcp_socket::CreateAsciiHttpField(name, value);
		field.name = name;
		return field;
	}

	void AddSocketHttpBody(async_tcp_socket::HttpBody& body, const WString& value)
	{
		async_tcp_socket::HttpBodyChunk chunk;
		CHECK_ERROR(async_tcp_socket::EncodeStrictUtf8(value, chunk.data), L"A SocketHttp body chunk must contain valid Unicode.");
		body.chunks.Add(std::move(chunk));
	}

	void AddSocketHttpBody(async_tcp_socket::HttpBody& body, const vuint8_t* buffer, vint size)
	{
		CHECK_ERROR(size > 0, L"A raw SocketHttp body chunk must not be empty.");
		async_tcp_socket::HttpBodyChunk chunk;
		chunk.data.Resize(size);
		for (vint i = 0; i < size; i++)
		{
			chunk.data[i] = buffer[i];
		}
		body.chunks.Add(std::move(chunk));
	}

	Ptr<async_tcp_socket::HttpResponse> CreateSocketHttpResponse(vint statusCode, const WString& body)
	{
		auto response = Ptr(new async_tcp_socket::HttpResponse);
		response->statusCode = statusCode;
		response->reason = statusCode == 200 ? L"OK" : L"Scripted failure";
		response->headers.Add(CreateSocketHttpFieldWithRawName(L"Content-Type", HttpNetworkProtocolContentType));
		if (body != WString::Empty)
		{
			AddSocketHttpBody(response->body, body);
		}
		return response;
	}

	WString ReadSocketHttpBody(const async_tcp_socket::HttpBody& body)
	{
		Array<vuint8_t> bytes;
		CHECK_ERROR(async_tcp_socket::FlattenHttpBody(body, bytes), L"A SocketHttp body is too large to flatten.");
		WString text;
		CHECK_ERROR(
			async_tcp_socket::DecodeStrictUtf8(bytes.Count() == 0 ? nullptr : &bytes[0], bytes.Count(), text),
			L"A SocketHttp body must contain valid UTF-8."
			);
		return text;
	}

	class SocketHttpRawQueryCallback : public Object, public virtual async_tcp_socket::IHttpRequestCallback
	{
	private:
		SpinLock						lock;
		Ptr<async_tcp_socket::HttpResponse>
									response;
		WString						failure;
		bool						completed = false;

		void Complete(Ptr<async_tcp_socket::HttpResponse> value, const WString& error)
		{
			bool signal = false;
			SPIN_LOCK(lock)
			{
				if (!completed)
				{
					completed = true;
					response = value;
					failure = error;
					signal = true;
				}
			}
			if (signal) eventCompleted.Signal();
		}

	public:
		EventObject						eventCompleted;

		SocketHttpRawQueryCallback()
		{
			CHECK_ERROR(eventCompleted.CreateManualUnsignal(false), L"Failed to create the raw SocketHttp query event.");
		}

		void OnInstalled(async_tcp_socket::IHttpRequestConnection*) override
		{
		}

		void OnReadResponse(Ptr<async_tcp_socket::HttpResponse> value) override
		{
			Complete(value, WString::Empty);
		}

		void OnReadRequest(Ptr<async_tcp_socket::HttpRequest>) override
		{
			Complete(nullptr, L"The raw SocketHttp query received a request instead of a response.");
		}

		void OnReadRequestFailure(async_tcp_socket::HttpRequestFailure) override
		{
			Complete(nullptr, L"The raw SocketHttp query failed while parsing a response.");
		}

		void OnError(const WString& error, bool) override
		{
			Complete(nullptr, error);
		}

		void OnDisconnected() override
		{
			Complete(nullptr, L"The raw SocketHttp query disconnected before receiving a response.");
		}

		Ptr<async_tcp_socket::HttpResponse> GetResponse()
		{
			SPIN_LOCK(lock)
			{
				return response;
			}
			return nullptr;
		}

		WString GetFailure()
		{
			SPIN_LOCK(lock)
			{
				return failure;
			}
			return {};
		}
	};

	template<typename TNativeClient>
	Ptr<async_tcp_socket::HttpResponse> SubmitSocketHttpRawQuery(vint port, Ptr<async_tcp_socket::HttpRequest> request)
	{
		SocketHttpRawQueryCallback callback;
		auto nativeClient = Ptr<async_tcp_socket::IAsyncSocketClient>(new TNativeClient(port));
		auto client = Ptr(new async_tcp_socket::HttpRequestClient(nativeClient));
		client->GetConnection()->InstallCallback(&callback);
		client->WaitForServer();
		client->GetConnection()->BeginReadingLoopUnsafe();
		client->GetConnection()->SendRequest(request);
		CHECK_ERROR(callback.eventCompleted.WaitForTime(SocketHttpFocusedTimeout), L"The raw SocketHttp query timed out.");
		auto response = callback.GetResponse();
		auto failure = callback.GetFailure();
		client->GetConnection()->Stop();
		client->GetConnection()->InstallCallback(nullptr);
		auto failureMessage = L"The raw SocketHttp query failed: " + failure;
		CHECK_ERROR(failure == WString::Empty, failureMessage.Buffer());
		CHECK_ERROR(response, L"The raw SocketHttp query completed without a response.");
		return response;
	}

	Ptr<async_tcp_socket::HttpRequest> CreateSocketHttpRawRequest(vint port, const WString& method, const WString& target)
	{
		auto request = Ptr(new async_tcp_socket::HttpRequest);
		request->method = method;
		request->requestTarget = target;
		request->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"host", L"localhost:" + itow(port)));
		return request;
	}

	class SocketHttpRecordingCallback : public FocusedProtocolCallback
	{
	private:
		SpinLock						lockMessages;
		List<WString>					messages;

	public:
		void OnReadString(const WString& value) override
		{
			SPIN_LOCK(lockMessages)
			{
				messages.Add(value);
			}
			readCount++;
			eventRead.Signal();
		}

		vint MessageCount()
		{
			SPIN_LOCK(lockMessages)
			{
				return messages.Count();
			}
			return 0;
		}

		WString Message(vint index)
		{
			SPIN_LOCK(lockMessages)
			{
				return messages[index];
			}
			return {};
		}
	};

	Ptr<async_tcp_socket::HttpResponse> CreateSocketHttpScriptResponse(vint statusCode, const WString& body)
	{
		auto response = Ptr(new async_tcp_socket::HttpResponse);
		response->statusCode = statusCode;
		response->reason = statusCode == 200 ? L"OK" : L"Scripted failure";
		response->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
		if (body != WString::Empty)
		{
			AddSocketHttpBody(response->body, body);
		}
		return response;
	}

	class SocketHttpConnectResponseScriptServer : public async_tcp_socket::HttpRequestServer
	{
	private:
		class Callback : public Object, public virtual async_tcp_socket::IHttpRequestCallback
		{
		private:
			SocketHttpConnectResponseScriptServer*
									owner = nullptr;
			async_tcp_socket::IHttpRequestConnection*
									connection = nullptr;

		public:
			Callback(SocketHttpConnectResponseScriptServer* _owner)
				: owner(_owner)
			{
			}

			void OnInstalled(async_tcp_socket::IHttpRequestConnection* value) override
			{
				connection = value;
			}

			void OnReadRequest(Ptr<async_tcp_socket::HttpRequest> request) override
			{
				connection->SendResponse(owner->NextResponse(request));
			}
		};

		CriticalSection					lockState;
		List<Ptr<async_tcp_socket::HttpResponse>>
									responses;
		List<Ptr<Callback>>				callbacks;
		WString						expectedTarget;
		vint						responseIndex = 0;

		Ptr<async_tcp_socket::HttpResponse> NextResponse(Ptr<async_tcp_socket::HttpRequest> request)
		{
			CHECK_ERROR(request && request->method == L"GET" && request->requestTarget == expectedTarget, L"The scripted SocketHttp server received an unexpected request.");
			CS_LOCK(lockState)
			{
				CHECK_ERROR(responseIndex < responses.Count(), L"The scripted SocketHttp server received too many Connect attempts.");
				return responses[responseIndex++];
			}
			return nullptr;
		}

	public:
		SocketHttpConnectResponseScriptServer(
			Ptr<async_tcp_socket::IAsyncSocketServer> server,
			const WString& target,
			const List<Ptr<async_tcp_socket::HttpResponse>>& scriptedResponses
			)
			: HttpRequestServer(server)
			, expectedTarget(target)
		{
			for (auto response : scriptedResponses)
			{
				responses.Add(response);
			}
		}

		~SocketHttpConnectResponseScriptServer()
		{
			HttpRequestServer::Stop();
			CS_LOCK(lockState)
			{
				callbacks.Clear();
			}
		}

		WaitForClientResult OnClientConnected(async_tcp_socket::IHttpRequestConnection* connection) override
		{
			auto callback = Ptr(new Callback(this));
			CS_LOCK(lockState)
			{
				callbacks.Add(callback);
			}
			connection->InstallCallback(callback.Obj());
			connection->BeginReadingLoopUnsafe();
			return WaitForClientResult::Accept;
		}
	};

	class SocketHttpQueryState : public Object
	{
	private:
		SpinLock						lock;
		bool							failed = false;
		vint							statusCode = 0;
		WString							body;
		Array<char>						bodyBytes;

	public:
		EventObject						eventCompleted;
		atomic_vint					callbackCount = 0;

		SocketHttpQueryState()
		{
			CHECK_ERROR(eventCompleted.CreateManualUnsignal(false), L"Failed to create the scripted SocketHttp query event.");
		}

		void Complete(Variant<windows_http::HttpResponse, windows_http::HttpError> result)
		{
			SPIN_LOCK(lock)
			{
				if (auto response = result.TryGet<windows_http::HttpResponse>())
				{
					failed = false;
					statusCode = response->statusCode;
					body = response->GetBodyUtf8();
					bodyBytes = std::move(response->body);
				}
				else
				{
					failed = true;
				}
			}
			callbackCount++;
			eventCompleted.Signal();
		}

		bool Failed()
		{
			SPIN_LOCK(lock)
			{
				return failed;
			}
			return false;
		}

		vint StatusCode()
		{
			SPIN_LOCK(lock)
			{
				return statusCode;
			}
			return 0;
		}

		WString Body()
		{
			SPIN_LOCK(lock)
			{
				return body;
			}
			return {};
		}

		bool BodyBytesEqual(const Array<vuint8_t>& expected)
		{
			SPIN_LOCK(lock)
			{
				if (bodyBytes.Count() != expected.Count()) return false;
				for (vint i = 0; i < bodyBytes.Count(); i++)
				{
					if ((vuint8_t)(unsigned char)bodyBytes[i] != expected[i]) return false;
				}
				return true;
			}
			return false;
		}
	};

	void SubmitSocketHttpQuery(
		Ptr<async_tcp_socket::SocketHttpClientApi> client,
		const WString& method,
		const WString& query,
		const WString& body,
		vint receiveTimeout,
		Ptr<SocketHttpQueryState> state
		)
	{
		windows_http::HttpRequest request;
		request.method = method;
		request.query = query;
		request.acceptTypes.Add(HttpNetworkProtocolContentType);
		request.receiveTimeout = receiveTimeout;
		if (body != WString::Empty)
		{
			request.contentType = HttpNetworkProtocolContentType;
			request.SetBodyUtf8(body);
		}
		else if (method == L"POST")
		{
			request.extraHeaders.Add(L"Content-Length", L"0");
		}
		client->HttpQuery(request, Func<void(Variant<windows_http::HttpResponse, windows_http::HttpError>)>([state](auto result)
		{
			state->Complete(std::move(result));
		}));
	}

	template<typename TNativeClient>
	Ptr<async_tcp_socket::SocketHttpClientApi> CreateFocusedSocketHttpApi(vint port)
	{
		return Ptr(new async_tcp_socket::SocketHttpClientApi(
			Ptr<async_tcp_socket::IAsyncSocketClient>(new TNativeClient(port)),
			L"localhost:" + itow(port)
			));
	}

	class PollScriptServer : public async_tcp_socket::SocketHttpServerApi
	{
	private:
		WString							requestPath = WString::Unmanaged(HttpServerUrl_Request) + L"/focused-token";
		WString							responsePath = WString::Unmanaged(HttpServerUrl_Response) + L"/focused-token";
		SpinLock						lockContexts;
		Ptr<async_tcp_socket::SocketHttpRequestContext>
									firstPoll;
		Ptr<async_tcp_socket::SocketHttpRequestContext>
									replacementPoll;

	protected:
		void OnHttpRequestReceived(Ptr<async_tcp_socket::SocketHttpRequestContext> context) override
		{
			auto path = context->GetRelativePath();
			if (path == HttpServerUrl_Connect)
			{
				connectCount++;
				context->Respond(CreateSocketHttpResponse(200, requestPath + L";" + responsePath));
			}
			else if (path == requestPath)
			{
				auto index = ++pollCount;
				if (index == 1)
				{
					SPIN_LOCK(lockContexts)
					{
						firstPoll = context;
					}
					eventFirstPoll.Signal();
				}
				else
				{
					SPIN_LOCK(lockContexts)
					{
						replacementPoll = context;
					}
					eventReplacementPoll.Signal();
				}
			}
			else if (path == responsePath)
			{
				responseCount++;
				responseBody = ReadSocketHttpBody(context->GetRequest()->body);
				context->Respond(CreateSocketHttpResponse(200, L"piggyback-reply"));
				eventResponse.Signal();
			}
			else
			{
				context->Respond(CreateSocketHttpResponse(404, WString::Empty));
			}
		}

	public:
		EventObject						eventFirstPoll;
		EventObject						eventReplacementPoll;
		EventObject						eventResponse;
		atomic_vint					connectCount = 0;
		atomic_vint					pollCount = 0;
		atomic_vint					responseCount = 0;
		WString							responseBody;

		PollScriptServer(const WString& _baseUrl, vint port)
			: SocketHttpServerApi(L"http://localhost:" + itow(port) + _baseUrl, false)
		{
			CHECK_ERROR(eventFirstPoll.CreateManualUnsignal(false), L"Failed to create the first-poll event.");
			CHECK_ERROR(eventReplacementPoll.CreateManualUnsignal(false), L"Failed to create the replacement-poll event.");
			CHECK_ERROR(eventResponse.CreateManualUnsignal(false), L"Failed to create the scripted response event.");
		}

		~PollScriptServer()
		{
			Stop();
		}

		bool RespondFirstPoll(const WString& body)
		{
			Ptr<async_tcp_socket::SocketHttpRequestContext> context;
			SPIN_LOCK(lockContexts)
			{
				context = firstPoll;
				firstPoll = nullptr;
			}
			return context && context->Respond(CreateSocketHttpResponse(200, body));
		}
	};

	class PollClientCallback : public FocusedProtocolCallback
	{
	private:
		atomic_vint*					receiveSubmissions = nullptr;

	public:
		EventObject						eventPiggyback;
		EventObject						eventPollMessage;
		bool							piggybackExact = false;
		bool							pollExact = false;
		bool							replacementBeforeCallback = false;

		PollClientCallback(atomic_vint* _receiveSubmissions)
			: receiveSubmissions(_receiveSubmissions)
		{
			CHECK_ERROR(eventPiggyback.CreateManualUnsignal(false), L"Failed to create the piggyback event.");
			CHECK_ERROR(eventPollMessage.CreateManualUnsignal(false), L"Failed to create the poll-message event.");
		}

		void OnReadString(const WString& value) override
		{
			readCount++;
			if (value == L"piggyback-reply")
			{
				piggybackExact = true;
				eventPiggyback.Signal();
			}
			else
			{
				pollExact = value == L"poll-message";
				replacementBeforeCallback = *receiveSubmissions >= 2;
				eventPollMessage.Signal();
			}
		}
	};

	class RetryScriptServer : public async_tcp_socket::SocketHttpServerApi
	{
	private:
		static WString EncodeBodyFingerprint(const async_tcp_socket::HttpBody& body)
		{
			Array<vuint8_t> bytes;
			CHECK_ERROR(async_tcp_socket::FlattenHttpBody(body, bytes), L"RetryScriptServer could not flatten a request body.");
			if (bytes.Count() == 0) return WString::Empty;
			const wchar_t* hex = L"0123456789ABCDEF";
			Array<wchar_t> fingerprint(bytes.Count() * 2);
			for (vint i = 0; i < bytes.Count(); i++)
			{
				fingerprint[i * 2] = hex[bytes[i] >> 4];
				fingerprint[i * 2 + 1] = hex[bytes[i] & 0x0F];
			}
			return WString::CopyFrom(&fingerprint[0], fingerprint.Count());
		}

		WString							requestPath = WString::Unmanaged(HttpServerUrl_Request) + L"/stable-token";
		WString							responsePath = WString::Unmanaged(HttpServerUrl_Response) + L"/stable-token";
		SpinLock						lockRecords;
		List<WString>					bodies;
		List<WString>					bodyFingerprints;
		List<WString>					targets;

	protected:
		void OnHttpRequestReceived(Ptr<async_tcp_socket::SocketHttpRequestContext> context) override
		{
			auto path = context->GetRelativePath();
			if (path == HttpServerUrl_Connect)
			{
				connectCount++;
				context->Respond(CreateSocketHttpResponse(200, requestPath + L";" + responsePath));
				return;
			}
			if (path != responsePath)
			{
				context->Respond(CreateSocketHttpResponse(404, WString::Empty));
				return;
			}

			auto request = context->GetRequest();
			auto body = ReadSocketHttpBody(request->body);
			auto bodyFingerprint = EncodeBodyFingerprint(request->body);
			vint index = 0;
			SPIN_LOCK(lockRecords)
			{
				bodies.Add(body);
				bodyFingerprints.Add(bodyFingerprint);
				targets.Add(path);
				index = bodies.Count();
			}
			if (index == 1)
			{
				eventFirstAttempt.Signal();
				eventReleaseFirstAttempt.WaitForTime(SocketHttpFocusedTimeout);
				context->Respond(CreateSocketHttpResponse(500, WString::Empty));
			}
			else if (index == 4)
			{
				context->Cancel();
			}
			else if (index >= 6)
			{
				context->Respond(CreateSocketHttpResponse(500, WString::Empty));
				if (index == 8) eventFatalAttempts.Signal();
			}
			else
			{
				context->Respond(CreateSocketHttpResponse(200, WString::Empty));
				if (index == 3) eventOrdered.Signal();
				if (index == 5) eventReplaced.Signal();
			}
		}

	public:
		EventObject						eventFirstAttempt;
		EventObject						eventReleaseFirstAttempt;
		EventObject						eventOrdered;
		EventObject						eventReplaced;
		EventObject						eventFatalAttempts;
		atomic_vint					connectCount = 0;

		RetryScriptServer(const WString& baseUrl, vint port)
			: SocketHttpServerApi(L"http://localhost:" + itow(port) + baseUrl, false)
		{
			CHECK_ERROR(eventFirstAttempt.CreateManualUnsignal(false), L"Failed to create the first-attempt event.");
			CHECK_ERROR(eventReleaseFirstAttempt.CreateManualUnsignal(false), L"Failed to create the release-first-attempt event.");
			CHECK_ERROR(eventOrdered.CreateManualUnsignal(false), L"Failed to create the ordered-send event.");
			CHECK_ERROR(eventReplaced.CreateManualUnsignal(false), L"Failed to create the replaced-send event.");
			CHECK_ERROR(eventFatalAttempts.CreateManualUnsignal(false), L"Failed to create the fatal-attempts event.");
		}

		~RetryScriptServer()
		{
			Stop();
		}

		vint RecordCount()
		{
			SPIN_LOCK(lockRecords)
			{
				return bodies.Count();
			}
			return 0;
		}

		WString Body(vint index)
		{
			SPIN_LOCK(lockRecords)
			{
				return bodies[index];
			}
			return {};
		}

		bool SameBodyBytes(vint first, vint second)
		{
			SPIN_LOCK(lockRecords)
			{
				return bodyFingerprints[first] == bodyFingerprints[second];
			}
			return false;
		}

		bool AllTargetsStable()
		{
			SPIN_LOCK(lockRecords)
			{
				for (auto&& target : targets)
				{
					if (target != responsePath) return false;
				}
				return true;
			}
			return false;
		}
	};

	class RetryClientCallback : public FocusedProtocolCallback
	{
	public:
		EventObject						eventFatalError;
		atomic_vint					localErrors = 0;
		atomic_vint					fatalErrors = 0;
		bool						fatalBeforeDisconnected = false;

		RetryClientCallback()
		{
			CHECK_ERROR(eventFatalError.CreateManualUnsignal(false), L"Failed to create the fatal-error event.");
		}

		void OnReadString(const WString&) override
		{
			readCount++;
		}

		void OnLocalError(const WString&, bool fatal) override
		{
			localErrors++;
			if (fatal)
			{
				fatalErrors++;
				eventFatalError.Signal();
			}
		}

		void OnDisconnected() override
		{
			fatalBeforeDisconnected = fatalErrors == 1;
			FocusedProtocolCallback::OnDisconnected();
		}
	};

	class SocketHttpFatalStopRaceState : public Object
	{
	public:
		EventObject						eventFatalReserved;
		EventObject						eventReleaseFatal;
		EventObject						eventStopStarted;
		EventObject						eventExternalStopReturned;
		atomic_vint					fatalReservedCount = 0;
		atomic_vint					fatalReleasedCount = 0;
		atomic_vint					stopStartedCount = 0;
		atomic_vint					externalStopReturns = 0;
		atomic_vint					externalStopErrors = 0;

		SocketHttpFatalStopRaceState()
		{
			CHECK_ERROR(eventFatalReserved.CreateManualUnsignal(false), L"Failed to create the fatal-Stop reserved event.");
			CHECK_ERROR(eventReleaseFatal.CreateManualUnsignal(false), L"Failed to create the fatal-Stop release event.");
			CHECK_ERROR(eventStopStarted.CreateManualUnsignal(false), L"Failed to create the fatal-Stop started event.");
			CHECK_ERROR(eventExternalStopReturned.CreateManualUnsignal(false), L"Failed to create the fatal-Stop external-return event.");
		}

		void FatalReserved()
		{
			fatalReservedCount++;
			eventFatalReserved.Signal();
			eventReleaseFatal.Wait();
			fatalReleasedCount++;
		}

		void StopStarted()
		{
			stopStartedCount++;
			eventStopStarted.Signal();
		}
	};

	template<typename TNativeClient>
	Ptr<INetworkProtocolClient> CreateSocketHttpProtocolClient(const WString& baseUrl, vint port, atomic_vint* factoryCalls = nullptr)
	{
		auto factory = async_tcp_socket::SocketHttpClient::NativeClientFactory([factoryCalls](vint nativePort)
		{
			if (factoryCalls) (*factoryCalls)++;
			return Ptr<async_tcp_socket::IAsyncSocketClient>(new TNativeClient(nativePort));
		});
		return Ptr<INetworkProtocolClient>(new async_tcp_socket::SocketHttpClient(baseUrl, port, factory));
	}

	WString RepeatSocketHttpCharacter(wchar_t character, vint count)
	{
		if (count == 0) return {};
		auto buffer = new wchar_t[count + 1];
		for (vint i = 0; i < count; i++) buffer[i] = character;
		buffer[count] = 0;
		return WString::TakeOver(buffer, count);
	}

	class FailedPollHookState : public Object
	{
	private:
		SpinLock						lock;
		List<WString>					claimedTokens;
		List<Pair<WString, bool>>		completedTokens;

	public:
		EventObject						eventClaimed;
		EventObject						eventReleaseClaim;
		EventObject						eventFirstCompletion;
		EventObject						eventSecondCompletion;
		bool							claimReleased = false;

		FailedPollHookState()
		{
			CHECK_ERROR(eventClaimed.CreateManualUnsignal(false), L"Failed to create the poll-claimed event.");
			CHECK_ERROR(eventReleaseClaim.CreateManualUnsignal(false), L"Failed to create the release-claim event.");
			CHECK_ERROR(eventFirstCompletion.CreateManualUnsignal(false), L"Failed to create the first poll-completion event.");
			CHECK_ERROR(eventSecondCompletion.CreateManualUnsignal(false), L"Failed to create the second poll-completion event.");
		}

		void Claimed(const WString& token)
		{
			vint count = 0;
			SPIN_LOCK(lock)
			{
				claimedTokens.Add(token);
				count = claimedTokens.Count();
			}
			if (count == 1)
			{
				eventClaimed.Signal();
				claimReleased = eventReleaseClaim.WaitForTime(SocketHttpFocusedTimeout);
			}
		}

		void Completed(const WString& token, bool succeeded)
		{
			vint count = 0;
			SPIN_LOCK(lock)
			{
				completedTokens.Add(Pair<WString, bool>(token, succeeded));
				count = completedTokens.Count();
			}
			if (count == 1) eventFirstCompletion.Signal();
			if (count == 2) eventSecondCompletion.Signal();
		}

		bool Validate()
		{
			SPIN_LOCK(lock)
			{
				return
					claimedTokens.Count() == 2 &&
					claimedTokens[0] != WString::Empty &&
					claimedTokens[0] == claimedTokens[1] &&
					completedTokens.Count() == 2 &&
					completedTokens[0].key == claimedTokens[0] &&
					!completedTokens[0].value &&
					completedTokens[1].key == claimedTokens[0] &&
					completedTokens[1].value;
			}
			return false;
		}
	};

#ifdef VCZH_MSVC

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

	class NamedPipeChannelServer : public ChannelServer<NamedPipeServer>
	{
	public:
		NamedPipeChannelServer(ChannelChatData& chatData, const WString& pipeName)
			: ChannelServer<NamedPipeServer>(chatData, pipeName)
		{
		}
	};

	class HttpChannelServer : public ChannelServer<HttpServer>
	{
	public:
		HttpChannelServer(ChannelChatData& chatData, const WString& baseUrl, vint port)
			: ChannelServer<HttpServer>(chatData, baseUrl, port)
		{
		}
	};

#endif

	template<typename TAsyncSocketServer>
	class AsyncSocketTextServer
		: protected TextServerCallbackHost
		, public async_tcp_socket::NetworkProtocolServer<TAsyncSocketServer>
	{
		using Base = async_tcp_socket::NetworkProtocolServer<TAsyncSocketServer>;

	public:
		AsyncSocketTextServer(ChatData& chatData, vint port)
			: TextServerCallbackHost(chatData)
			, Base(port)
		{
		}

		WaitForClientResult OnClientConnected(INetworkProtocolConnection* connection) override
		{
			return AcceptTextConnection(connection);
		}
	};

	template<typename TAsyncSocketServer>
	class AsyncSocketChannelServer
		: public ChannelServer<async_tcp_socket::NetworkProtocolServer<TAsyncSocketServer>>
	{
		using Base = ChannelServer<async_tcp_socket::NetworkProtocolServer<TAsyncSocketServer>>;

	public:
		AsyncSocketChannelServer(ChannelChatData& chatData, vint port)
			: Base(chatData, port)
		{
		}
	};
}

template<typename TAsyncSocketServer, typename TAsyncSocketClient>
void RunAsyncSocketNetworkProtocolTestCases(bool synchronizeServerStartup)
{
	TEST_CASE(L"AsyncSocket (NetworkProtocol)")
	{
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			const vint port = 38500 + i;
			RunTextNetworkProtocol(
				[port](ChatData& chatData)->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new AsyncSocketTextServer<TAsyncSocketServer>(chatData, port)); },
				[port]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new async_tcp_socket::NetworkProtocolClient<TAsyncSocketClient>(port)); },
				synchronizeServerStartup
			);
		}
	});

	TEST_CASE(L"AsyncSocket (Channel)")
	{
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			const vint port = 38600 + i;
			RunNetworkProtocolChannel(
				[port](ChannelChatData& chatData)->Ptr<IChannelServer<WString>> { return Ptr<IChannelServer<WString>>(new AsyncSocketChannelServer<TAsyncSocketServer>(chatData, port)); },
				[port]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new async_tcp_socket::NetworkProtocolClient<TAsyncSocketClient>(port)); },
				synchronizeServerStartup
			);
		}
	});
}

template<typename TNativeServer, typename TNativeClient>
void RunSocketHttpNetworkProtocolTestCases()
{
	TEST_CASE(L"SocketHttp portable pair (NetworkProtocol)")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			const vint port = 39000 + i;
			const WString baseUrl = L"/VlppOSTestSocketHttpProtocol";
			RunTextNetworkProtocol(
				[port, baseUrl](ChatData& chatData)->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new SocketHttpTextServer(chatData, baseUrl, port)); },
				[port, baseUrl]()->Ptr<INetworkProtocolClient> { return CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port); },
				true
			);
		}
	});

	TEST_CASE(L"SocketHttp portable pair (Channel)")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			const vint port = 39100 + i;
			const WString baseUrl = L"/VlppOSTestSocketHttpChannel";
			RunNetworkProtocolChannel(
				[port, baseUrl](ChannelChatData& chatData)->Ptr<IChannelServer<WString>> { return Ptr<IChannelServer<WString>>(new SocketHttpChannelServer(chatData, baseUrl, port)); },
				[port, baseUrl]()->Ptr<INetworkProtocolClient> { return CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port); },
				true
			);
		}
	});
}

#ifdef VCZH_MSVC
template<typename TNativeServer, typename TNativeClient>
void RunSocketHttpWindowsInteropTestCases()
{
	TEST_CASE(L"SocketHttpServer with Windows HttpClient (NetworkProtocol)")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			const vint port = 39200 + i;
			const WString baseUrl = L"/VlppOSTestSocketHttpWinClientProtocol";
			RunTextNetworkProtocol(
				[port, baseUrl](ChatData& chatData)->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new SocketHttpTextServer(chatData, baseUrl, port)); },
				[port, baseUrl]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new HttpClient(baseUrl, port)); },
				true
			);
		}
	});

	TEST_CASE(L"SocketHttpServer with Windows HttpClient (Channel)")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			const vint port = 39300 + i;
			const WString baseUrl = L"/VlppOSTestSocketHttpWinClientChannel";
			RunNetworkProtocolChannel(
				[port, baseUrl](ChannelChatData& chatData)->Ptr<IChannelServer<WString>> { return Ptr<IChannelServer<WString>>(new SocketHttpChannelServer(chatData, baseUrl, port)); },
				[port, baseUrl]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new HttpClient(baseUrl, port)); },
				true
			);
		}
	});

	TEST_CASE(L"Windows HttpClient preserves legacy first-semicolon Connect parsing")
	{
		const vint port = 39630;
		const WString baseUrl = L"/VlppOSTestWinClientLegacyConnect";
		List<Ptr<async_tcp_socket::HttpResponse>> responses;
		responses.Add(CreateSocketHttpScriptResponse(200, L"/request-path;/response-path;legacy-suffix"));
		auto nativeServer = Ptr<async_tcp_socket::IAsyncSocketServer>(new TNativeServer(port));
		auto server = Ptr(new SocketHttpConnectResponseScriptServer(
			nativeServer,
			baseUrl + HttpServerUrl_Connect,
			responses
			));
		server->Start();

		ExactMessageCallback callback(L"");
		auto client = Ptr(new HttpClient(baseUrl, port));
		client->GetConnection()->InstallCallback(&callback);
		client->WaitForServer();
		TEST_ASSERT(client->GetStatus() == ClientStatus::Connected);
		TEST_ASSERT(callback.Connection() == client->GetConnection());
		client->GetConnection()->Stop();
		server->Stop();
	});

	TEST_CASE(L"Windows HttpServer with SocketHttpClient (NetworkProtocol)")
	{
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			const vint port = 39400 + i;
			const WString baseUrl = L"/VlppOSTestWinServerSocketHttpProtocol";
			RunTextNetworkProtocol(
				[port, baseUrl](ChatData& chatData)->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new HttpTextServer(chatData, baseUrl, port)); },
				[port, baseUrl]()->Ptr<INetworkProtocolClient> { return CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port); },
				true
			);
		}
	});

	TEST_CASE(L"Windows HttpServer with SocketHttpClient (Channel)")
	{
		for (vint i = 0; i < InterProcessTestRepeatCount; i++)
		{
			const vint port = 39500 + i;
			const WString baseUrl = L"/VlppOSTestWinServerSocketHttpChannel";
			RunNetworkProtocolChannel(
				[port, baseUrl](ChannelChatData& chatData)->Ptr<IChannelServer<WString>> { return Ptr<IChannelServer<WString>>(new HttpChannelServer(chatData, baseUrl, port)); },
				[port, baseUrl]()->Ptr<INetworkProtocolClient> { return CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port); },
				true
			);
		}
	});
}
#endif

template<typename TNativeServer, typename TNativeClient>
void RunSocketHttpFocusedTestCases()
{
	TEST_CASE(L"SocketHttp server preserves strict Network Protocol request wire forms")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		const vint port = 39611;
		const WString baseUrl = L"/VlppOSTestSocketHttpFocusedWireServer";
		SocketHttpRecordingCallback callback;
		auto server = Ptr(new SingleConnectionSocketHttpServer(baseUrl, port, &callback));
		server->Start();

		auto expectStatus = [port](Ptr<async_tcp_socket::HttpRequest> request, vint statusCode)
		{
			auto response = SubmitSocketHttpRawQuery<TNativeClient>(port, request);
			TEST_ASSERT(response->statusCode == statusCode);
			return response;
		};

		auto connectRequest = CreateSocketHttpRawRequest(port, L"GET", baseUrl + HttpServerUrl_Connect);
		auto connectResponse = expectStatus(connectRequest, 200);
		TEST_ASSERT(async_tcp_socket::CountHttpFields(connectResponse->headers, L"content-type") == 1);
		auto connectContentType = async_tcp_socket::FindHttpField(connectResponse->headers, L"content-type");
		TEST_ASSERT(connectContentType != nullptr);
		TEST_ASSERT(async_tcp_socket::HttpFieldValueEqualsAscii(
			connectContentType->value,
			HttpNetworkProtocolContentType
			));
		TEST_ASSERT(callback.eventInstalled.WaitForTime(SocketHttpFocusedTimeout));
		auto connectBody = ReadSocketHttpBody(connectResponse->body);
		auto separator = connectBody.IndexOf(L';');
		TEST_ASSERT(separator > 0 && separator + 1 < connectBody.Length());
		auto requestPath = connectBody.Left(separator);
		auto responsePath = connectBody.Right(connectBody.Length() - separator - 1);
		auto requestPrefix = WString::Unmanaged(HttpServerUrl_Request) + L"/";
		auto responsePrefix = WString::Unmanaged(HttpServerUrl_Response) + L"/";
		TEST_ASSERT(requestPath.Length() == requestPrefix.Length() + 36);
		TEST_ASSERT(responsePath.Length() == responsePrefix.Length() + 36);
		TEST_ASSERT(requestPath.Left(requestPrefix.Length()) == requestPrefix);
		TEST_ASSERT(responsePath.Left(responsePrefix.Length()) == responsePrefix);
		TEST_ASSERT(requestPath.Right(36) == responsePath.Right(36));
		auto connection = callback.Connection();
		TEST_ASSERT(connection != nullptr);

		connection->SendString(L"poll-without-length");
		auto pollWithoutLength = CreateSocketHttpRawRequest(port, L"POST", baseUrl + requestPath);
		auto pollWithoutLengthResponse = expectStatus(pollWithoutLength, 200);
		TEST_ASSERT(ReadSocketHttpBody(pollWithoutLengthResponse->body) == L"poll-without-length");

		connection->SendString(L"poll-with-zero-length");
		auto pollWithZeroLength = CreateSocketHttpRawRequest(port, L"POST", baseUrl + requestPath);
		pollWithZeroLength->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"0"));
		auto pollWithZeroLengthResponse = expectStatus(pollWithZeroLength, 200);
		TEST_ASSERT(ReadSocketHttpBody(pollWithZeroLengthResponse->body) == L"poll-with-zero-length");

		auto addSubmittedBody = [](Ptr<async_tcp_socket::HttpRequest> request, const WString& contentLength, const WString& body)
		{
			request->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", contentLength));
			request->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
			if (body != WString::Empty)
			{
				AddSocketHttpBody(request->body, body);
			}
		};

		auto submittedPlain = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		addSubmittedBody(submittedPlain, L"3", L"abc");
		expectStatus(submittedPlain, 200);
		TEST_ASSERT(callback.MessageCount() == 1);
		TEST_ASSERT(callback.Message(0) == L"abc");

		auto submittedOws = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		addSubmittedBody(submittedOws, L"\t003 \t", L"def");
		expectStatus(submittedOws, 200);
		TEST_ASSERT(callback.MessageCount() == 2);
		TEST_ASSERT(callback.Message(1) == L"def");

		auto missingLength = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		missingLength->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
		auto missingLengthResponse = expectStatus(missingLength, 404);
		TEST_ASSERT(async_tcp_socket::CountHttpFields(missingLengthResponse->headers, L"content-type") == 0);
		TEST_ASSERT(missingLengthResponse->body.chunks.Count() == 0);
		TEST_ASSERT(missingLengthResponse->body.trailers.Count() == 0);

		auto zeroLength = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		addSubmittedBody(zeroLength, L"0", WString::Empty);
		expectStatus(zeroLength, 404);

		auto duplicateLength = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		addSubmittedBody(duplicateLength, L"3", L"abc");
		duplicateLength->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"3"));
		expectStatus(duplicateLength, 404);

		auto commaLength = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		addSubmittedBody(commaLength, L"3, 3", L"abc");
		expectStatus(commaLength, 404);

		auto chunked = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		chunked->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"transfer-encoding", L"chunked"));
		chunked->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
		AddSocketHttpBody(chunked->body, L"abc");
		expectStatus(chunked, 404);

		auto trailerBearing = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		trailerBearing->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
		AddSocketHttpBody(trailerBearing->body, L"abc");
		trailerBearing->body.trailers.Add(async_tcp_socket::CreateAsciiHttpField(L"x-characterization", L"trailer"));
		expectStatus(trailerBearing, 404);

		auto missingType = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		missingType->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"3"));
		AddSocketHttpBody(missingType->body, L"abc");
		expectStatus(missingType, 404);

		auto duplicateType = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		addSubmittedBody(duplicateType, L"3", L"abc");
		duplicateType->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
		expectStatus(duplicateType, 404);

		auto wrongType = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		wrongType->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"3"));
		wrongType->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", L"application/json; charset=utf-8"));
		AddSocketHttpBody(wrongType->body, L"abc");
		expectStatus(wrongType, 404);

		vuint8_t malformedUtf8Bytes[] = { 0xC0, 0x80 };
		auto malformedUtf8 = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		malformedUtf8->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"2"));
		malformedUtf8->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
		AddSocketHttpBody(malformedUtf8->body, malformedUtf8Bytes, 2);
		expectStatus(malformedUtf8, 404);

		vuint8_t truncatedUtf8Bytes[] = { 0xE2, 0x82 };
		auto truncatedUtf8 = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		truncatedUtf8->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"2"));
		truncatedUtf8->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
		AddSocketHttpBody(truncatedUtf8->body, truncatedUtf8Bytes, 2);
		expectStatus(truncatedUtf8, 404);

		vuint8_t nulByte[] = { 0 };
		auto embeddedNul = CreateSocketHttpRawRequest(port, L"POST", baseUrl + responsePath);
		embeddedNul->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"1"));
		embeddedNul->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
		AddSocketHttpBody(embeddedNul->body, nulByte, 1);
		expectStatus(embeddedNul, 404);
		TEST_ASSERT(callback.MessageCount() == 2);

		auto connectWithZeroLength = CreateSocketHttpRawRequest(port, L"GET", baseUrl + HttpServerUrl_Connect);
		connectWithZeroLength->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"0"));
		expectStatus(connectWithZeroLength, 200);

		auto verifyRejectedEmptyBodyForms = [=](const WString& method, const WString& target)
		{
			auto positiveLength = CreateSocketHttpRawRequest(port, method, target);
			positiveLength->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"1"));
			AddSocketHttpBody(positiveLength->body, L"x");
			expectStatus(positiveLength, 404);

			auto duplicateLength = CreateSocketHttpRawRequest(port, method, target);
			duplicateLength->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"0"));
			duplicateLength->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"0"));
			expectStatus(duplicateLength, 404);

			auto commaLength = CreateSocketHttpRawRequest(port, method, target);
			commaLength->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-length", L"0, 0"));
			expectStatus(commaLength, 404);

			auto transferCoded = CreateSocketHttpRawRequest(port, method, target);
			transferCoded->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"transfer-encoding", L"chunked"));
			expectStatus(transferCoded, 404);

			auto trailerBearing = CreateSocketHttpRawRequest(port, method, target);
			trailerBearing->body.trailers.Add(async_tcp_socket::CreateAsciiHttpField(L"x-characterization", L"trailer"));
			expectStatus(trailerBearing, 404);
		};
		verifyRejectedEmptyBodyForms(L"GET", baseUrl + HttpServerUrl_Connect);
		verifyRejectedEmptyBodyForms(L"POST", baseUrl + requestPath);

		server->Stop();
	});

	TEST_CASE(L"SocketHttp client preserves successful-response and Connect-path compatibility")
	{
		auto normalPair = WString::Unmanaged(HttpServerUrl_Request) + L"/script-token;" + HttpServerUrl_Response + L"/script-token";
		auto runScript = [normalPair](vint port, const List<Ptr<async_tcp_socket::HttpResponse>>& responses, vint expectedErrors)
		{
			auto nativeServer = Ptr<async_tcp_socket::IAsyncSocketServer>(new TNativeServer(port));
			auto server = Ptr(new SocketHttpConnectResponseScriptServer(nativeServer, HttpServerUrl_Connect, responses));
			server->Start();
			RetryClientCallback callback;
			auto client = CreateSocketHttpProtocolClient<TNativeClient>(WString::Empty, port);
			client->GetConnection()->InstallCallback(&callback);
			client->WaitForServer();
			TEST_ASSERT(client->GetStatus() == ClientStatus::Connected);
			TEST_ASSERT(callback.localErrors == expectedErrors);
			TEST_ASSERT(callback.fatalErrors == 0);
			TEST_ASSERT(callback.readCount == 0);
			client->GetConnection()->Stop();
			server->Stop();
		};

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			auto chunkedTrailerContentType = Ptr(new async_tcp_socket::HttpResponse);
			chunkedTrailerContentType->statusCode = 200;
			chunkedTrailerContentType->reason = L"OK";
			AddSocketHttpBody(chunkedTrailerContentType->body, WString::Unmanaged(HttpServerUrl_Request) + L"/trailing/;");
			AddSocketHttpBody(chunkedTrailerContentType->body, WString::Unmanaged(HttpServerUrl_Response) + L"/trailing/");
			chunkedTrailerContentType->body.trailers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
			chunkedTrailerContentType->body.trailers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", L"application/octet-stream"));
			responses.Add(chunkedTrailerContentType);
			runScript(39612, responses, 0);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			auto headerFirstDuplicateContentType = CreateSocketHttpScriptResponse(200, normalPair);
			headerFirstDuplicateContentType->body.trailers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", L"application/octet-stream"));
			responses.Add(headerFirstDuplicateContentType);
			runScript(39620, responses, 0);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			responses.Add(CreateSocketHttpScriptResponse(500, normalPair));
			auto missingContentType = CreateSocketHttpScriptResponse(200, normalPair);
			missingContentType->headers.Clear();
			responses.Add(missingContentType);
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39613, responses, 2);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			auto wrongCaseContentType = CreateSocketHttpScriptResponse(200, normalPair);
			wrongCaseContentType->headers[0] = async_tcp_socket::CreateAsciiHttpField(L"content-type", L"Application/Json; Charset=UTF8");
			responses.Add(wrongCaseContentType);
			auto wrongContentType = CreateSocketHttpScriptResponse(200, normalPair);
			wrongContentType->headers[0] = async_tcp_socket::CreateAsciiHttpField(L"content-type", L"application/json; charset=utf-8");
			responses.Add(wrongContentType);
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39621, responses, 2);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			auto wrongFirstContentType = CreateSocketHttpScriptResponse(200, normalPair);
			wrongFirstContentType->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
			wrongFirstContentType->headers[0] = async_tcp_socket::CreateAsciiHttpField(L"content-type", L"application/octet-stream");
			responses.Add(wrongFirstContentType);
			auto correctFirstContentType = CreateSocketHttpScriptResponse(200, normalPair);
			correctFirstContentType->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", L"application/octet-stream"));
			responses.Add(correctFirstContentType);
			runScript(39626, responses, 1);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			auto wrongHeaderCorrectTrailer = CreateSocketHttpScriptResponse(200, normalPair);
			wrongHeaderCorrectTrailer->headers[0] = async_tcp_socket::CreateAsciiHttpField(L"content-type", L"application/octet-stream");
			wrongHeaderCorrectTrailer->body.trailers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-type", HttpNetworkProtocolContentType));
			responses.Add(wrongHeaderCorrectTrailer);
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39627, responses, 1);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			vuint8_t malformedUtf8Bytes[] = { 0xC0, 0x80 };
			auto malformedUtf8 = CreateSocketHttpScriptResponse(200, WString::Empty);
			AddSocketHttpBody(malformedUtf8->body, malformedUtf8Bytes, 2);
			responses.Add(malformedUtf8);
			vuint8_t nulByte[] = { 0 };
			auto embeddedNul = CreateSocketHttpScriptResponse(200, WString::Empty);
			AddSocketHttpBody(embeddedNul->body, nulByte, 1);
			responses.Add(embeddedNul);
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39614, responses, 2);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			responses.Add(CreateSocketHttpScriptResponse(200, WString::Empty));
			responses.Add(CreateSocketHttpScriptResponse(200, L"/missing-delimiter"));
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39615, responses, 2);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			responses.Add(CreateSocketHttpScriptResponse(200, L";/response"));
			responses.Add(CreateSocketHttpScriptResponse(200, L"/request;"));
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39622, responses, 2);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			responses.Add(CreateSocketHttpScriptResponse(200, L"/first;/second;/third"));
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39623, responses, 1);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			auto unsupportedEncoding = CreateSocketHttpScriptResponse(200, normalPair);
			unsupportedEncoding->headers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-encoding", L"gzip"));
			responses.Add(unsupportedEncoding);
			auto unsupportedTrailerEncoding = CreateSocketHttpScriptResponse(200, normalPair);
			unsupportedTrailerEncoding->body.trailers.Add(async_tcp_socket::CreateAsciiHttpField(L"content-encoding", L"gzip"));
			responses.Add(unsupportedTrailerEncoding);
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39616, responses, 2);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			responses.Add(CreateSocketHttpScriptResponse(200, L"/illegal%2Frequest;/legal-response"));
			responses.Add(CreateSocketHttpScriptResponse(200, L"/legal-request;/malformed%2"));
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39617, responses, 2);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			responses.Add(CreateSocketHttpScriptResponse(200, L"/legal-request;/raw-\x4F60"));
			auto overlongPath = L"/" + RepeatSocketHttpCharacter(L'a', async_tcp_socket::HttpRequestLineSizeLimit - 10 - 4);
			responses.Add(CreateSocketHttpScriptResponse(200, overlongPath + L";/response"));
			auto exactPath = L"/" + RepeatSocketHttpCharacter(L'a', async_tcp_socket::HttpRequestLineSizeLimit - 10 - 4 - 1);
			responses.Add(CreateSocketHttpScriptResponse(200, exactPath + L";/response"));
			runScript(39624, responses, 2);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			auto overlongPath = L"/" + RepeatSocketHttpCharacter(L'a', async_tcp_socket::HttpRequestLineSizeLimit - 10 - 4);
			responses.Add(CreateSocketHttpScriptResponse(200, L"/request;" + overlongPath));
			auto exactPath = L"/" + RepeatSocketHttpCharacter(L'a', async_tcp_socket::HttpRequestLineSizeLimit - 10 - 4 - 1);
			responses.Add(CreateSocketHttpScriptResponse(200, exactPath + L";" + exactPath));
			runScript(39625, responses, 1);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			responses.Add(CreateSocketHttpScriptResponse(200, L"/legal-request;/encoded%5Cseparator"));
			responses.Add(CreateSocketHttpScriptResponse(200, L"/encoded%00nul;/legal-response"));
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39628, responses, 2);
		}

		{
			List<Ptr<async_tcp_socket::HttpResponse>> responses;
			responses.Add(CreateSocketHttpScriptResponse(200, L"/legal-request;/back\\slash"));
			responses.Add(CreateSocketHttpScriptResponse(200, L"/query?value;/legal-response"));
			responses.Add(CreateSocketHttpScriptResponse(200, normalPair));
			runScript(39629, responses, 2);
		}
	});

	TEST_CASE(L"SocketHttp constructors preserve base-prefix and request-line boundaries")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		const vint port = 39618;
		auto clientFactory = async_tcp_socket::SocketHttpClient::NativeClientFactory([](vint nativePort)
		{
			return Ptr<async_tcp_socket::IAsyncSocketClient>(new TNativeClient(nativePort));
		});
		auto createServer = [port](const WString& baseUrl)
		{
			return Ptr(new async_tcp_socket::SocketHttpServer(baseUrl, port));
		};
		auto createClient = [port, clientFactory](const WString& baseUrl)
		{
			return Ptr(new async_tcp_socket::SocketHttpClient(baseUrl, port, clientFactory));
		};

		{
			auto server = createServer(WString::Empty);
			auto client = createClient(WString::Empty);
		}
		{
			const WString legal = L"/legal;%E4%BD%A0:@";
			auto server = createServer(legal);
			auto client = createClient(legal);
		}

		List<WString> invalidBaseUrls;
		invalidBaseUrls.Add(L"/");
		invalidBaseUrls.Add(L"/trailing/");
		invalidBaseUrls.Add(L"/query?value");
		invalidBaseUrls.Add(L"/fragment#value");
		invalidBaseUrls.Add(L"/back\\slash");
		invalidBaseUrls.Add(WString::Unmanaged(L"/raw-\x4F60"));
		invalidBaseUrls.Add(L"/malformed%2");
		invalidBaseUrls.Add(L"/encoded%2Fseparator");
		invalidBaseUrls.Add(L"/encoded%5cseparator");
		invalidBaseUrls.Add(L"/encoded%00nul");
		invalidBaseUrls.Add(WString::CopyFrom(L"/a\0b", 4));
		for (auto&& baseUrl : invalidBaseUrls)
		{
			TEST_ERROR(createServer(baseUrl));
			TEST_ERROR(createClient(baseUrl));
		}

		const vint requestRouteLength = WString::Unmanaged(HttpServerUrl_Request).Length();
		const vint responseRouteLength = WString::Unmanaged(HttpServerUrl_Response).Length();
		const vint longestServerPostRoute = requestRouteLength > responseRouteLength ? requestRouteLength : responseRouteLength;
		const vint serverBaseUrlLimit =
			async_tcp_socket::HttpRequestLineSizeLimit - 10 - WString::Unmanaged(L"POST").Length() - longestServerPostRoute - 1 - 36;
		auto exactServerBaseUrl = L"/" + RepeatSocketHttpCharacter(L'a', serverBaseUrlLimit - 1);
		{
			auto server = createServer(exactServerBaseUrl);
		}
		TEST_ERROR(createServer(exactServerBaseUrl + L"a"));

		const vint clientBaseUrlLimit =
			async_tcp_socket::HttpRequestLineSizeLimit - 10 - WString::Unmanaged(L"GET").Length() - WString::Unmanaged(HttpServerUrl_Connect).Length();
		auto exactClientBaseUrl = L"/" + RepeatSocketHttpCharacter(L'a', clientBaseUrlLimit - 1);
		{
			auto client = createClient(exactClientBaseUrl);
		}
		TEST_ERROR(createClient(exactClientBaseUrl + L"a"));
	});

	TEST_CASE(L"SocketHttp empty poll response submits a replacement without delivering a message")
	{
		const vint port = 39619;
		const WString baseUrl = L"/VlppOSTestSocketHttpFocusedEmptyPoll";
		auto server = Ptr(new PollScriptServer(baseUrl, port));
		server->Start();
		atomic_vint receiveSubmissions = 0;
		SocketHttpReceiveSubmissionHookScope receiveSubmissionHook(Func<void()>([&receiveSubmissions]() { receiveSubmissions++; }));
		PollClientCallback callback(&receiveSubmissions);
		auto client = CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port);
		client->GetConnection()->InstallCallback(&callback);
		client->WaitForServer();
		client->GetConnection()->BeginReadingLoopUnsafe();
		TEST_ASSERT(server->eventFirstPoll.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(server->RespondFirstPoll(WString::Empty));
		TEST_ASSERT(server->eventReplacementPoll.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(receiveSubmissions >= 2);
		TEST_ASSERT(callback.readCount == 0);
		client->GetConnection()->Stop();
		server->Stop();
	});

	TEST_CASE(L"SocketHttp replacement poll precedes callback and Response reply is piggybacked")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		const vint port = 39600;
		const WString baseUrl = L"/VlppOSTestSocketHttpFocusedPoll";
		atomic_vint receiveSubmissions = 0;
		SocketHttpReceiveSubmissionHookScope receiveSubmissionHook(Func<void()>([&receiveSubmissions]() { receiveSubmissions++; }));
		auto server = Ptr(new PollScriptServer(baseUrl, port));
		server->Start();
		PollClientCallback callback(&receiveSubmissions);
		auto client = CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port);
		client->GetConnection()->InstallCallback(&callback);
		client->WaitForServer();
		TEST_ASSERT(client->GetStatus() == ClientStatus::Connected);
		client->GetConnection()->BeginReadingLoopUnsafe();
		TEST_ASSERT(server->eventFirstPoll.WaitForTime(SocketHttpFocusedTimeout));

		client->GetConnection()->SendString(L"inbound-response");
		TEST_ASSERT(server->eventResponse.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(callback.eventPiggyback.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(server->pollCount == 1);
		TEST_ASSERT(server->responseBody == L"inbound-response");
		TEST_ASSERT(callback.piggybackExact);

		TEST_ASSERT(server->RespondFirstPoll(L"poll-message"));
		TEST_ASSERT(callback.eventPollMessage.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(server->eventReplacementPoll.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(callback.pollExact);
		TEST_ASSERT(callback.replacementBeforeCallback);
		TEST_ASSERT(server->pollCount >= 2);
		client->GetConnection()->Stop();
		server->Stop();
		TEST_ASSERT(server->connectCount == 1);
		TEST_ASSERT(server->responseCount == 1);

		const vint piggybackPort = 39607;
		const WString piggybackBaseUrl = L"/VlppOSTestSocketHttpFocusedServerPiggyback";
		ExactMessageCallback piggybackServerCallback(L"inbound-response", L"server-piggyback");
		auto piggybackServer = Ptr(new SingleConnectionSocketHttpServer(piggybackBaseUrl, piggybackPort, &piggybackServerCallback));
		piggybackServer->Start();
		auto piggybackClient = CreateFocusedSocketHttpApi<TNativeClient>(piggybackPort);
		piggybackClient->WaitForServer();

		auto connectState = Ptr(new SocketHttpQueryState);
		SubmitSocketHttpQuery(piggybackClient, L"GET", piggybackBaseUrl + HttpServerUrl_Connect, WString::Empty, SocketHttpFocusedTimeout, connectState);
		TEST_ASSERT(connectState->eventCompleted.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(!connectState->Failed());
		TEST_ASSERT(connectState->StatusCode() == 200);
		TEST_ASSERT(piggybackServerCallback.eventInstalled.WaitForTime(SocketHttpFocusedTimeout));
		auto connectBody = connectState->Body();
		auto separator = connectBody.IndexOf(L';');
		TEST_ASSERT(separator > 0 && separator + 1 < connectBody.Length());
		auto responsePath = connectBody.Sub(separator + 1, connectBody.Length() - separator - 1);

		auto responseState = Ptr(new SocketHttpQueryState);
		SubmitSocketHttpQuery(piggybackClient, L"POST", piggybackBaseUrl + responsePath, L"inbound-response", SocketHttpFocusedTimeout, responseState);
		TEST_ASSERT(responseState->eventCompleted.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(piggybackServerCallback.eventRead.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(!responseState->Failed());
		TEST_ASSERT(responseState->StatusCode() == 200);
		TEST_ASSERT(responseState->Body() == L"server-piggyback");
		TEST_ASSERT(piggybackServerCallback.exact);
		piggybackClient->Stop();
		piggybackServer->Stop();
	});

	TEST_CASE(L"SocketHttp send FIFO retries its head, replaces one physical lane, and disconnects after attempt three")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		const vint port = 39601;
		const WString baseUrl = L"/VlppOSTestSocketHttpFocusedRetry";
		auto server = Ptr(new RetryScriptServer(baseUrl, port));
		server->Start();
		atomic_vint factoryCalls = 0;
		RetryClientCallback callback;
		auto client = CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port, &factoryCalls);
		client->GetConnection()->InstallCallback(&callback);
		client->WaitForServer();
		TEST_ASSERT(client->GetStatus() == ClientStatus::Connected);
		TEST_ASSERT(factoryCalls == 2);
		SignalSocketHttpEventOnExit releaseFirstAttempt(server->eventReleaseFirstAttempt);

		client->GetConnection()->SendString(L"first");
		TEST_ASSERT(server->eventFirstAttempt.WaitForTime(SocketHttpFocusedTimeout));
		client->GetConnection()->SendString(L"second");
		server->eventReleaseFirstAttempt.Signal();
		TEST_ASSERT(server->eventOrdered.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(callback.readCount == 0);
		TEST_ASSERT(server->RecordCount() == 3);
		TEST_ASSERT(server->Body(0) == L"first");
		TEST_ASSERT(server->Body(1) == L"first");
		TEST_ASSERT(server->Body(2) == L"second");
		TEST_ASSERT(server->SameBodyBytes(0, 1));

		client->GetConnection()->SendString(L"replace-\x4F60");
		TEST_ASSERT(server->eventReplaced.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(server->RecordCount() == 5);
		TEST_ASSERT(server->Body(3) == L"replace-\x4F60");
		TEST_ASSERT(server->Body(4) == L"replace-\x4F60");
		TEST_ASSERT(server->SameBodyBytes(3, 4));
		TEST_ASSERT(server->AllTargetsStable());
		TEST_ASSERT(server->connectCount == 1);
		TEST_ASSERT(factoryCalls >= 3);
		TEST_ASSERT(callback.localErrors >= 2);
		TEST_ASSERT(callback.fatalErrors == 0);
		TEST_ASSERT(client->GetStatus() == ClientStatus::Connected);

		auto fatalStopState = Ptr(new SocketHttpFatalStopRaceState);
		SocketHttpFatalStopHookScope fatalStopHooks(
			Func<void()>([fatalStopState]() { fatalStopState->FatalReserved(); }),
			Func<void()>([fatalStopState]() { fatalStopState->StopStarted(); })
			);
		SocketHttpStopThreadScope fatalStopThreadScope(&fatalStopState->eventReleaseFatal);
		client->GetConnection()->SendString(L"fatal");
		TEST_ASSERT(server->eventFatalAttempts.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(fatalStopState->eventFatalReserved.WaitForTime(SocketHttpFocusedTimeout));

		auto externalStopThread = Thread::CreateAndStart([fatalStopState, connection = client->GetConnection()]()
		{
			try
			{
				connection->Stop();
				fatalStopState->externalStopReturns++;
			}
			catch (...)
			{
				fatalStopState->externalStopErrors++;
			}
			fatalStopState->eventExternalStopReturned.Signal();
		}, false);
		fatalStopThreadScope.SetFirst(externalStopThread);
		TEST_ASSERT(fatalStopState->eventStopStarted.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(callback.fatalErrors == 0);
		TEST_ASSERT(!callback.eventDisconnected.WaitForTime(0));
		TEST_ASSERT(!fatalStopState->eventExternalStopReturned.WaitForTime(0));

		fatalStopState->eventReleaseFatal.Signal();
		TEST_ASSERT(callback.eventFatalError.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(callback.eventDisconnected.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(fatalStopState->eventExternalStopReturned.WaitForTime(SocketHttpFocusedTimeout));
		fatalStopThreadScope.JoinAll();
		TEST_ASSERT(server->RecordCount() == 8);
		TEST_ASSERT(server->Body(5) == L"fatal");
		TEST_ASSERT(server->Body(6) == L"fatal");
		TEST_ASSERT(server->Body(7) == L"fatal");
		TEST_ASSERT(server->SameBodyBytes(5, 6));
		TEST_ASSERT(server->SameBodyBytes(6, 7));
		TEST_ASSERT(server->AllTargetsStable());
		TEST_ASSERT(server->connectCount == 1);
		TEST_ASSERT(callback.localErrors >= 5);
		TEST_ASSERT(callback.fatalErrors == 1);
		TEST_ASSERT(callback.fatalBeforeDisconnected);
		TEST_ASSERT(callback.disconnectedCount == 1);
		TEST_ASSERT(fatalStopState->fatalReservedCount == 1);
		TEST_ASSERT(fatalStopState->fatalReleasedCount == 1);
		TEST_ASSERT(fatalStopState->stopStartedCount == 1);
		TEST_ASSERT(fatalStopState->externalStopReturns == 1);
		TEST_ASSERT(fatalStopState->externalStopErrors == 0);
		TEST_ASSERT(client->GetStatus() == ClientStatus::Disconnected);
		client->GetConnection()->Stop();
		TEST_ASSERT(callback.disconnectedCount == 1);
		server->Stop();
	});

	TEST_CASE(L"SocketHttp preserves non-ASCII WString values and a synchronous server reply")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		const vint port = 39602;
		const WString baseUrl = L"/VlppOSTestSocketHttpFocusedUnicode";
		const WString request = L"Client: \x4F60\x597D, \x4E16\x754C";
		const WString reply = L"Server: \x3053\x3093\x306B\x3061\x306F";
		ExactMessageCallback serverCallback(request, reply);
		auto server = Ptr(new SingleConnectionSocketHttpServer(baseUrl, port, &serverCallback));
		server->Start();
		ExactMessageCallback clientCallback(reply);
		auto client = CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port);
		client->GetConnection()->InstallCallback(&clientCallback);
		client->WaitForServer();
		client->GetConnection()->BeginReadingLoopUnsafe();
		client->GetConnection()->SendString(request);
		TEST_ASSERT(serverCallback.eventRead.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(clientCallback.eventRead.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(serverCallback.exact);
		TEST_ASSERT(clientCallback.exact);
		TEST_ASSERT(serverCallback.readCount == 1);
		TEST_ASSERT(clientCallback.readCount == 1);
		client->GetConnection()->Stop();
		server->Stop();
	});

	TEST_CASE(L"SocketHttp accepts the UTF-8 body limit and rejects one encoded byte beyond each FIFO")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		const vint port = 39603;
		const WString baseUrl = L"/VlppOSTestSocketHttpFocusedLimit";
		auto exact = RepeatSocketHttpCharacter(L'a', async_tcp_socket::HttpBodySizeLimit - 3) + L"\x4F60";
		auto oversized = exact + L"x";
		const wchar_t embeddedNulBuffer[] = { L'a', 0, L'b' };
		auto embeddedNul = WString::CopyFrom(embeddedNulBuffer, 3);
		const wchar_t invalidUnicodeBuffer[] = {
#if defined VCZH_MSVC
			(wchar_t)0xD800
#else
			(wchar_t)0x110000
#endif
		};
		auto invalidUnicode = WString::CopyFrom(invalidUnicodeBuffer, 1);
		TEST_ASSERT(wtou8(exact).Length() == async_tcp_socket::HttpBodySizeLimit);
		TEST_ASSERT(wtou8(oversized).Length() == async_tcp_socket::HttpBodySizeLimit + 1);

		ExactMessageCallback serverCallback(exact);
		auto server = Ptr(new SingleConnectionSocketHttpServer(baseUrl, port, &serverCallback));
		server->Start();
		ExactMessageCallback clientCallback(exact);
		auto client = CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port);
		client->GetConnection()->InstallCallback(&clientCallback);
		client->WaitForServer();
		client->GetConnection()->BeginReadingLoopUnsafe();
		TEST_ASSERT(serverCallback.eventInstalled.WaitForTime(SocketHttpFocusedTimeout));

		TEST_ERROR(client->GetConnection()->SendString(WString::Empty));
		TEST_ERROR(client->GetConnection()->SendString(embeddedNul));
		TEST_ERROR(client->GetConnection()->SendString(invalidUnicode));
		TEST_ERROR(client->GetConnection()->SendString(oversized));
		TEST_ASSERT(serverCallback.readCount == 0);
		client->GetConnection()->SendString(exact);
		TEST_ASSERT(serverCallback.eventRead.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(serverCallback.exact);
		TEST_ASSERT(serverCallback.readCount == 1);

		auto serverConnection = serverCallback.Connection();
		TEST_ASSERT(serverConnection != nullptr);
		TEST_ERROR(serverConnection->SendString(WString::Empty));
		TEST_ERROR(serverConnection->SendString(embeddedNul));
		TEST_ERROR(serverConnection->SendString(invalidUnicode));
		TEST_ERROR(serverConnection->SendString(oversized));
		TEST_ASSERT(clientCallback.readCount == 0);
		serverConnection->SendString(exact);
		TEST_ASSERT(clientCallback.eventRead.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(clientCallback.exact);
		TEST_ASSERT(clientCallback.readCount == 1);
		client->GetConnection()->Stop();
		server->Stop();
	});

	TEST_CASE(L"SocketHttp failed poll delivery is requeued before a replacement poll")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		const vint port = 39606;
		const WString baseUrl = L"/VlppOSTestSocketHttpFocusedRequeue";
		auto hookState = Ptr(new FailedPollHookState);
		SocketHttpPollHookScope pollHooks(
			Func<void(const WString&)>([hookState](const WString& token) { hookState->Claimed(token); }),
			Func<void(const WString&, bool)>([hookState](const WString& token, bool succeeded) { hookState->Completed(token, succeeded); })
			);
		ExactMessageCallback serverCallback(L"unused");
		auto server = Ptr(new SingleConnectionSocketHttpServer(baseUrl, port, &serverCallback));
		server->Start();

		auto connectClient = CreateFocusedSocketHttpApi<TNativeClient>(port);
		connectClient->WaitForServer();
		auto connectState = Ptr(new SocketHttpQueryState);
		SubmitSocketHttpQuery(connectClient, L"GET", baseUrl + HttpServerUrl_Connect, WString::Empty, SocketHttpFocusedTimeout, connectState);
		TEST_ASSERT(connectState->eventCompleted.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(!connectState->Failed());
		TEST_ASSERT(connectState->StatusCode() == 200);
		TEST_ASSERT(serverCallback.eventInstalled.WaitForTime(SocketHttpFocusedTimeout));
		auto connectBody = connectState->Body();
		auto separator = connectBody.IndexOf(L';');
		TEST_ASSERT(separator > 0 && separator + 1 < connectBody.Length());
		auto requestPath = connectBody.Left(separator);
		connectClient->Stop();

		const WString requeueMessage = L"requeue-\x4F60";
		Array<vuint8_t> requeueMessageBytes;
		TEST_ASSERT(async_tcp_socket::EncodeStrictUtf8(requeueMessage, requeueMessageBytes));
		serverCallback.Connection()->SendString(requeueMessage);
		auto firstPoll = CreateFocusedSocketHttpApi<TNativeClient>(port);
		firstPoll->WaitForServer();
		auto firstPollState = Ptr(new SocketHttpQueryState);
		SignalSocketHttpEventOnExit releaseClaim(hookState->eventReleaseClaim);
		SubmitSocketHttpQuery(firstPoll, L"POST", baseUrl + requestPath, WString::Empty, 0, firstPollState);
		TEST_ASSERT(hookState->eventClaimed.WaitForTime(SocketHttpFocusedTimeout));
		firstPoll->Stop();
		hookState->eventReleaseClaim.Signal();
		TEST_ASSERT(firstPollState->eventCompleted.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(firstPollState->Failed());
		TEST_ASSERT(hookState->eventFirstCompletion.WaitForTime(SocketHttpFocusedTimeout));

		auto replacementPoll = CreateFocusedSocketHttpApi<TNativeClient>(port);
		replacementPoll->WaitForServer();
		auto replacementState = Ptr(new SocketHttpQueryState);
		SubmitSocketHttpQuery(replacementPoll, L"POST", baseUrl + requestPath, WString::Empty, 0, replacementState);
		TEST_ASSERT(replacementState->eventCompleted.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(hookState->eventSecondCompletion.WaitForTime(SocketHttpFocusedTimeout));
		TEST_ASSERT(!replacementState->Failed());
		TEST_ASSERT(replacementState->StatusCode() == 200);
		TEST_ASSERT(replacementState->Body() == requeueMessage);
		TEST_ASSERT(replacementState->BodyBytesEqual(requeueMessageBytes));
		TEST_ASSERT(hookState->claimReleased);
		TEST_ASSERT(hookState->Validate());
		replacementPoll->Stop();
		server->Stop();
	});

	TEST_CASE(L"SocketHttp Stop is callback-reentrant for client and whole server")
	{
		SocketHttpListenerFactoryScope<TNativeServer> listenerFactory;
		{
			const vint port = 39604;
			const WString baseUrl = L"/VlppOSTestSocketHttpFocusedClientStop";
			ExactMessageCallback serverCallback(L"trigger-client-stop", L"stop-client");
			auto server = Ptr(new SingleConnectionSocketHttpServer(baseUrl, port, &serverCallback));
			server->Start();
			StopActionCallback clientCallback(L"stop-client");
			auto client = CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port);
			clientCallback.SetStopAction(Func<void()>([connection = client->GetConnection()]() { connection->Stop(); }));
			client->GetConnection()->InstallCallback(&clientCallback);
			client->WaitForServer();
			client->GetConnection()->BeginReadingLoopUnsafe();
			client->GetConnection()->SendString(L"trigger-client-stop");
			TEST_ASSERT(clientCallback.eventRead.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(clientCallback.eventDisconnected.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(clientCallback.exact);
			TEST_ASSERT(clientCallback.stopReturned);
			TEST_ASSERT(clientCallback.disconnectedCount == 1);
			TEST_ASSERT(client->GetStatus() == ClientStatus::Disconnected);
			server->Stop();
		}
		{
			const vint port = 39605;
			const WString baseUrl = L"/VlppOSTestSocketHttpFocusedServerStop";
			StopActionCallback serverCallback(L"trigger-server-stop");
			auto server = Ptr(new SingleConnectionSocketHttpServer(baseUrl, port, &serverCallback));
			serverCallback.SetStopAction(Func<void()>([&server]() { server->Stop(); }));
			server->Start();
			ExactMessageCallback clientCallback(L"unused");
			auto client = CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port);
			client->GetConnection()->InstallCallback(&clientCallback);
			client->WaitForServer();
			client->GetConnection()->BeginReadingLoopUnsafe();
			client->GetConnection()->SendString(L"trigger-server-stop");
			TEST_ASSERT(serverCallback.eventRead.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(serverCallback.eventDisconnected.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(serverCallback.exact);
			TEST_ASSERT(serverCallback.stopReturned);
			TEST_ASSERT(serverCallback.disconnectedCount == 1);
			TEST_ASSERT(server->IsStopped());
			client->GetConnection()->Stop();
		}
		{
			const vint port = 39610;
			const WString baseUrl = L"/VlppOSTestSocketHttpFocusedThrowingDisconnect";
			ExactMessageCallback serverCallback(L"unused");
			auto server = Ptr(new SingleConnectionSocketHttpServer(baseUrl, port, &serverCallback));
			server->Start();
			ThrowingDisconnectCallback clientCallback;
			auto client = CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port);
			client->GetConnection()->InstallCallback(&clientCallback);
			client->WaitForServer();
			client->GetConnection()->BeginReadingLoopUnsafe();
			TEST_ASSERT(serverCallback.eventInstalled.WaitForTime(SocketHttpFocusedTimeout));

			TEST_ERROR(client->GetConnection()->Stop());
			TEST_ASSERT(clientCallback.eventDisconnected.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(clientCallback.disconnectThrows == 1);
			TEST_ASSERT(clientCallback.disconnectedCount == 1);
			TEST_ASSERT(client->GetStatus() == ClientStatus::Disconnected);

			SocketHttpStopThreadScope threadScope;
			auto secondStopThread = Thread::CreateAndStart([connection = client->GetConnection(), &clientCallback]()
			{
				try
				{
					connection->Stop();
					clientCallback.secondStopReturns++;
				}
				catch (...)
				{
					clientCallback.secondStopErrors++;
				}
				clientCallback.eventSecondStopReturned.Signal();
			}, false);
			threadScope.SetFirst(secondStopThread);
			TEST_ASSERT(clientCallback.eventSecondStopReturned.WaitForTime(SocketHttpFocusedTimeout));
			threadScope.JoinAll();
			TEST_ASSERT(clientCallback.secondStopReturns == 1);
			TEST_ASSERT(clientCallback.secondStopErrors == 0);
			TEST_ASSERT(clientCallback.disconnectThrows == 1);
			TEST_ASSERT(clientCallback.disconnectedCount == 1);
			server->Stop();
		}
		{
			const vint port = 39608;
			const WString baseUrl = L"/VlppOSTestSocketHttpFocusedStopPollRace";
			auto state = Ptr(new SocketHttpStopRaceState);
			SocketHttpPollHookScope pollHooks(
				Func<void(const WString&)>([state](const WString&) { state->PollClaimed(); }),
				Func<void(const WString&, bool)>([state](const WString&, bool) { state->PollCompleted(); }),
				Func<void(const WString&)>([state](const WString&) { state->PollRegistered(); })
				);
			ExactMessageCallback serverCallback(L"unused");
			auto server = Ptr(new SocketHttpStopRaceServer(baseUrl, port, &serverCallback, state));
			server->Start();

			auto connectClient = CreateFocusedSocketHttpApi<TNativeClient>(port);
			connectClient->WaitForServer();
			auto connectState = Ptr(new SocketHttpQueryState);
			SubmitSocketHttpQuery(connectClient, L"GET", baseUrl + HttpServerUrl_Connect, WString::Empty, SocketHttpFocusedTimeout, connectState);
			TEST_ASSERT(connectState->eventCompleted.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(!connectState->Failed());
			TEST_ASSERT(connectState->StatusCode() == 200);
			TEST_ASSERT(serverCallback.eventInstalled.WaitForTime(SocketHttpFocusedTimeout));
			auto connectBody = connectState->Body();
			auto separator = connectBody.IndexOf(L';');
			TEST_ASSERT(separator > 0 && separator + 1 < connectBody.Length());
			auto requestPath = connectBody.Left(separator);
			connectClient->Stop();

			auto pollClient = CreateFocusedSocketHttpApi<TNativeClient>(port);
			pollClient->WaitForServer();
			auto pollState = Ptr(new SocketHttpQueryState);
			SocketHttpStopThreadScope threadScope(&state->eventReleasePoll);
			SubmitSocketHttpQuery(pollClient, L"POST", baseUrl + requestPath, WString::Empty, 0, pollState);
			TEST_ASSERT(state->eventPollRegistered.WaitForTime(SocketHttpFocusedTimeout));

			auto connection = serverCallback.Connection();
			auto sendThread = Thread::CreateAndStart([state, connection]()
			{
				try
				{
					connection->SendString(L"held-poll");
					state->sendReturns++;
				}
				catch (...)
				{
					state->stopErrors++;
				}
			}, false);
			threadScope.SetFirst(sendThread);
			TEST_ASSERT(state->eventPollClaimed.WaitForTime(SocketHttpFocusedTimeout));

			auto localStopThread = Thread::CreateAndStart([state, connection]()
			{
				try
				{
					connection->Stop();
					state->localStopReturns++;
				}
				catch (...)
				{
					state->stopErrors++;
				}
				state->eventLocalStopReturned.Signal();
			}, false);
			threadScope.SetSecond(localStopThread);
			TEST_ASSERT(serverCallback.eventDisconnected.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(serverCallback.disconnectedCount == 1);
			TEST_ASSERT(!state->eventLocalStopReturned.WaitForTime(0));

			auto wholeStopThread = Thread::CreateAndStart([state, server]()
			{
				try
				{
					server->Stop();
					state->wholeStopReturns++;
				}
				catch (...)
				{
					state->stopErrors++;
				}
				state->eventWholeStopReturned.Signal();
			}, false);
			threadScope.SetThird(wholeStopThread);
			TEST_ASSERT(state->eventWholeStopEntered.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(!state->eventWholeStopReturned.WaitForTime(0));

			state->eventReleasePoll.Signal();
			TEST_ASSERT(state->eventLocalStopReturned.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(state->eventWholeStopReturned.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(pollState->eventCompleted.WaitForTime(SocketHttpFocusedTimeout));
			threadScope.JoinAll();
			TEST_ASSERT(state->pollRegisteredCount == 1);
			TEST_ASSERT(state->pollClaimedCount == 1);
			TEST_ASSERT(state->pollReleasedCount == 1);
			TEST_ASSERT(state->pollCompletedCount == 1);
			TEST_ASSERT(pollState->callbackCount == 1);
			TEST_ASSERT(state->localStopReturns == 1);
			TEST_ASSERT(state->wholeStopReturns == 1);
			TEST_ASSERT(state->sendReturns == 1);
			TEST_ASSERT(state->stopErrors == 0);
			TEST_ASSERT(serverCallback.disconnectedCount == 1);
			TEST_ASSERT(server->IsStopped());
			pollClient->Stop();
		}
		{
			const vint port = 39609;
			const WString baseUrl = L"/VlppOSTestSocketHttpFocusedReentrantFollower";
			auto state = Ptr(new SocketHttpReentrantFollowerState);
			SocketHttpReentrantFollowerCallback firstServerCallback(state);
			SocketHttpReentrantFollowerCallback secondServerCallback(state);
			auto server = Ptr(new TwoConnectionSocketHttpServer(baseUrl, port, &firstServerCallback, &secondServerCallback));
			state->server = server.Obj();
			server->Start();

			ExactMessageCallback firstClientCallback(L"unused");
			auto firstClient = CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port);
			firstClient->GetConnection()->InstallCallback(&firstClientCallback);
			firstClient->WaitForServer();
			firstClient->GetConnection()->BeginReadingLoopUnsafe();
			TEST_ASSERT(firstServerCallback.eventInstalled.WaitForTime(SocketHttpFocusedTimeout));

			ExactMessageCallback secondClientCallback(L"unused");
			auto secondClient = CreateSocketHttpProtocolClient<TNativeClient>(baseUrl, port);
			secondClient->GetConnection()->InstallCallback(&secondClientCallback);
			secondClient->WaitForServer();
			secondClient->GetConnection()->BeginReadingLoopUnsafe();
			TEST_ASSERT(secondServerCallback.eventInstalled.WaitForTime(SocketHttpFocusedTimeout));

			SocketHttpStopThreadScope threadScope;
			auto outerStopThread = Thread::CreateAndStart([state, server]()
			{
				try
				{
					server->Stop();
					state->outerStopReturns++;
				}
				catch (...)
				{
					state->stopErrors++;
				}
				state->eventOuterStopReturned.Signal();
			}, false);
			threadScope.SetFirst(outerStopThread);
			TEST_ASSERT(state->eventNestedStopReturned.WaitForTime(SocketHttpFocusedTimeout));
			TEST_ASSERT(state->eventOuterStopReturned.WaitForTime(SocketHttpFocusedTimeout));
			threadScope.JoinAll();
			TEST_ASSERT(state->disconnectEntered == 2);
			TEST_ASSERT(state->disconnectCompleted == 2);
			TEST_ASSERT(state->nestedStopCalls == 1);
			TEST_ASSERT(state->nestedStopReturns == 1);
			TEST_ASSERT(state->outerStopReturns == 1);
			TEST_ASSERT(state->nestedSawOtherCompleted == 1);
			TEST_ASSERT(state->stopErrors == 0);
			TEST_ASSERT(firstServerCallback.disconnectedCount == 1);
			TEST_ASSERT(secondServerCallback.disconnectedCount == 1);
			TEST_ASSERT(server->IsStopped());
			firstClient->GetConnection()->Stop();
			secondClient->GetConnection()->Stop();
		}
	});
}

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
	RunAsyncSocketNetworkProtocolTestCases<
		async_tcp_socket::windows_socket::AsyncSocketServer,
		async_tcp_socket::windows_socket::AsyncSocketClient
	>(true);
	RunSocketHttpNetworkProtocolTestCases<
		async_tcp_socket::windows_socket::AsyncSocketServer,
		async_tcp_socket::windows_socket::AsyncSocketClient
	>();
	RunSocketHttpWindowsInteropTestCases<
		async_tcp_socket::windows_socket::AsyncSocketServer,
		async_tcp_socket::windows_socket::AsyncSocketClient
	>();
	RunSocketHttpFocusedTestCases<
		async_tcp_socket::windows_socket::AsyncSocketServer,
		async_tcp_socket::windows_socket::AsyncSocketClient
	>();

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
#elif defined VCZH_GCC && defined VCZH_APPLE
	RunAsyncSocketNetworkProtocolTestCases<
		async_tcp_socket::macos_socket::AsyncSocketServer,
		async_tcp_socket::macos_socket::AsyncSocketClient
	>(false);
	RunSocketHttpNetworkProtocolTestCases<
		async_tcp_socket::macos_socket::AsyncSocketServer,
		async_tcp_socket::macos_socket::AsyncSocketClient
	>();
	RunSocketHttpFocusedTestCases<
		async_tcp_socket::macos_socket::AsyncSocketServer,
		async_tcp_socket::macos_socket::AsyncSocketClient
	>();
#elif defined VCZH_GCC && !defined VCZH_APPLE
	RunAsyncSocketNetworkProtocolTestCases<
		async_tcp_socket::linux_socket::AsyncSocketServer,
		async_tcp_socket::linux_socket::AsyncSocketClient
	>(true);
	RunSocketHttpNetworkProtocolTestCases<
		async_tcp_socket::linux_socket::AsyncSocketServer,
		async_tcp_socket::linux_socket::AsyncSocketClient
	>();
	RunSocketHttpFocusedTestCases<
		async_tcp_socket::linux_socket::AsyncSocketServer,
		async_tcp_socket::linux_socket::AsyncSocketClient
	>();
#endif
}
