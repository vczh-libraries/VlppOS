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
#include <exception>
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
			Ptr<INetworkProtocolConnection>					connection;
			vint											clientId = -1;
			bool											accepted = false;
			bool											readyForBroadcast = false;

			Connection(NetworkProtocolChannelServer* _server)
				: server(_server)
			{
			}

			void OnReadString(const WString& str) override
			{
				server->OnNetworkReadString(this, str);
			}

			void OnReadError(const WString& error) override
			{
				server->OnNetworkReadError(error);
			}

			bool OnLocalError(const WString& error, bool fatal) override
			{
				// Before admission, let the raw transport apply its retry policy.
				// Afterwards, a failed delivery makes the channel unreliable.
				return fatal || accepted;
			}

			void OnConnected() override
			{
			}

			void OnDisconnected() override
			{
				server->OnNetworkDisconnected(this);
			}

			void OnInstalled(INetworkProtocolConnection* _connection) override
			{
				CHECK_ERROR(connection.Obj() == _connection, L"NetworkProtocolChannelServer was installed on an unexpected connection.");
			}
		};

		class StopBarrierGuard
		{
		private:
			inline static thread_local StopBarrierGuard*		currentStopBarrierGuard = nullptr;
			NetworkProtocolChannelServer*					server = nullptr;
			StopBarrierGuard*								previousStopBarrierGuard = nullptr;
			bool											deferStopCompletion = false;

		public:
			StopBarrierGuard(NetworkProtocolChannelServer* _server, bool _deferStopCompletion = false)
				: server(_server)
				, previousStopBarrierGuard(currentStopBarrierGuard)
				, deferStopCompletion(_deferStopCompletion)
			{
				currentStopBarrierGuard = this;
			}

			~StopBarrierGuard() noexcept
			{
				currentStopBarrierGuard = previousStopBarrierGuard;
				server->EndStopBarrier(deferStopCompletion);
			}

			static bool IsActiveFor(NetworkProtocolChannelServer* server)
			{
				for (auto guard = currentStopBarrierGuard; guard; guard = guard->previousStopBarrierGuard)
				{
					if (guard->server == server)
					{
						return true;
					}
				}
				return false;
			}
		};

		class StopCompletionGuard
		{
		private:
			inline static thread_local StopCompletionGuard*	currentStopCompletionGuard = nullptr;
			NetworkProtocolChannelServer*					server = nullptr;
			StopCompletionGuard*							previousStopCompletionGuard = nullptr;

		public:
			StopCompletionGuard(NetworkProtocolChannelServer* _server)
				: server(_server)
				, previousStopCompletionGuard(currentStopCompletionGuard)
			{
				currentStopCompletionGuard = this;
			}

			~StopCompletionGuard() noexcept
			{
				currentStopCompletionGuard = previousStopCompletionGuard;
			}

			static bool IsActiveFor(NetworkProtocolChannelServer* server)
			{
				for (auto guard = currentStopCompletionGuard; guard; guard = guard->previousStopCompletionGuard)
				{
					if (guard->server == server)
					{
						return true;
					}
				}
				return false;
			}
		};

		typename TSerialization::ContextType							context;
		// covers connections (including readyForBroadcast), localClients, localClientsAwaitingConnected, pendingConnections, clientChannels, nextClientId, activeStopBarriers, stopCompletionThread, started, broadcastingError, broadcastingErrorMessage, stopAfterBarriersDrained, stopCompleting, stopException and stopped
		SpinLock														lockConnections;
		collections::Dictionary<vint, Ptr<Connection>>					connections;
		collections::Dictionary<vint, Ptr<LocalChannelClient>>			localClients;
		collections::List<vint>											localClientsAwaitingConnected;
		collections::List<Ptr<Connection>>								pendingConnections;
		ClientChannelMap												clientChannels;
		vint															nextClientId = 1;
		vint															activeStopBarriers = 0;
		EventObject														eventStopCompleted;
		EventObject														eventStopCompletionThreadJoined;
		Thread*															stopCompletionThread = nullptr;
		bool															started = false;
		bool															broadcastingError = false;
		WString															broadcastingErrorMessage;
		bool															stopAfterBarriersDrained = false;
		bool															stopCompleting = false;
		std::exception_ptr												stopException;
		bool															stopped = false;

		void InitializeBarriers()
		{
			CHECK_ERROR(eventStopCompleted.CreateManualUnsignal(true), L"NetworkProtocolChannelServer failed to create the stop completion event.");
			CHECK_ERROR(eventStopCompletionThreadJoined.CreateManualUnsignal(true), L"NetworkProtocolChannelServer failed to create the stop completion-thread event.");
		}

		void BeginStopBarrierUnsafe()
		{
			activeStopBarriers++;
		}

		static void CompleteStopThreadProc(Thread*, void* argument)
		{
			auto server = static_cast<NetworkProtocolChannelServer*>(argument);
			server->CompleteStop();
		}

		void EndStopBarrier(bool deferStopCompletion) noexcept
		{
			bool completeStop = false;
			{
				SPIN_LOCK(lockConnections)
				{
					if (activeStopBarriers > 0)
					{
						activeStopBarriers--;
						if (activeStopBarriers == 0)
						{
							if (stopAfterBarriersDrained && !stopCompleting)
							{
								stopAfterBarriersDrained = false;
								stopCompleting = true;
								completeStop = true;
							}
						}
					}
				}
			}
			if (completeStop)
			{
				if (!deferStopCompletion)
				{
					CompleteStop();
				}
				else
				{
					SPIN_LOCK(lockConnections)
						{
							if (!eventStopCompletionThreadJoined.Unsignal())
							{
								std::terminate();
							}
							stopCompletionThread = Thread::CreateAndStart(&CompleteStopThreadProc, this, false);
							if (!stopCompletionThread)
							{
								// Completing on this raw callback stack can deadlock a transport
								// that drains callbacks in Stop, so scheduling failure is unrecoverable.
								std::terminate();
							}
						}
				}
			}
		}

		void JoinStopCompletionThread()
		{
			Thread* joiningThread = nullptr;
			{
				SPIN_LOCK(lockConnections)
					{
						joiningThread = stopCompletionThread;
						stopCompletionThread = nullptr;
					}
			}
			if (joiningThread)
			{
				CHECK_ERROR(joiningThread->Wait(), L"NetworkProtocolChannelServer failed to join the stop completion thread.");
				delete joiningThread;
				CHECK_ERROR(eventStopCompletionThreadJoined.Signal(), L"NetworkProtocolChannelServer failed to signal the stop completion-thread event.");
			}
			else
			{
				CHECK_ERROR(eventStopCompletionThreadJoined.Wait(), L"NetworkProtocolChannelServer failed to wait for the stop completion thread.");
			}
		}

		bool TryBeginNetworkProtocolCallback()
		{
			bool invokeCallback = false;
			{
				SPIN_LOCK(lockConnections)
				{
					if (!stopped)
					{
						BeginStopBarrierUnsafe();
						invokeCallback = true;
					}
				}
			}
			if (!invokeCallback)
			{
				return false;
			}
			return true;
		}

		void OnNetworkReadString(Connection* connection, const WString& str)
		{
			if (!TryBeginNetworkProtocolCallback())
			{
				return;
			}
			StopBarrierGuard stopBarrierGuard(this, true);
			OnReadString(connection, str);
		}

		void OnNetworkReadError(const WString& error)
		{
			if (!TryBeginNetworkProtocolCallback())
			{
				return;
			}
			StopBarrierGuard stopBarrierGuard(this, true);
			BroadcastError(error);
		}

		void OnNetworkDisconnected(Connection* connection)
		{
			if (!TryBeginNetworkProtocolCallback())
			{
				return;
			}
			StopBarrierGuard stopBarrierGuard(this, true);
			OnConnectionDisconnected(connection);
		}

		void CompleteStopCore()
		{
			collections::List<Ptr<Connection>> stoppingConnections;
			collections::List<Ptr<Connection>> stoppingPendingConnections;
			collections::List<vint> stoppingLocalClientIds;
			collections::List<Ptr<LocalChannelClient>> stoppingLocalClients;
			{
				SPIN_LOCK(lockConnections)
				{
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
					localClientsAwaitingConnected.Clear();
					pendingConnections.Clear();
					clientChannels.Clear();
				}
			}

			std::exception_ptr completionException;
			auto recordException = [&completionException]()
			{
				if (!completionException)
				{
					completionException = std::current_exception();
				}
			};
			for (vint i = 0; i < stoppingLocalClients.Count(); i++)
			{
				try
				{
					NotifyLocalClientDisconnected(stoppingLocalClients[i]);
				}
				catch (...)
				{
					recordException();
				}
				try
				{
					OnClientDisconnected(stoppingLocalClientIds[i]);
				}
				catch (...)
				{
					recordException();
				}
			}
			try
			{
				TServerBase::Stop();
			}
			catch (...)
			{
				recordException();
			}
			// Network connections are owned and stopped by TServerBase.
			for (auto&& connection : stoppingConnections)
			{
				try
				{
					OnClientDisconnected(connection->clientId);
				}
				catch (...)
				{
					recordException();
				}
			}
			if (completionException)
			{
				SPIN_LOCK(lockConnections)
				{
					stopException = completionException;
				}
			}
			eventStopCompleted.Signal();
		}

		void CompleteStop() noexcept
		{
			StopCompletionGuard stopCompletionGuard(this);
			try
			{
				CompleteStopCore();
			}
			catch (...)
			{
				auto completionException = std::current_exception();
				{
					SPIN_LOCK(lockConnections)
					{
						stopException = completionException;
					}
				}
				eventStopCompleted.Signal();
			}
		}

		void RethrowStopException()
		{
			std::exception_ptr completionException;
			{
				SPIN_LOCK(lockConnections)
				{
					completionException = stopException;
					stopException = nullptr;
				}
			}
			if (completionException)
			{
				std::rethrow_exception(completionException);
			}
		}

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
						if (!broadcastingError && !stopped)
						{
							assignedClientId = nextClientId++;
							BeginStopBarrierUnsafe();
						}
					}
				}
				if (assignedClientId == -1)
				{
					connection->connection->Stop();
					return;
				}
				StopBarrierGuard stopBarrierGuard(this);

				if (OnClientConnected(assignedClientId, availableChannels.Keys(), nullptr) == WaitForClientResult::Accept)
				{
					bool accepted = false;
					bool deliverFatal = false;
					WString fatalError;
					{
						SPIN_LOCK(lockConnections)
						{
							if (!broadcastingError && !stopped)
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
							else if (broadcastingError)
							{
								deliverFatal = true;
								fatalError = broadcastingErrorMessage;
							}
						}
					}
					if (accepted)
					{
						connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create(assignedClientId, WString::Empty, WString::Empty)));
						bool deliverFatalAfterConnected = false;
						WString fatalErrorAfterConnected;
						{
							SPIN_LOCK(lockConnections)
							{
								connection->readyForBroadcast = true;
								if (broadcastingError)
								{
									deliverFatalAfterConnected = true;
									fatalErrorAfterConnected = broadcastingErrorMessage;
								}
							}
						}
						if (deliverFatalAfterConnected)
						{
							try
							{
								connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create({}, WString::Unmanaged(ErrorChannel), fatalErrorAfterConnected)));
							}
							catch (...)
							{
							}
							Thread::Sleep(200);
							connection->connection->Stop();
						}
					}
					else
					{
						if (deliverFatal)
						{
							try
							{
								connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create({}, WString::Unmanaged(ErrorChannel), fatalError)));
							}
							catch (...)
							{
							}
							// Match BroadcastError's delivery grace for a client that
							// was application-accepted after its target snapshot.
							Thread::Sleep(200);
						}
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

		WaitForClientResult OnClientConnected(Ptr<INetworkProtocolConnection> connection) override
		{
			CHECK_ERROR(connection, L"NetworkProtocolChannelServer::OnClientConnected needs a valid connection.");
			if (!TryBeginNetworkProtocolCallback())
			{
				return WaitForClientResult::Reject;
			}
			StopBarrierGuard stopBarrierGuard(this, true);

			auto context = Ptr(new Connection(this));
			context->connection = connection;
			bool acceptConnection = false;
			{
				SPIN_LOCK(lockConnections)
				{
					if (started && !broadcastingError && !stopped)
					{
						pendingConnections.Add(context);
						acceptConnection = true;
					}
				}
			}
			if (!acceptConnection)
			{
				return WaitForClientResult::Reject;
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

		WaitForClientResult OnClientConnected(vint clientId, const typename IChannelClient<TPackage>::ChannelNameList& availableChannels, Ptr<IChannelClient<TPackage>> localClient) override
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
			InitializeBarriers();
		}

		template<typename TFirst, typename... TArgs>
			requires (!std::is_constructible_v<typename TSerialization::ContextType, TFirst&&>)
		NetworkProtocolChannelServer(TFirst&& first, TArgs&&... args)
			: TServerBase(std::forward<TFirst>(first), std::forward<TArgs>(args)...)
			, context()
		{
			InitializeBarriers();
		}

		template<typename... TArgs>
		NetworkProtocolChannelServer(const typename TSerialization::ContextType& _context, TArgs&&... args)
			: TServerBase(std::forward<TArgs>(args)...)
			, context(_context)
		{
			InitializeBarriers();
		}

		~NetworkProtocolChannelServer()
		{
			try
			{
				Stop();
			}
			catch (...)
			{
			}
		}

		vint ConnectLocalClient(Ptr<IChannelClient<TPackage>> localClient) override
		{
			CHECK_ERROR(localClient, L"NetworkProtocolChannelServer::ConnectLocalClient needs a valid localClient.");
			{
				SPIN_LOCK(lockConnections)
				{
					if (broadcastingError)
					{
						return -1;
					}
					CHECK_ERROR(started, L"NetworkProtocolChannelServer has not started.");
					CHECK_ERROR(!stopped, L"NetworkProtocolChannelServer has stopped.");
				}
			}

			auto networkProtocolClient = localClient.template Cast<LocalChannelClient>();
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
					if (!broadcastingError && !stopped)
					{
						assignedClientId = nextClientId++;
						BeginStopBarrierUnsafe();
					}
				}
			}
			if (assignedClientId == -1)
			{
				return -1;
			}
			StopBarrierGuard stopBarrierGuard(this);

			if (OnClientConnected(assignedClientId, channels.Keys(), localClient) == WaitForClientResult::Reject)
			{
				return -1;
			}

			if (!networkProtocolClient->ConnectLocalServer(this, assignedClientId))
			{
				return -1;
			}

			bool connected = false;
			bool deliverFatal = false;
			WString fatalError;
			{
				SPIN_LOCK(lockConnections)
				{
					if (!broadcastingError && !stopped)
					{
						localClients.Add(assignedClientId, networkProtocolClient);
						localClientsAwaitingConnected.Add(assignedClientId);
						for (auto&& channelName : channels.Keys())
						{
							clientChannels.Add(assignedClientId, channelName);
						}
						connected = true;
					}
					else if (broadcastingError)
					{
						deliverFatal = true;
						fatalError = broadcastingErrorMessage;
					}
				}
			}
			if (!connected)
			{
				if (deliverFatal)
				{
					try
					{
						networkProtocolClient->OnReadError(fatalError);
					}
					catch (...)
					{
						networkProtocolClient->NotifyDisconnected();
						throw;
					}
				}
				networkProtocolClient->NotifyDisconnected();
				return -1;
			}

			std::exception_ptr connectedException;
			try
			{
				networkProtocolClient->NotifyLocalConnected();
			}
			catch (...)
			{
				connectedException = std::current_exception();
			}
			bool deliverFatalAfterConnected = false;
			WString fatalErrorAfterConnected;
			{
				SPIN_LOCK(lockConnections)
				{
					localClientsAwaitingConnected.Remove(assignedClientId);
					if (broadcastingError)
					{
						deliverFatalAfterConnected = true;
						fatalErrorAfterConnected = broadcastingErrorMessage;
					}
				}
			}
			if (deliverFatalAfterConnected)
			{
				try
				{
					networkProtocolClient->OnReadError(fatalErrorAfterConnected);
				}
				catch (...)
				{
					if (!connectedException)
					{
						connectedException = std::current_exception();
					}
				}
			}
			if (connectedException)
			{
				std::rethrow_exception(connectedException);
			}
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
						localClientsAwaitingConnected.Remove(clientId);
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
			bool calledFromExistingStopBarrier = StopBarrierGuard::IsActiveFor(this);
			collections::List<Ptr<Connection>> targetConnections;
			collections::List<Ptr<LocalChannelClient>> targetLocalClients;
			WString broadcastMessage;
			{
				SPIN_LOCK(lockConnections)
				{
					if (broadcastingError || stopped)
					{
						return;
					}
					broadcastingError = true;
					broadcastingErrorMessage = errorMessage;
					broadcastMessage = broadcastingErrorMessage;
					for (auto&& connection : connections.Values())
					{
						if (connection->readyForBroadcast)
						{
							targetConnections.Add(connection);
						}
					}
					for (auto&& clientId : localClients.Keys())
					{
						if (!localClientsAwaitingConnected.Contains(clientId))
						{
							targetLocalClients.Add(localClients[clientId]);
						}
					}
					BeginStopBarrierUnsafe();
				}
			}

			std::exception_ptr deliveryException;
			{
				StopBarrierGuard stopBarrierGuard(this);
				for (auto&& connection : targetConnections)
				{
					try
					{
						connection->connection->SendString(NetworkPackage::ToString(NetworkPackage::Create({}, WString::Unmanaged(ErrorChannel), broadcastMessage)));
					}
					catch (...)
					{
						if (!deliveryException)
						{
							deliveryException = std::current_exception();
						}
					}
				}
				for (auto&& localClient : targetLocalClients)
				{
					try
					{
						localClient->OnReadError(broadcastMessage);
					}
					catch (...)
					{
						if (!deliveryException)
						{
							deliveryException = std::current_exception();
						}
					}
				}
				// Give transport clients a chance to consume the fatal package before closing.
				Thread::Sleep(200);
				try
				{
					Stop();
				}
				catch (...)
				{
					if (!deliveryException)
					{
						deliveryException = std::current_exception();
					}
				}
			}
			if (!calledFromExistingStopBarrier)
			{
				try
				{
					Stop();
				}
				catch (...)
				{
					if (!deliveryException)
					{
						deliveryException = std::current_exception();
					}
				}
			}
			if (deliveryException)
			{
				std::rethrow_exception(deliveryException);
			}
		}

		void Stop() override
		{
			if (StopCompletionGuard::IsActiveFor(this))
			{
				return;
			}
			bool calledFromStopBarrier = StopBarrierGuard::IsActiveFor(this);
			bool completeStop = false;
			bool waitForStop = false;
			{
				SPIN_LOCK(lockConnections)
				{
					if (!stopped)
					{
						started = false;
						stopped = true;
						CHECK_ERROR(eventStopCompleted.Unsignal(), L"NetworkProtocolChannelServer failed to unsignal the stop completion event.");
					}
					if (!stopCompleting)
					{
						if (activeStopBarriers == 0)
						{
							stopCompleting = true;
							completeStop = true;
						}
						else
						{
							stopAfterBarriersDrained = true;
						}
					}
					waitForStop = !completeStop && !calledFromStopBarrier;
				}
			}

			if (completeStop)
			{
				CompleteStop();
			}
			else if (waitForStop)
			{
				eventStopCompleted.Wait();
			}
			if (!calledFromStopBarrier)
			{
				JoinStopCompletionThread();
				RethrowStopException();
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
