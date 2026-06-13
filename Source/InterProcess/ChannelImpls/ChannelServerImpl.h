/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
***********************************************************************/

#ifndef VCZH_INTERPROCESS_CHANNELIMPLS_CHANNELSERVERIMPL
#define VCZH_INTERPROCESS_CHANNELIMPLS_CHANNELSERVERIMPL

#include "LocalChannelClientImpl.h"
#include "../NetworkProtocol.h"
#include "ChannelPackage.h"
#include <utility>

namespace vl::inter_process
{
/***********************************************************************
NetworkProtocolChannelServer
***********************************************************************/

	template<typename TPackage, typename TSerialization, typename TServerBase>
	class NetworkProtocolChannelServer
		: public TServerBase
		, public virtual IChannelServer<TPackage>
		, public virtual INetworkProtocolLocalChannelServer<TPackage, TSerialization>
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

				if (OnClientConnected(assignedClientId, availableChannels.Keys(), nullptr) == WaitForClientResult::Accept)
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
			NetworkPackage::ClientIdList noBlockedReceivers;
			auto blockedReceivers = package.extraClientIds ? &package.extraClientIds.Value() : &noBlockedReceivers;
			if (package.clientId)
			{
				auto receiverClientId = package.clientId.Value();
				CHECK_ERROR(receiverClientId > 0 && ClientHasChannel(receiverClientId, package.channelName), L"NetworkProtocolChannelServer received a message to a client without the specified channel.");
			}
			else
			{
				for (auto blockedReceiver : *blockedReceivers)
				{
					CHECK_ERROR(blockedReceiver > 0 && ClientHasChannel(blockedReceiver, package.channelName), L"NetworkProtocolChannelServer received a message blocking a client without the specified channel.");
				}
			}

			PackageList batch;
			TSerialization::Deserialize(context, package.messageBody, batch);
			SendBatch(package.clientId, *blockedReceivers, connection->clientId, connection->clientId, package.channelName, batch);
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

		bool SendBatch(Nullable<vint> receiverClientId, const NetworkPackage::ClientIdList& blockedReceivers, vint senderClientId, vint excludedClientId, const WString& channelName, const PackageList& batch)
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
							if (connection->clientId != excludedClientId && !blockedReceivers.Contains(connection->clientId) && clientChannels.Contains(connection->clientId, channelName))
							{
								targetConnections.Add(connection);
							}
						}
						for (auto&& clientId : localClients.Keys())
						{
							if (clientId != excludedClientId && !blockedReceivers.Contains(clientId) && clientChannels.Contains(clientId, channelName))
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

		bool SendFromLocalClient(Nullable<vint> receiverClientId, const NetworkPackage::ClientIdList& blockedReceivers, vint senderClientId, const WString& channelName, const PackageList& batch) override
		{
			CHECK_ERROR(senderClientId > 0 && ClientHasChannel(senderClientId, channelName), L"NetworkProtocolChannelServer received a message from a local client without the specified channel.");
			if (receiverClientId)
			{
				CHECK_ERROR(receiverClientId.Value() > 0 && ClientHasChannel(receiverClientId.Value(), channelName), L"NetworkProtocolChannelServer received a message from a local client to a client without the specified channel.");
			}
			else
			{
				for (auto blockedReceiver : blockedReceivers)
				{
					CHECK_ERROR(blockedReceiver > 0 && ClientHasChannel(blockedReceiver, channelName), L"NetworkProtocolChannelServer received a message from a local client blocking a client without the specified channel.");
				}
			}
			return SendBatch(receiverClientId, blockedReceivers, senderClientId, senderClientId, channelName, batch);
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
			TServerBase::Start();
		}

		WaitForClientResult OnClientConnected(vint clientId, const typename IChannelClient<TPackage>::ChannelNameList& availableChannels, IChannelClient<TPackage>* localClient) override
		{
			// default implementation allows all clients to connect
			return WaitForClientResult::Accept;
		}

		void OnClientDisconnected(vint clientId) override
		{
			// default implementation does nothing
		}

		NetworkProtocolChannelServer()
			: TServerBase()
			, context()
		{
		}

		template<typename TFirst, typename... TArgs>
			requires (!std::is_constructible_v<typename TSerialization::ContextType, TFirst&&>)
		NetworkProtocolChannelServer(TFirst&& first, TArgs&&... args)
			: TServerBase(std::forward<TFirst>(first), std::forward<TArgs>(args)...)
			, context()
		{
		}

		template<typename... TArgs>
		NetworkProtocolChannelServer(const typename TSerialization::ContextType& _context, TArgs&&... args)
			: TServerBase(std::forward<TArgs>(args)...)
			, context(_context)
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

			if (OnClientConnected(assignedClientId, channels.Keys(), localClient.Obj()) == WaitForClientResult::Reject)
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
				for (vint i = 0; i < stoppingLocalClients.Count(); i++)
				{
					NotifyLocalClientDisconnected(stoppingLocalClients[i]);
					OnClientDisconnected(stoppingLocalClientIds[i]);
				}
			}

			TServerBase::Stop();

			if (shouldStop)
			{
				// Network connections are owned and stopped by TServerBase.
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
			return result || TServerBase::IsStopped();
		}
	};
}

#endif
