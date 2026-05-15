/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
  INetworkProtocol<T>

***********************************************************************/

#ifndef VCZH_INTERPROCESS_TEXTNETWORKPROTOCOL
#define VCZH_INTERPROCESS_TEXTNETWORKPROTOCOL

#include "Channel.h"
#include "../Threading.h"

namespace vl::inter_process
{
	struct NetworkPackage
	{
		Nullable<vint>				clientId;
		WString						channelName;
		WString						messageBody;

		static inline NetworkPackage Create(Nullable<vint> _clientId, const WString& _channelName, const WString& _messageBody)
		{
			NetworkPackage package;
			package.clientId = std::move(_clientId);
			package.channelName = _channelName;
			package.messageBody = _messageBody;
			return package;
		}

		static inline WString ToString(const NetworkPackage& package)
		{
			return (package.clientId ? itow(package.clientId.Value()) : WString::Empty)
				+ L";" + package.channelName
				+ L";" + package.messageBody
				;
		}

		static inline void Parse(const WString& str, NetworkPackage& package)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::NetworkPackage::Parse(const WString&, NetworkPackage&)#"
			const wchar_t* reading = str.Buffer();

			const wchar_t* afterClientId = wcschr(reading, L';');
			CHECK_ERROR(afterClientId != nullptr, ERROR_MESSAGE_PREFIX L"Invalid package format.");
			if (afterClientId == reading)
			{
				package.clientId.Reset();
			}
			else
			{
				package.clientId = wtoi(str.Left((vint)(afterClientId - reading)));
			}

			const wchar_t* afterChannelName = wcschr(afterClientId + 1, L';');
			CHECK_ERROR(afterChannelName != nullptr, ERROR_MESSAGE_PREFIX L"Invalid package format.");
			package.channelName = str.Sub((vint)(afterClientId - reading + 1), (vint)(afterChannelName - afterClientId - 1));
			package.messageBody = str.Right(str.Length() - (vint)(afterChannelName - reading + 1));
#undef ERROR_MESSAGE_PREFIX
		}
	};

/***********************************************************************
INetworkProtocolServer
***********************************************************************/

	class INetworkProtocolConnection;

	/// <summary>
	/// Callbacks for network protocol events.
	/// Functions could be run in any thread, implementation should be thread-safe.
	/// </summary>
	class INetworkProtocolCallback : public virtual Interface
	{
	public:
		/// <summary>
		/// Called when a text message is received from the other side of the connection.
		/// </summary>
		/// <param name="str">The text message.</param>
		virtual void							OnReadString(const WString& str) = 0;

		/// <summary>
		/// Called when a localerror message is raised.
		/// </summary>
		/// <param name="error">The error message.</param>
		virtual void							OnReadError(const WString& error) = 0;

		/// <summary>
		/// Called when the connection becomes available.
		/// This function might not be called if <see cref="INetworkProtocolConnection::InstallCallback"/> is called after the connection is already established.
		/// </summary>
		virtual void							OnConnected() = 0;

		/// <summary>
		/// Called when the connection is lost.
		/// </summary>
		virtual void							OnDisconnected() = 0;

		/// <summary>
		/// Called when the callback is installed to a connection.
		/// </summary>
		/// <param name="connection"></param>
		virtual void							OnInstalled(INetworkProtocolConnection* connection) = 0;
	};

	/// <summary>
	/// Represents a network connection, exchanging messages between the server and one client.
	/// After the connection is lost, it won't reconnect, if the server supports reconnection, a new connection object will be created.
	/// One connection object can be obtained from the client.
	/// Multiple connection objects can be obtained from the server.
	/// </summary>
	class INetworkProtocolConnection : public virtual Interface
	{
	public:
		/// <summary>
		/// Install the callback to the connection.
		/// If multiple text messages are received before installing the callback, all text messages will be pushed to the callback right away.
		/// This function can only be called once to install a callback, and no uninstallation is supported.
		/// </summary>
		/// <param name="callback">The callback object. It should not be null.</param>
		virtual void							InstallCallback(INetworkProtocolCallback* callback) = 0;

		/// <summary>
		/// Start receiving messages asynchronously until the connection is lost.
		/// Some implementation may start receiving messages immediately after the connection is established.
		/// So there is no guarantee that messages should not arrive before calling this function.
		/// </summary>
		virtual void							BeginReadingLoopUnsafe() = 0;

		/// <summary>
		/// Send a text message to the other side of the connection.
		/// </summary>
		/// <param name="str">The text message to send.</param>
		virtual void							SendString(const WString& str) = 0;

		/// <summary>
		/// Stop the connection.
		/// </summary>
		virtual void							Stop() = 0;
	};

	/// <summary>
	/// Represents a client.
	/// </summary>
	class INetworkProtocolClient : public virtual Interface
	{
	public:
		/// <summary>
		/// Obtain the connection to the server.
		/// An valid object will always be returned, but before <see cref="WaitForServer"/>  finishing, using the connection is undefined behavior.
		/// </summary>
		/// <returns>The connection to the server.</returns>
		virtual INetworkProtocolConnection*		GetConnection() = 0;

		/// <summary>
		/// Block until the connection to the server is established.
		/// </summary>
		virtual void							WaitForServer() = 0;

		/// <summary>
		/// Returns the status of the client.
		/// </summary>
		/// <returns>The status of the client.</returns>
		virtual ClientStatus					GetStatus() = 0;
	};

	/// <summary>
	/// Represents a server.
	/// </summary>
	class INetworkProtocolServer : public virtual Interface
	{
	public:
		/// <summary>
		/// Block until a client connects to the server, returning the connection between the server and this client.
		/// </summary>
		/// <returns>The connection to the client.</returns>
		virtual INetworkProtocolConnection*		WaitForClient() = 0;

		/// <summary>
		/// Stop the server.
		/// </summary>
		virtual void							Stop() = 0;

		/// <summary>
		/// Test if the server has stopped.
		/// A stopped server could be caused by either calling <see cref="Stop"/> or the underlying mechanism failing.
		/// </summary>
		/// <returns>Returns true if the server has stopped, false otherwise.</returns>
		virtual bool							IsStopped() = 0;
	};

/***********************************************************************
Hooking IChannelServer/IChannelClient to INetworkProtocolServer/INetworkProtocolClient

The serialization contract is the same to the one described in ChannelSerialization.h
SourceType will be List<TPackage>
DestType will be WString

NetworkPackage will be used as text message parsing and formatting for INetworkProtocolConnection.
BatchWrite belongs to IChannel, meaning each channel sends its own batch messages in one NetworkPackage.
channelName will be either a system channel or a user defined channel.
messageBody represents a list of TPackage.
When sending from client to server, clientId means the target client.
  Empty means broadcasting.
When sending from server to client, clientId means the source client.
  Empty means there is no source client, for example, a message generated from the server.

When a client establishes a connection to the server, channel names will be sent to the server:
  clientId will be empty, it does not mean broadcasting.
  channelName will be empty.
  messageBody will be all available channel names joined by "!", as "!" cannot be part of the channel name anyway.
After the server receives the first message from a client, an client id will be sent to the client:
  clientId is the assigned client id, starting from 1.
  channelName will be empty.
  messageBody will be empty.
After the client receives the first message from the server, WaitForClient unblocks.
  This can be implemented using EventObject.
If the server stops in any reason before the client receiving the client id, WaitForClient should also unblock.

Later 
***********************************************************************/

/***********************************************************************
NetworkProtocolChannel
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannel : public Object, public virtual IChannel<TPackage>
	{
	};

/***********************************************************************
NetworkProtocolChannelClient
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannelClient : public Object, public virtual IChannelClient<TPackage>
	{
		static_assert(std::is_same_v<typename TSerialization::SourceType, collections::List<TPackage>>);
		static_assert(std::is_same_v<typename TSerialization::DestType, WString>);
	protected:
		using PackageList = typename TSerialization::SourceType;
		using ChannelMap = typename IChannelClient<TPackage>::ChannelMap;
		using ChannelNameList = typename IChannelClient<TPackage>::ChannelNameList;

		class Channel : public NetworkProtocolChannel<TPackage, TSerialization>
		{
		protected:
			NetworkProtocolChannelClient*					client = nullptr;
			WString											channelName;
			IChannelReader<TPackage>*						reader = nullptr;
			SpinLock										lockUnreadPackages;
			PackageList										unreadPackages;
			SpinLock										lockQueuedPackages;
			collections::Group<Nullable<vint>, TPackage>	queuedPackages;

		public:
			Channel(NetworkProtocolChannelClient* _client, const WString& _channelName)
				: client(_client)
				, channelName(_channelName)
			{
			}

			const WString& GetChannelName() override
			{
				return channelName;
			}

			IChannelReader<TPackage>* GetReader() override
			{
				return reader;
			}

			void Initialize(IChannelReader<TPackage>* _reader) override
			{
				CHECK_ERROR(_reader, L"NetworkProtocolChannelClient::Channel::Initialize needs a valid reader.");
				SPIN_LOCK(lockUnreadPackages)
				{
					reader = _reader;
					ReadBatch(unreadPackages);
					unreadPackages.Clear();
				}
			}

			void SendToClient(vint clientId, const TPackage& package) override
			{
				QueuePackage(clientId, package);
			}

			void BroadcastFromClient(const TPackage& package) override
			{
				QueuePackage({}, package);
			}

			void BatchWrite(bool& disconnected) override
			{
				collections::Group<Nullable<vint>, TPackage> packages;
				SPIN_LOCK(lockQueuedPackages)
				{
					packages = std::move(queuedPackages);
				}

				for (vint i = 0; i < packages.Count(); i++)
				{
					auto clientId = packages.Keys()[i];
					auto&& batch = packages.GetByIndex(i);
					if (client->SendBatch(clientId, channelName, batch))
					{
						disconnected = true;
						return;
					}
				}
			}

			void ReadBatch(const PackageList& batch)
			{
				if (reader)
				{
					for (auto&& package : batch)
					{
						reader->OnRead(package);
					}
				}
				else
				{
					SPIN_LOCK(lockUnreadPackages)
					{
						for (auto&& package : batch)
						{
							unreadPackages.Add(package);
						}
					}
				}
			}

		protected:
			void QueuePackage(Nullable<vint> clientId, const TPackage& package)
			{
				SPIN_LOCK(lockQueuedPackages)
				{
					queuedPackages.Add(clientId, package);
				}
			}
		};

		class Callback : public Object, public virtual INetworkProtocolCallback
		{
		protected:
			NetworkProtocolChannelClient*					client = nullptr;

		public:
			Callback(NetworkProtocolChannelClient* _client)
				: client(_client)
			{
			}

			void OnReadString(const WString& str) override
			{
				client->OnReadString(str);
			}

			void OnReadError(const WString& error) override
			{
				client->OnError(error);
				client->NotifyDisconnected();
			}

			void OnConnected() override
			{
			}

			void OnDisconnected() override
			{
				client->NotifyDisconnected();
			}

			void OnInstalled(INetworkProtocolConnection*) override
			{
			}
		};

		typename TSerialization::ContextType					context;
		Ptr<Callback>											callback;
		Ptr<INetworkProtocolClient>								npClient;
		SpinLock												lockStatus;
		EventObject												eventWaitForServer;
		ClientStatus											status = ClientStatus::Ready;
		bool													connectedNotified = false;
		vint													clientId = -1;
		ChannelMap												channels;
		collections::Dictionary<WString, Ptr<Channel>>			ownedChannels;

		static void ValidateChannelName(const WString& channelName)
		{
			CHECK_ERROR(channelName.Length() > 0, L"Channel name should not be empty.");
			CHECK_ERROR(wcschr(channelName.Buffer(), L'!') == nullptr, L"Channel name should not contain !.");
		}

		WString JoinChannelNames()
		{
			WString channelNames;
			for (auto&& channelName : OnGetChannelNames())
			{
				if (channelNames.Length() > 0)
				{
					channelNames += L"!";
				}
				channelNames += channelName;
			}
			return channelNames;
		}

		Channel* FindChannel(const WString& channelName)
		{
			vint index = ownedChannels.Keys().IndexOf(channelName);
			return index == -1 ? nullptr : ownedChannels.Values()[index].Obj();
		}

		void SetStatus(ClientStatus newStatus)
		{
			SPIN_LOCK(lockStatus)
			{
				status = newStatus;
			}
		}

		bool SendBatch(Nullable<vint> targetClientId, const WString& channelName, const PackageList& batch)
		{
			if (GetStatus() != ClientStatus::Connected)
			{
				return true;
			}

			WString messageBody;
			TSerialization::Serialize(context, batch, messageBody);
			npClient->GetConnection()->SendString(NetworkPackage::ToString(NetworkPackage::Create(std::move(targetClientId), channelName, messageBody)));
			return false;
		}

		void OnReadString(const WString& str)
		{
			NetworkPackage package;
			NetworkPackage::Parse(str, package);
			if (package.channelName == ErrorChannel)
			{
				OnError(package.messageBody);
				NotifyDisconnected();
				return;
			}
			else if (package.channelName == WString::Empty)
			{
				CHECK_ERROR(package.clientId, L"NetworkProtocolChannelClient received an invalid connection response.");
				{
					SPIN_LOCK(lockStatus)
					{
						clientId = package.clientId.Value();
						status = ClientStatus::Connected;
					}
				}
				eventWaitForServer.Signal();
				return;
			}

			auto channel = FindChannel(package.channelName);
			if (channel)
			{
				PackageList batch;
				TSerialization::Deserialize(context, package.messageBody, batch);
				channel->ReadBatch(batch);
			}
		}

		void NotifyDisconnected()
		{
			bool shouldNotify = false;
			SPIN_LOCK(lockStatus)
			{
				if (status != ClientStatus::Disconnected)
				{
					status = ClientStatus::Disconnected;
					shouldNotify = true;
				}
			}
			eventWaitForServer.Signal();
			if (shouldNotify)
			{
				OnDisconnected();
			}
		}

		void NotifyConnected()
		{
			bool shouldNotify = false;
			vint connectedClientId = -1;
			SPIN_LOCK(lockStatus)
			{
				if (status == ClientStatus::Connected && !connectedNotified)
				{
					connectedNotified = true;
					connectedClientId = clientId;
					shouldNotify = true;
				}
			}
			if (shouldNotify)
			{
				OnConnected(connectedClientId);
			}
		}

	public:
		void OnConnected(vint clientId) override
		{
			// default implementation does nothing
		}

		void OnDisconnected() override
		{
			// default implementation does nothing
		}

		void OnError(const WString& errorMessage) override
		{
			// default implementation does nothing
		}

		NetworkProtocolChannelClient(
			Ptr<INetworkProtocolClient> _npClient,
			const typename TSerialization::ContextType& _context = {}
		)
			: context(_context)
			, callback(Ptr(new Callback(this)))
			, npClient(_npClient)
		{
			CHECK_ERROR(npClient, L"NetworkProtocolChannelClient needs a valid INetworkProtocolClient.");
			CHECK_ERROR(eventWaitForServer.CreateManualUnsignal(false), L"NetworkProtocolChannelClient initialization failed on eventWaitForServer.");
			npClient->GetConnection()->InstallCallback(callback.Obj());
		}

		~NetworkProtocolChannelClient()
		{
			npClient->GetConnection()->Stop();
		}

		IChannel<TPackage>* CreateChannel(const WString& channelName)
		{
			ValidateChannelName(channelName);
			vint index = channels.Keys().IndexOf(channelName);
			if (index != -1)
			{
				return channels.Values()[index];
			}

			auto channel = Ptr(new Channel(this, channelName));
			ownedChannels.Add(channelName, channel);
			channels.Add(channelName, channel.Obj());
			return channel.Obj();
		}

		const ChannelNameList& OnGetChannelNames() override
		{
			return channels.Keys();
		}

		const ChannelMap& GetChannels() override
		{
			return channels;
		}

		vint GetClientId() override
		{
			vint result = -1;
			SPIN_LOCK(lockStatus)
			{
				result = clientId;
			}
			return result;
		}

		void WaitForServer() override
		{
			auto currentStatus = GetStatus();
			if (currentStatus == ClientStatus::Connected || currentStatus == ClientStatus::Disconnected)
			{
				NotifyConnected();
				return;
			}
			if (currentStatus == ClientStatus::WaitingForServer)
			{
				eventWaitForServer.Wait();
				NotifyConnected();
				return;
			}

			SetStatus(ClientStatus::WaitingForServer);
			npClient->WaitForServer();
			if (npClient->GetStatus() != ClientStatus::Connected)
			{
				NotifyDisconnected();
				return;
			}

			npClient->GetConnection()->SendString(NetworkPackage::ToString(NetworkPackage::Create({}, WString::Empty, JoinChannelNames())));
			npClient->GetConnection()->BeginReadingLoopUnsafe();
			eventWaitForServer.Wait();
			NotifyConnected();
		}

		ClientStatus GetStatus() override
		{
			ClientStatus result = ClientStatus::Disconnected;
			SPIN_LOCK(lockStatus)
			{
				result = status;
			}
			return result;
		}

		void BroadcastError(const WString& errorMessage) override
		{
			npClient->GetConnection()->SendString(NetworkPackage::ToString(NetworkPackage::Create({}, WString::Unmanaged(ErrorChannel), errorMessage)));
			NotifyDisconnected();
		}
	};

/***********************************************************************
NetworkProtocolChannelServer
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannelServer : public Object, public virtual IChannelServer<TPackage>
	{
		static_assert(std::is_same_v<typename TSerialization::SourceType, collections::List<TPackage>>);
		static_assert(std::is_same_v<typename TSerialization::DestType, WString>);
	protected:
		using PackageList = typename TSerialization::SourceType;
		using ChannelMap = typename IChannelClient<TPackage>::ChannelMap;
		using ChannelNameList = typename IChannelClient<TPackage>::ChannelNameList;
		using ClientChannelMap = typename IChannelServer<TPackage>::ClientChannelMap;
		using ClientIdList = typename IChannelServer<TPackage>::ClientIdList;

		struct QueuedPackage
		{
			Nullable<vint>									clientId;
			TPackage										package;
		};

		class Channel : public NetworkProtocolChannel<TPackage, TSerialization>
		{
		protected:
			NetworkProtocolChannelServer*					server = nullptr;
			WString											channelName;
			IChannelReader<TPackage>*						reader = nullptr;
			SpinLock										lockReader;
			PackageList										unreadPackages;
			SpinLock										lockQueuedPackages;
			collections::List<QueuedPackage>				queuedPackages;

		public:
			Channel(NetworkProtocolChannelServer* _server, const WString& _channelName)
				: server(_server)
				, channelName(_channelName)
			{
			}

			const WString& GetChannelName() override
			{
				return channelName;
			}

			IChannelReader<TPackage>* GetReader() override
			{
				IChannelReader<TPackage>* result = nullptr;
				SPIN_LOCK(lockReader)
				{
					result = reader;
				}
				return result;
			}

			void Initialize(IChannelReader<TPackage>* _reader) override
			{
				CHECK_ERROR(_reader, L"NetworkProtocolChannelServer::Channel::Initialize needs a valid reader.");
				SPIN_LOCK(lockReader)
				{
					CHECK_ERROR(!reader, L"NetworkProtocolChannelServer::Channel::Initialize can only be called once.");
					reader = _reader;
					for (auto&& package : unreadPackages)
					{
						reader->OnRead(package);
					}
					unreadPackages.Clear();
				}
			}

			void SendToClient(vint clientId, const TPackage& package) override
			{
				QueuePackage(clientId, package);
			}

			void BroadcastFromClient(const TPackage& package) override
			{
				QueuePackage({}, package);
			}

			void BatchWrite(bool& disconnected) override
			{
				collections::List<QueuedPackage> packages;
				SPIN_LOCK(lockQueuedPackages)
				{
					for (auto&& package : queuedPackages)
					{
						packages.Add(package);
					}
					queuedPackages.Clear();
				}

				while (packages.Count() > 0)
				{
					auto clientId = packages[0].clientId;
					PackageList batch;
					for (vint i = packages.Count() - 1; i >= 0; i--)
					{
						if (NetworkProtocolChannelServer::IsSameClientId(packages[i].clientId, clientId))
						{
							batch.Insert(0, packages[i].package);
							packages.RemoveAt(i);
						}
					}

					disconnected = server->SendBatch(clientId, {}, -1, channelName, batch) || disconnected;
				}
			}

			void ReadBatch(const PackageList& batch)
			{
				SPIN_LOCK(lockReader)
				{
					if (reader)
					{
						for (auto&& package : batch)
						{
							reader->OnRead(package);
						}
					}
					else
					{
						for (auto&& package : batch)
						{
							unreadPackages.Add(package);
						}
					}
				}
			}

		protected:
			void QueuePackage(Nullable<vint> clientId, const TPackage& package)
			{
				QueuedPackage queuedPackage;
				queuedPackage.clientId = std::move(clientId);
				queuedPackage.package = package;
				SPIN_LOCK(lockQueuedPackages)
				{
					queuedPackages.Add(std::move(queuedPackage));
				}
			}
		};

		class Connection : public Object, public virtual INetworkProtocolCallback
		{
		protected:
			NetworkProtocolChannelServer*					server = nullptr;

		public:
			INetworkProtocolConnection*						connection = nullptr;
			EventObject										eventConnected;
			vint											clientId = -1;
			bool											accepted = false;

			Connection(NetworkProtocolChannelServer* _server)
				: server(_server)
			{
				CHECK_ERROR(eventConnected.CreateManualUnsignal(false), L"NetworkProtocolChannelServer::Connection initialization failed on eventConnected.");
			}

			void OnReadString(const WString& str) override
			{
				server->OnReadString(this, str);
			}

			void OnReadError(const WString& error) override
			{
				server->BroadcastError(error);
			}

			void OnConnected() override
			{
			}

			void OnDisconnected() override
			{
				server->OnConnectionDisconnected(this);
			}

			void OnInstalled(INetworkProtocolConnection* _connection) override
			{
				connection = _connection;
			}
		};

		typename TSerialization::ContextType					context;
		Ptr<INetworkProtocolServer>								npServer;
		SpinLock												lockChannels;
		ChannelMap												channels;
		collections::Dictionary<WString, Ptr<Channel>>			ownedChannels;
		SpinLock												lockConnections;
		collections::Dictionary<vint, Ptr<Connection>>			connections;
		collections::List<Ptr<Connection>>						pendingConnections;
		ClientChannelMap										clientChannels;
		vint													nextClientId = 1;
		bool													stopped = false;

		static bool IsSameClientId(Nullable<vint> a, Nullable<vint> b)
		{
			if (a && b) return a.Value() == b.Value();
			return !a && !b;
		}

		static void ValidateChannelName(const WString& channelName)
		{
			CHECK_ERROR(channelName.Length() > 0, L"Channel name should not be empty.");
			CHECK_ERROR(wcschr(channelName.Buffer(), L'!') == nullptr, L"Channel name should not contain !.");
		}

		static void SplitChannelNames(const WString& joinedNames, ChannelMap& availableChannels)
		{
			const wchar_t* reading = joinedNames.Buffer();
			while (true)
			{
				auto delimiter = wcschr(reading, L'!');
				auto channelName =
					delimiter
					? WString::CopyFrom(reading, (vint)(delimiter - reading))
					: WString::CopyFrom(reading, (vint)wcslen(reading));
				if (channelName.Length() > 0)
				{
					ValidateChannelName(channelName);
					availableChannels.Add(channelName, nullptr);
				}

				if (!delimiter)
				{
					break;
				}
				reading = delimiter + 1;
			}
		}

		Channel* FindChannel(const WString& channelName)
		{
			vint index = ownedChannels.Keys().IndexOf(channelName);
			return index == -1 ? nullptr : ownedChannels.Values()[index].Obj();
		}

		Ptr<Connection> RemovePendingConnection(Connection* connection)
		{
			Ptr<Connection> pendingConnection;
			for (vint i = 0; i < pendingConnections.Count(); i++)
			{
				if (pendingConnections[i].Obj() == connection)
				{
					pendingConnection = pendingConnections[i];
					pendingConnections.RemoveAt(i);
					break;
				}
			}
			return pendingConnection;
		}

		void OnReadString(Connection* connection, const WString& str)
		{
			NetworkPackage package;
			NetworkPackage::Parse(str, package);
			if (package.channelName == ErrorChannel)
			{
				BroadcastError(package.messageBody);
				return;
			}

			if (connection->clientId == -1)
			{
				CHECK_ERROR(!package.clientId && package.channelName == WString::Empty, L"NetworkProtocolChannelServer received an invalid connection request.");
				ChannelMap availableChannels;
				SplitChannelNames(package.messageBody, availableChannels);

				Ptr<Connection> pendingConnection;
				vint assignedClientId = -1;
				{
					SPIN_LOCK(lockConnections)
					{
						pendingConnection = RemovePendingConnection(connection);
						CHECK_ERROR(pendingConnection, L"NetworkProtocolChannelServer failed to find a pending connection.");
						assignedClientId = nextClientId++;
					}
				}

				if (OnClientConnected(assignedClientId, availableChannels.Keys()))
				{
					{
						SPIN_LOCK(lockConnections)
						{
							connection->clientId = assignedClientId;
							connection->accepted = true;
							connections.Add(assignedClientId, pendingConnection);
							for (auto&& channelName : availableChannels.Keys())
							{
								clientChannels.Add(assignedClientId, channelName);
							}
						}
					}
					connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create(assignedClientId, WString::Empty, WString::Empty)));
				}
				else
				{
					connection->connection->Stop();
				}
				connection->eventConnected.Signal();
				return;
			}

			auto channel = FindChannel(package.channelName);
			if (!channel)
			{
				return;
			}
			{
				SPIN_LOCK(lockConnections)
				{
					if (!clientChannels.Contains(connection->clientId, package.channelName))
					{
						return;
					}
				}
			}

			PackageList batch;
			TSerialization::Deserialize(context, package.messageBody, batch);
			channel->ReadBatch(batch);

			Nullable<vint> sourceClientId = connection->clientId;
			SendBatch(package.clientId, sourceClientId, connection->clientId, package.channelName, batch);
		}

		void OnConnectionDisconnected(Connection* connection)
		{
			vint disconnectedClientId = -1;
			{
				SPIN_LOCK(lockConnections)
				{
					if (connection->clientId == -1)
					{
						RemovePendingConnection(connection);
					}
					else if (connections.Keys().Contains(connection->clientId))
					{
						disconnectedClientId = connection->clientId;
						connections.Remove(connection->clientId);
						clientChannels.Remove(connection->clientId);
					}
				}
			}

			connection->eventConnected.Signal();
			if (disconnectedClientId != -1)
			{
				OnClientDisconnected(disconnectedClientId);
			}
		}

		bool SendBatch(Nullable<vint> targetClientId, Nullable<vint> sourceClientId, vint excludedClientId, const WString& channelName, const PackageList& batch)
		{
			if (IsStopped())
			{
				return true;
			}

			WString messageBody;
			TSerialization::Serialize(context, batch, messageBody);
			if (targetClientId)
			{
				Ptr<Connection> connection;
				{
					SPIN_LOCK(lockConnections)
					{
						if (connections.Keys().Contains(targetClientId.Value()) && clientChannels.Contains(targetClientId.Value(), channelName))
						{
							connection = connections[targetClientId.Value()];
						}
					}
				}
				if (connection)
				{
					connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create(std::move(sourceClientId), channelName, messageBody)));
				}
			}
			else
			{
				collections::List<Ptr<Connection>> targetConnections;
				{
					SPIN_LOCK(lockConnections)
					{
						for (auto&& connection : connections.Values())
						{
							if (connection->clientId != excludedClientId && clientChannels.Contains(connection->clientId, channelName))
							{
								targetConnections.Add(connection);
							}
						}
					}
				}
				for (auto&& connection : targetConnections)
				{
					connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create(sourceClientId, channelName, messageBody)));
				}
			}
			return false;
		}

	public:

		bool OnClientConnected(vint clientId, const typename IChannelClient<TPackage>::ChannelNameList& availableChannels) override
		{
			// default implementation allows all clients to connect
			return true;
		}

		void OnClientDisconnected(vint clientId) override
		{
			// default implementation does nothing
		}

		NetworkProtocolChannelServer(
			Ptr<INetworkProtocolServer> _npServer,
			const typename TSerialization::ContextType& _context = {}
		)
			: context(_context)
			, npServer(_npServer)
		{
			CHECK_ERROR(npServer, L"NetworkProtocolChannelServer needs a valid INetworkProtocolServer.");
		}

		~NetworkProtocolChannelServer()
		{
			Stop();
		}

		IChannel<TPackage>* CreateChannel(const WString& channelName)
		{
			ValidateChannelName(channelName);
			vint index = channels.Keys().IndexOf(channelName);
			if (index != -1)
			{
				return channels.Values()[index];
			}

			auto channel = Ptr(new Channel(this, channelName));
			ownedChannels.Add(channelName, channel);
			channels.Add(channelName, channel.Obj());
			return channel.Obj();
		}

		bool ConnectLocalClient(Ptr<IChannelClient<TPackage>> localClient) override
		{
			return false;
		}

		bool IsLocalClient(vint clientId) override
		{
			return false;
		}

		vint WaitForClient() override
		{
			CHECK_ERROR(!IsStopped(), L"NetworkProtocolChannelServer has stopped.");
			auto connection = npServer->WaitForClient();
			auto context = Ptr(new Connection(this));
			{
				SPIN_LOCK(lockConnections)
				{
					pendingConnections.Add(context);
				}
			}
			connection->InstallCallback(context.Obj());
			connection->BeginReadingLoopUnsafe();
			context->eventConnected.Wait();
			CHECK_ERROR(context->accepted, L"NetworkProtocolChannelServer failed to accept the connection.");
			return context->clientId;
		}

		bool DisconnectClient(vint clientId) override
		{
			Ptr<Connection> connection;
			{
				SPIN_LOCK(lockConnections)
				{
					if (connections.Keys().Contains(clientId))
					{
						connection = connections[clientId];
						connections.Remove(clientId);
						clientChannels.Remove(clientId);
					}
				}
			}
			if (connection)
			{
				connection->connection->Stop();
				OnClientDisconnected(clientId);
				return true;
			}
			return false;
		}

		const ClientIdList& GetClientIds() override
		{
			return clientChannels.Keys();
		}

		const ClientChannelMap& GetClientChannels() override
		{
			return clientChannels;
		}

		void BroadcastError(const WString& errorMessage) override
		{
			collections::List<Ptr<Connection>> targetConnections;
			{
				SPIN_LOCK(lockConnections)
				{
					for (auto&& connection : connections.Values())
					{
						targetConnections.Add(connection);
					}
				}
			}

			for (auto&& connection : targetConnections)
			{
				connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create({}, WString::Unmanaged(ErrorChannel), errorMessage)));
			}
			Stop();
		}

		void Stop() override
		{
			collections::List<Ptr<Connection>> stoppingConnections;
			collections::List<Ptr<Connection>> stoppingPendingConnections;
			bool shouldStop = false;
			{
				SPIN_LOCK(lockConnections)
				{
					if (!stopped)
					{
						stopped = true;
						shouldStop = true;
						for (auto&& connection : connections.Values())
						{
							stoppingConnections.Add(connection);
						}
						for (auto&& connection : pendingConnections)
						{
							stoppingPendingConnections.Add(connection);
						}
						connections.Clear();
						pendingConnections.Clear();
						clientChannels.Clear();
					}
				}
			}

			if (shouldStop)
			{
				npServer->Stop();
				for (auto&& connection : stoppingPendingConnections)
				{
					connection->eventConnected.Signal();
				}
				for (auto&& connection : stoppingConnections)
				{
					OnClientDisconnected(connection->clientId);
				}
			}
		}

		bool IsStopped() override
		{
			bool result = false;
			SPIN_LOCK(lockConnections)
			{
				result = stopped;
			}
			return result || npServer->IsStopped();
		}
	};
}

#endif
