/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
***********************************************************************/

#ifndef VCZH_INTERPROCESS_CHANNELIMPLS_CHANNELCLIENTIMPL
#define VCZH_INTERPROCESS_CHANNELIMPLS_CHANNELCLIENTIMPL

#include "ChannelClientBaseImpl.h"
#include "../NetworkProtocol.h"
#include "ChannelPackage.h"

namespace vl::inter_process
{
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
		bool SendBatch(Nullable<vint> receiverClientId, const NetworkPackage::ClientIdList& blockedReceivers, const WString& channelName, const PackageList& batch) override
		{
			if (this->GetStatus() != ClientStatus::Connected)
			{
				return true;
			}

			CHECK_ERROR(npClient, L"NetworkProtocolChannelClient::SendBatch needs an established network connection.");
			WString messageBody;
			TSerialization::Serialize(this->context, batch, messageBody);
			npClient->GetConnection()->SendString(NetworkPackage::ToString(NetworkPackage::Create(std::move(receiverClientId), blockedReceivers, channelName, messageBody)));
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
			if (npClient && this->GetStatus() != ClientStatus::Disconnected)
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

}

#endif
