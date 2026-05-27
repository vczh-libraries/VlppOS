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
		/// Called when an error message is received from the other side of the connection.
		/// </summary>
		/// <param name="error">The error message.</param>
		virtual void							OnReadError(const WString& error) = 0;

		/// <summary>
		/// Called when a local transport error occurs.
		/// </summary>
		/// <param name="error">The error message.</param>
		/// <param name="fatal">Indicates whether the connection should be disconnected after this callback.</param>
		virtual void							OnLocalError(const WString& error, bool fatal) = 0;

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
		/// Called when a client connects to the server.
		/// The server begins listening to client connections after <see cref="Start"/> is called.
		/// No callback happens before <see cref="Start"/> or after <see cref="Stop"/> is called.
		/// </summary>
		/// <param name="connection">A connection object representing the client.</param>
		/// <returns>Returns "Reject" to disconnect the client immediatelly.</returns>
		virtual WaitForClientResult				OnClientConnected(INetworkProtocolConnection* connection) = 0;

		/// <summary>
		/// Start the server.
		/// </summary>
		virtual void							Start() = 0;

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
  Channel messages delivered by the server always carry a source client id.

When a client establishes a connection to the server, channel names will be sent to the server:
  clientId will be empty, it does not mean broadcasting.
  channelName will be empty.
  messageBody will be all available channel names joined by "!", as "!" cannot be part of the channel name anyway.
After the server receives the first message from a client, an client id will be sent to the client:
  clientId is the assigned client id, starting from 1.
  channelName will be empty.
  messageBody will be empty.

Later 
***********************************************************************/

/***********************************************************************
NetworkProtocolChannel
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannel : public Object, public virtual IChannel<TPackage>
	{
		static_assert(std::is_same_v<typename TSerialization::SourceType, collections::List<TPackage>>);
		static_assert(std::is_same_v<typename TSerialization::DestType, WString>);
	protected:
		using PackageList = typename TSerialization::SourceType;
		using ChannelMap = collections::Dictionary<WString, IChannel<TPackage>*>;
		using ChannelNameList = typename ChannelMap::KeyContainer;

		struct UnreadPackage
		{
			vint								senderClientId = -1;
			TPackage							package;
		};

		struct QueuedPackage
		{
			vint								senderClientId = -1;
			Nullable<vint>						receiverClientId;
			TPackage							package;
		};

		WString									channelName;

		// covers reader and unreadPackages
		SpinLock								lockUnreadPackages;
		IChannelReader<TPackage>*				reader = nullptr;
		collections::List<UnreadPackage>		unreadPackages;

		// covers queuedPackages
		SpinLock								lockQueuedPackages;
		collections::List<QueuedPackage>		queuedPackages;

		virtual void ValidatePackage(vint senderClientId, Nullable<vint> receiverClientId) = 0;
		virtual bool WriteBatch(vint senderClientId, Nullable<vint> receiverClientId, const PackageList& batch) = 0;

	public:
		NetworkProtocolChannel(const WString& _channelName)
			: channelName(_channelName)
		{
		}

		static void ValidateChannelName(const WString& channelName)
		{
			CHECK_ERROR(channelName.Length() > 0, L"Channel name should not be empty.");
			CHECK_ERROR(wcschr(channelName.Buffer(), L'!') == nullptr, L"Channel name should not contain !.");
		}

		static WString JoinChannelNames(const ChannelNameList& channelNames)
		{
			WString joinedNames;
			for (auto&& channelName : channelNames)
			{
				if (joinedNames.Length() > 0)
				{
					joinedNames += L"!";
				}
				joinedNames += channelName;
			}
			return joinedNames;
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
			CHECK_ERROR(_reader, L"NetworkProtocolChannel::Initialize needs a valid reader.");
			SPIN_LOCK(lockUnreadPackages)
			{
				CHECK_ERROR(!reader, L"NetworkProtocolChannel::Initialize cannot be called more than once.");
				reader = _reader;
				for (auto&& package : unreadPackages)
				{
					reader->OnRead(package.senderClientId, package.package);
				}
				unreadPackages.Clear();
			}
		}

		void SendToClient(vint senderClientId, vint receiverClientId, const TPackage& package) override
		{
			ValidatePackage(senderClientId, receiverClientId);
			QueuePackage(senderClientId, receiverClientId, package);
		}

		void BroadcastFromClient(vint senderClientId, const TPackage& package) override
		{
			ValidatePackage(senderClientId, {});
			QueuePackage(senderClientId, {}, package);
		}

		void BatchWrite(bool& disconnected) override
		{
			collections::List<QueuedPackage> packages;
			SPIN_LOCK(lockQueuedPackages)
			{
				packages = std::move(queuedPackages);
			}

			while (packages.Count() > 0)
			{
				PackageList batch;
				auto senderClientId = packages[0].senderClientId;
				auto receiverClientId = packages[0].receiverClientId;
				for (vint i = 0; i < packages.Count();)
				{
					auto&& package = packages[i];
					if (package.senderClientId == senderClientId && package.receiverClientId == receiverClientId)
					{
						batch.Add(package.package);
						packages.RemoveAt(i);
					}
					else
					{
						i++;
					}
				}

				if (WriteBatch(senderClientId, receiverClientId, batch))
				{
					disconnected = true;
					return;
				}
			}
		}

		void ReadBatch(vint senderClientId, const PackageList& batch)
		{
			SPIN_LOCK(lockUnreadPackages)
			{
				if (!reader)
				{
					for (auto&& package : batch)
					{
						UnreadPackage unreadPackage;
						unreadPackage.senderClientId = senderClientId;
						unreadPackage.package = package;
						unreadPackages.Add(unreadPackage);
					}
					return;
				}
			}
			for (auto&& package : batch)
			{
				reader->OnRead(senderClientId, package);
			}
		}

	protected:
		void QueuePackage(vint senderClientId, Nullable<vint> receiverClientId, const TPackage& package)
		{
			SPIN_LOCK(lockQueuedPackages)
			{
				QueuedPackage queuedPackage;
				queuedPackage.senderClientId = senderClientId;
				queuedPackage.receiverClientId = receiverClientId;
				queuedPackage.package = package;
				queuedPackages.Add(queuedPackage);
			}
		}
	};

/***********************************************************************
NetworkProtocolChannelClientBase
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannelClientBase : public Object, public virtual IChannelClient<TPackage>
	{
	protected:
		using BaseChannel = NetworkProtocolChannel<TPackage, TSerialization>;
		using PackageList = typename TSerialization::SourceType;
		using ChannelMap = typename IChannelClient<TPackage>::ChannelMap;
		using ChannelNameList = typename IChannelClient<TPackage>::ChannelNameList;

	private:
		class Channel : public NetworkProtocolChannel<TPackage, TSerialization>
		{
			using Base = NetworkProtocolChannel<TPackage, TSerialization>;

		private:
			NetworkProtocolChannelClientBase*				client = nullptr;

			void ValidatePackage(vint senderClientId, Nullable<vint> receiverClientId) override
			{
				auto currentClientId = client->GetClientId();
				CHECK_ERROR(currentClientId != -1, L"NetworkProtocolChannelClient::Channel needs to be connected before sending.");
				CHECK_ERROR(senderClientId == currentClientId, L"NetworkProtocolChannelClient::Channel needs senderClientId to match the current client id.");
				if (receiverClientId)
				{
					CHECK_ERROR(receiverClientId.Value() > 0, L"NetworkProtocolChannelClient::Channel needs a valid receiverClientId.");
				}
			}

			bool WriteBatch(vint, Nullable<vint> receiverClientId, const PackageList& batch) override
			{
				return client->SendBatch(receiverClientId, this->channelName, batch);
			}

		public:
			Channel(NetworkProtocolChannelClientBase* _client, const WString& _channelName)
				: Base(_channelName)
				, client(_client)
			{
			}
		};

	protected:
		typename TSerialization::ContextType						context;

		// covers status, connectedNotified and clientId
		SpinLock													lockStatus;
		ClientStatus												status = ClientStatus::Ready;
		bool														connectedNotified = false;
		vint														clientId = -1;

	private:
		ChannelMap													channels;
		collections::Dictionary<WString, Ptr<Channel>>				ownedChannels;

	protected:
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

		void SetConnected(vint assignedClientId)
		{
			SPIN_LOCK(lockStatus)
			{
				clientId = assignedClientId;
				status = ClientStatus::Connected;
			}
		}

		bool TrySetConnected(vint assignedClientId)
		{
			bool connected = false;
			SPIN_LOCK(lockStatus)
			{
				if (status == ClientStatus::Ready || status == ClientStatus::WaitingForServer)
				{
					clientId = assignedClientId;
					status = ClientStatus::Connected;
					connected = true;
				}
			}
			return connected;
		}

		virtual bool SendBatch(Nullable<vint> receiverClientId, const WString& channelName, const PackageList& batch) = 0;

		void ReceiveBatch(const WString& channelName, vint senderClientId, const WString& messageBody)
		{
			auto channel = FindChannel(channelName);
			if (channel)
			{
				PackageList batch;
				TSerialization::Deserialize(context, messageBody, batch);
				channel->ReadBatch(senderClientId, batch);
			}
		}

		virtual void NotifyDisconnected()
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

		void OnReadError(const WString& errorMessage) override
		{
			// default implementation does nothing
		}

		void OnLocalError(const WString& errorMessage, bool fatal) override
		{
			// default implementation does nothing
		}

	protected:
		NetworkProtocolChannelClientBase(const typename TSerialization::ContextType& _context = {})
			: context(_context)
		{
		}

	private:
		IChannel<TPackage>* CreateChannel(const WString& channelName)
		{
			BaseChannel::ValidateChannelName(channelName);
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

	protected:
		void EnsureChannels(const ChannelNameList& channelNames)
		{
			for (auto&& channelName : channelNames)
			{
				CreateChannel(channelName);
			}
		}

	public:
		const ChannelNameList& OnGetChannelNames() override
		{
			return channels.Keys();
		}

		const ChannelMap& GetChannels() override
		{
			EnsureChannels(OnGetChannelNames());
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

		ClientStatus GetStatus() override
		{
			ClientStatus result = ClientStatus::Disconnected;
			SPIN_LOCK(lockStatus)
			{
				result = status;
			}
			return result;
		}
	};

/***********************************************************************
NetworkProtocolChannelClient
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannelClient : public NetworkProtocolChannelClientBase<TPackage, TSerialization>
	{
	protected:
		using Base = NetworkProtocolChannelClientBase<TPackage, TSerialization>;
		using BaseChannel = typename Base::BaseChannel;
		using PackageList = typename TSerialization::SourceType;

		class Callback : public Object, public virtual INetworkProtocolCallback
		{
		private:
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
				client->OnReadError(error);
				client->NotifyDisconnected();
			}

			void OnLocalError(const WString& error, bool fatal) override
			{
				client->OnLocalError(error, fatal);
				if (fatal)
				{
					client->NotifyDisconnected();
				}
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

	private:
		EventObject													eventWaitForServer;
		Ptr<Callback>												callback;
		Ptr<INetworkProtocolClient>									npClient;
		SpinLock													lockQueuedPackagesBeforeConnected;
		collections::List<NetworkPackage>							queuedPackagesBeforeConnected;

	protected:
		bool SendBatch(Nullable<vint> receiverClientId, const WString& channelName, const PackageList& batch) override
		{
			if (this->GetStatus() != ClientStatus::Connected)
			{
				return true;
			}

			CHECK_ERROR(npClient, L"NetworkProtocolChannelClient::SendBatch needs an established network connection.");
			WString messageBody;
			TSerialization::Serialize(this->context, batch, messageBody);
			npClient->GetConnection()->SendString(NetworkPackage::ToString(NetworkPackage::Create(std::move(receiverClientId), channelName, messageBody)));
			return false;
		}

	private:
		void OnReadChannelPackage(const NetworkPackage& package)
		{
			if (this->FindChannel(package.channelName))
			{
				CHECK_ERROR(package.clientId, L"NetworkProtocolChannelClient received a channel message without senderClientId.");
				auto senderClientId = package.clientId.Value();
				CHECK_ERROR(senderClientId > 0, L"NetworkProtocolChannelClient received an invalid senderClientId.");
				this->ReceiveBatch(package.channelName, senderClientId, package.messageBody);
			}
		}

		void OnReadString(const WString& str)
		{
			NetworkPackage package;
			NetworkPackage::Parse(str, package);
			if (package.channelName == ErrorChannel)
			{
				this->OnReadError(package.messageBody);
				NotifyDisconnected();
				return;
			}
			else if (package.channelName == WString::Empty)
			{
				CHECK_ERROR(package.clientId, L"NetworkProtocolChannelClient received an invalid connection response.");
				CHECK_ERROR(package.clientId.Value() > 0, L"NetworkProtocolChannelClient received an invalid client id.");
				this->SetConnected(package.clientId.Value());
				this->NotifyConnected();
				eventWaitForServer.Signal();
				collections::List<NetworkPackage> packages;
				SPIN_LOCK(lockQueuedPackagesBeforeConnected)
				{
					packages = std::move(queuedPackagesBeforeConnected);
				}
				for (auto&& queuedPackage : packages)
				{
					OnReadChannelPackage(queuedPackage);
				}
				return;
			}

			if (this->GetStatus() != ClientStatus::Connected)
			{
				SPIN_LOCK(lockQueuedPackagesBeforeConnected)
				{
					if (this->GetStatus() != ClientStatus::Connected)
					{
						queuedPackagesBeforeConnected.Add(std::move(package));
						return;
					}
				}
			}
			OnReadChannelPackage(package);
		}

	protected:
		void NotifyDisconnected() override
		{
			eventWaitForServer.Signal();
			Base::NotifyDisconnected();
		}

	protected:
		NetworkProtocolChannelClient(const typename TSerialization::ContextType& _context = {})
			: Base(_context)
		{
			CHECK_ERROR(eventWaitForServer.CreateManualUnsignal(false), L"NetworkProtocolChannelClient initialization failed on eventWaitForServer.");
		}

	public:
		NetworkProtocolChannelClient(
			Ptr<INetworkProtocolClient> _npClient,
			const typename TSerialization::ContextType& _context = {}
		)
			: Base(_context)
		{
			CHECK_ERROR(eventWaitForServer.CreateManualUnsignal(false), L"NetworkProtocolChannelClient initialization failed on eventWaitForServer.");
			CHECK_ERROR(_npClient, L"NetworkProtocolChannelClient needs a valid INetworkProtocolClient.");
			callback = Ptr(new Callback(this));
			npClient = _npClient;
			npClient->GetConnection()->InstallCallback(callback.Obj());
		}

		~NetworkProtocolChannelClient()
		{
			if (npClient)
			{
				npClient->GetConnection()->Stop();
			}
		}

		void WaitForServer() override
		{
			auto currentStatus = this->GetStatus();
			if (currentStatus == ClientStatus::Connected || currentStatus == ClientStatus::Disconnected)
			{
				this->NotifyConnected();
				return;
			}
			if (currentStatus == ClientStatus::WaitingForServer)
			{
				eventWaitForServer.Wait();
				this->NotifyConnected();
				return;
			}
			CHECK_ERROR(currentStatus == ClientStatus::Ready, L"NetworkProtocolChannelClient::WaitForServer found an unexpected client status.");

			this->SetStatus(ClientStatus::WaitingForServer);
			npClient->WaitForServer();
			if (npClient->GetStatus() != ClientStatus::Connected)
			{
				NotifyDisconnected();
				return;
			}

			auto&& channelNames = this->OnGetChannelNames();
			this->EnsureChannels(channelNames);
			npClient->GetConnection()->SendString(NetworkPackage::ToString(NetworkPackage::Create({}, WString::Empty, BaseChannel::JoinChannelNames(channelNames))));
			npClient->GetConnection()->BeginReadingLoopUnsafe();
			eventWaitForServer.Wait();
			this->NotifyConnected();
		}

		void BroadcastError(const WString& errorMessage) override
		{
			CHECK_ERROR(npClient, L"NetworkProtocolChannelClient::BroadcastError needs an established connection.");
			npClient->GetConnection()->SendString(NetworkPackage::ToString(NetworkPackage::Create({}, WString::Unmanaged(ErrorChannel), errorMessage)));
			NotifyDisconnected();
		}
	};

/***********************************************************************
NetworkProtocolLocalChannelClient
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannelServer;

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolLocalChannelClient : public NetworkProtocolChannelClientBase<TPackage, TSerialization>
	{
		friend class NetworkProtocolChannelServer<TPackage, TSerialization>;

	private:
		using Base = NetworkProtocolChannelClientBase<TPackage, TSerialization>;
		using PackageList = typename TSerialization::SourceType;

		NetworkProtocolChannelServer<TPackage, TSerialization>*		localServer = nullptr;

		bool ConnectLocalServer(NetworkProtocolChannelServer<TPackage, TSerialization>* server, vint assignedClientId)
		{
			CHECK_ERROR(server, L"NetworkProtocolLocalChannelClient::ConnectLocalServer needs a valid server.");
			localServer = server;

			bool connected = this->TrySetConnected(assignedClientId);
			if (!connected)
			{
				localServer = nullptr;
			}
			return connected;
		}

		void NotifyLocalConnected()
		{
			this->NotifyConnected();
		}

	protected:
		bool SendBatch(Nullable<vint> receiverClientId, const WString& channelName, const PackageList& batch) override
		{
			if (this->GetStatus() != ClientStatus::Connected)
			{
				return true;
			}

			if (localServer)
			{
				return localServer->SendFromLocalClient(receiverClientId, this->GetClientId(), channelName, batch);
			}
			return true;
		}

		void NotifyDisconnected() override
		{
			localServer = nullptr;
			Base::NotifyDisconnected();
		}

	public:
		NetworkProtocolLocalChannelClient(const typename TSerialization::ContextType& _context = {})
			: Base(_context)
		{
		}

		void WaitForServer() override
		{
		}

		void BroadcastError(const WString& errorMessage) override
		{
			if (localServer)
			{
				localServer->BroadcastError(errorMessage);
				return;
			}
			this->OnLocalError(errorMessage, true);
			this->NotifyDisconnected();
		}
	};

/***********************************************************************
NetworkProtocolChannelServer
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannelServer : public Object, public virtual IChannelServer<TPackage>, public virtual INetworkProtocolServer
	{
		friend class NetworkProtocolLocalChannelClient<TPackage, TSerialization>;

	private:
		using BaseChannel = NetworkProtocolChannel<TPackage, TSerialization>;
		using PackageList = typename TSerialization::SourceType;
		using ChannelMap = typename IChannelClient<TPackage>::ChannelMap;
		using ChannelNameList = typename IChannelClient<TPackage>::ChannelNameList;
		using ClientChannelMap = typename IChannelServer<TPackage>::ClientChannelMap;
		using ClientIdList = typename IChannelServer<TPackage>::ClientIdList;
		using LocalChannelClient = NetworkProtocolLocalChannelClient<TPackage, TSerialization>;

		class Connection : public Object, public virtual INetworkProtocolCallback
		{
		private:
			NetworkProtocolChannelServer*					server = nullptr;

		public:
			INetworkProtocolConnection*						connection = nullptr;
			vint											clientId = -1;
			bool											accepted = false;

			Connection(NetworkProtocolChannelServer* _server)
				: server(_server)
			{
			}

			void OnReadString(const WString& str) override
			{
				server->OnReadString(this, str);
			}

			void OnReadError(const WString& error) override
			{
				server->BroadcastError(error);
			}

			void OnLocalError(const WString& error, bool fatal) override
			{
				// Server-side transport errors are finalized by OnDisconnected.
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

		typename TSerialization::ContextType							context;
		// covers connections, localClients, pendingConnections, clientChannels, nextClientId, started and stopped
		SpinLock														lockConnections;
		collections::Dictionary<vint, Ptr<Connection>>					connections;
		collections::Dictionary<vint, Ptr<LocalChannelClient>>			localClients;
		collections::List<Ptr<Connection>>								pendingConnections;
		ClientChannelMap												clientChannels;
		vint															nextClientId = 1;
		bool															started = false;
		bool															stopped = false;

		bool ClientHasChannel(vint clientId, const WString& channelName)
		{
			bool result = false;
			SPIN_LOCK(lockConnections)
			{
				result = (connections.Keys().Contains(clientId) || localClients.Keys().Contains(clientId)) && clientChannels.Contains(clientId, channelName);
			}
			return result;
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
				BaseChannel::SplitChannelNames(package.messageBody, availableChannels);

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

				if (OnClientConnected(assignedClientId, availableChannels.Keys()) == WaitForClientResult::Accept)
				{
					bool accepted = false;
					{
						SPIN_LOCK(lockConnections)
						{
							if (!stopped)
							{
								connection->clientId = assignedClientId;
								connection->accepted = true;
								connections.Add(assignedClientId, pendingConnection);
								for (auto&& channelName : availableChannels.Keys())
								{
									clientChannels.Add(assignedClientId, channelName);
								}
								accepted = true;
							}
						}
					}
					if (accepted)
					{
						connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create(assignedClientId, WString::Empty, WString::Empty)));
					}
					else
					{
						connection->connection->Stop();
					}
				}
				else
				{
					connection->connection->Stop();
				}
				return;
			}

			CHECK_ERROR(ClientHasChannel(connection->clientId, package.channelName), L"NetworkProtocolChannelServer received a message from a client without the specified channel.");
			if (package.clientId)
			{
				auto receiverClientId = package.clientId.Value();
				CHECK_ERROR(receiverClientId > 0 && ClientHasChannel(receiverClientId, package.channelName), L"NetworkProtocolChannelServer received a message to a client without the specified channel.");
			}

			PackageList batch;
			TSerialization::Deserialize(context, package.messageBody, batch);
			SendBatch(package.clientId, connection->clientId, connection->clientId, package.channelName, batch);
		}

		void OnConnectionDisconnected(Connection* connection)
		{
			Ptr<Connection> disconnectedConnection;
			vint disconnectedClientId = -1;
			{
				SPIN_LOCK(lockConnections)
				{
					if (connection->clientId == -1)
					{
						disconnectedConnection = RemovePendingConnection(connection);
					}
					else if (connections.Keys().Contains(connection->clientId))
					{
						disconnectedClientId = connection->clientId;
						disconnectedConnection = connections[connection->clientId];
						connections.Remove(connection->clientId);
						clientChannels.Remove(connection->clientId);
					}
				}
			}

			if (disconnectedClientId != -1)
			{
				OnClientDisconnected(disconnectedClientId);
			}
		}

		void NotifyLocalClientDisconnected(Ptr<LocalChannelClient> localClient)
		{
			localClient->NotifyDisconnected();
		}

		void DeliverBatchToLocalClient(Ptr<LocalChannelClient> localClient, vint senderClientId, const WString& channelName, const PackageList& batch)
		{
			auto&& channels = localClient->GetChannels();
			auto index = channels.Keys().IndexOf(channelName);
			CHECK_ERROR(index != -1, L"NetworkProtocolChannelServer failed to find a local channel.");

			auto channel = channels.Values()[index];
			if (auto networkChannel = dynamic_cast<BaseChannel*>(channel))
			{
				networkChannel->ReadBatch(senderClientId, batch);
			}
			else
			{
				auto reader = channel->GetReader();
				CHECK_ERROR(reader, L"NetworkProtocolChannelServer needs a readable local channel.");
				for (auto&& package : batch)
				{
					reader->OnRead(senderClientId, package);
				}
			}
		}

		bool SendBatch(Nullable<vint> receiverClientId, vint senderClientId, vint excludedClientId, const WString& channelName, const PackageList& batch)
		{
			if (IsStopped())
			{
				return true;
			}

			WString messageBody;
			TSerialization::Serialize(context, batch, messageBody);
			if (receiverClientId)
			{
				Ptr<Connection> connection;
				Ptr<LocalChannelClient> localClient;
				{
					SPIN_LOCK(lockConnections)
					{
						if (connections.Keys().Contains(receiverClientId.Value()) && clientChannels.Contains(receiverClientId.Value(), channelName))
						{
							connection = connections[receiverClientId.Value()];
						}
						else if (localClients.Keys().Contains(receiverClientId.Value()) && clientChannels.Contains(receiverClientId.Value(), channelName))
						{
							localClient = localClients[receiverClientId.Value()];
						}
					}
				}
				if (connection)
				{
					connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create(senderClientId, channelName, messageBody)));
				}
				if (localClient)
				{
					DeliverBatchToLocalClient(localClient, senderClientId, channelName, batch);
				}
			}
			else
			{
				collections::List<Ptr<Connection>> targetConnections;
				collections::List<Ptr<LocalChannelClient>> targetLocalClients;
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
						for (auto&& clientId : localClients.Keys())
						{
							if (clientId != excludedClientId && clientChannels.Contains(clientId, channelName))
							{
								targetLocalClients.Add(localClients[clientId]);
							}
						}
					}
				}
				for (auto&& connection : targetConnections)
				{
					connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create(senderClientId, channelName, messageBody)));
				}
				for (auto&& localClient : targetLocalClients)
				{
					DeliverBatchToLocalClient(localClient, senderClientId, channelName, batch);
				}
			}
			return false;
		}

		bool SendFromLocalClient(Nullable<vint> receiverClientId, vint senderClientId, const WString& channelName, const PackageList& batch)
		{
			CHECK_ERROR(senderClientId > 0 && ClientHasChannel(senderClientId, channelName), L"NetworkProtocolChannelServer received a message from a local client without the specified channel.");
			if (receiverClientId)
			{
				CHECK_ERROR(receiverClientId.Value() > 0 && ClientHasChannel(receiverClientId.Value(), channelName), L"NetworkProtocolChannelServer received a message from a local client to a client without the specified channel.");
			}
			return SendBatch(receiverClientId, senderClientId, senderClientId, channelName, batch);
		}

	public:

		WaitForClientResult OnClientConnected(INetworkProtocolConnection* connection) override
		{
			CHECK_ERROR(connection, L"NetworkProtocolChannelServer::OnClientConnected needs a valid connection.");

			auto context = Ptr(new Connection(this));
			context->connection = connection;
			{
				SPIN_LOCK(lockConnections)
				{
					if (!started || stopped)
					{
						return WaitForClientResult::Reject;
					}
					pendingConnections.Add(context);
				}
			}
			connection->InstallCallback(context.Obj());
			connection->BeginReadingLoopUnsafe();
			return WaitForClientResult::Accept;
		}

		void Start() override
		{
			SPIN_LOCK(lockConnections)
			{
				CHECK_ERROR(!stopped, L"NetworkProtocolChannelServer has stopped.");
				started = true;
			}
		}

		WaitForClientResult OnClientConnected(vint clientId, const typename IChannelClient<TPackage>::ChannelNameList& availableChannels) override
		{
			// default implementation allows all clients to connect
			return WaitForClientResult::Accept;
		}

		void OnClientDisconnected(vint clientId) override
		{
			// default implementation does nothing
		}

		NetworkProtocolChannelServer(
			const typename TSerialization::ContextType& _context = {}
		)
			: context(_context)
		{
		}

		~NetworkProtocolChannelServer()
		{
			Stop();
		}

		vint ConnectLocalClient(Ptr<IChannelClient<TPackage>> localClient) override
		{
			CHECK_ERROR(localClient, L"NetworkProtocolChannelServer::ConnectLocalClient needs a valid localClient.");
			{
				SPIN_LOCK(lockConnections)
				{
					CHECK_ERROR(started, L"NetworkProtocolChannelServer has not started.");
					CHECK_ERROR(!stopped, L"NetworkProtocolChannelServer has stopped.");
				}
			}

			auto networkProtocolClient = localClient.Cast<LocalChannelClient>();
			CHECK_ERROR(networkProtocolClient, L"NetworkProtocolChannelServer::ConnectLocalClient needs a NetworkProtocolLocalChannelClient.");

			if (networkProtocolClient->GetStatus() == ClientStatus::Connected || networkProtocolClient->GetStatus() == ClientStatus::Disconnected)
			{
				return -1;
			}

			auto&& channels = networkProtocolClient->GetChannels();
			for (auto&& channelName : channels.Keys())
			{
				BaseChannel::ValidateChannelName(channelName);
				auto index = channels.Keys().IndexOf(channelName);
				CHECK_ERROR(channels.Values()[index], L"NetworkProtocolChannelServer::ConnectLocalClient needs valid local channels.");
			}

			vint assignedClientId = -1;
			{
				SPIN_LOCK(lockConnections)
				{
					assignedClientId = nextClientId++;
				}
			}

			if (OnClientConnected(assignedClientId, channels.Keys()) == WaitForClientResult::Reject)
			{
				return -1;
			}

			if (!networkProtocolClient->ConnectLocalServer(this, assignedClientId))
			{
				return -1;
			}

			bool connected = false;
			{
				SPIN_LOCK(lockConnections)
				{
					if (!stopped)
					{
						localClients.Add(assignedClientId, networkProtocolClient);
						for (auto&& channelName : channels.Keys())
						{
							clientChannels.Add(assignedClientId, channelName);
						}
						connected = true;
					}
				}
			}
			if (!connected)
			{
				networkProtocolClient->NotifyDisconnected();
				return -1;
			}

			networkProtocolClient->NotifyLocalConnected();
			return assignedClientId;
		}

		bool IsLocalClient(vint clientId) override
		{
			bool result = false;
			SPIN_LOCK(lockConnections)
			{
				result = localClients.Keys().Contains(clientId);
			}
			return result;
		}

		bool DisconnectClient(vint clientId) override
		{
			Ptr<Connection> connection;
			Ptr<LocalChannelClient> localClient;
			{
				SPIN_LOCK(lockConnections)
				{
					if (connections.Keys().Contains(clientId))
					{
						connection = connections[clientId];
						connections.Remove(clientId);
						clientChannels.Remove(clientId);
					}
					else if (localClients.Keys().Contains(clientId))
					{
						localClient = localClients[clientId];
						localClients.Remove(clientId);
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
			if (localClient)
			{
				NotifyLocalClientDisconnected(localClient);
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
			collections::List<Ptr<LocalChannelClient>> targetLocalClients;
			{
				SPIN_LOCK(lockConnections)
				{
					for (auto&& connection : connections.Values())
					{
						targetConnections.Add(connection);
					}
					for (auto&& localClient : localClients.Values())
					{
						targetLocalClients.Add(localClient);
					}
				}
			}

			for (auto&& connection : targetConnections)
			{
				connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create({}, WString::Unmanaged(ErrorChannel), errorMessage)));
			}
			for (auto&& localClient : targetLocalClients)
			{
				localClient->OnReadError(errorMessage);
			}
			// Give transport clients a chance to consume the fatal package before closing.
			Thread::Sleep(200);
			Stop();
		}

		void Stop() override
		{
			collections::List<Ptr<Connection>> stoppingConnections;
			collections::List<Ptr<Connection>> stoppingPendingConnections;
			collections::List<vint> stoppingLocalClientIds;
			collections::List<Ptr<LocalChannelClient>> stoppingLocalClients;
			bool shouldStop = false;
			{
				SPIN_LOCK(lockConnections)
				{
					if (!stopped)
					{
						started = false;
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
						for (auto&& clientId : localClients.Keys())
						{
							stoppingLocalClientIds.Add(clientId);
							stoppingLocalClients.Add(localClients[clientId]);
						}
						connections.Clear();
						localClients.Clear();
						pendingConnections.Clear();
						clientChannels.Clear();
					}
				}
			}

			if (shouldStop)
			{
				for (auto&& connection : stoppingPendingConnections)
				{
					connection->connection->Stop();
				}
				for (auto&& connection : stoppingConnections)
				{
					connection->connection->Stop();
					OnClientDisconnected(connection->clientId);
				}
				for (vint i = 0; i < stoppingLocalClients.Count(); i++)
				{
					NotifyLocalClientDisconnected(stoppingLocalClients[i]);
					OnClientDisconnected(stoppingLocalClientIds[i]);
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
			return result;
		}
	};
}

#endif
