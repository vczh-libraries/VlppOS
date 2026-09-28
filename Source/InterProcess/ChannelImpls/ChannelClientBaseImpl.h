/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
***********************************************************************/

#ifndef VCZH_INTERPROCESS_CHANNELIMPLS_CHANNELCLIENTBASEIMPL
#define VCZH_INTERPROCESS_CHANNELIMPLS_CHANNELCLIENTBASEIMPL

#include "ChannelImpl.h"
#include "ChannelPackage.h"

namespace vl::inter_process
{
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
			using ClientIdList = typename Base::ClientIdList;

		private:
			NetworkProtocolChannelClientBase*				client = nullptr;

			void ValidatePackage(Nullable<vint> receiverClientId, const ClientIdList& blockedReceivers) override
			{
				auto currentClientId = client->GetClientId();
				CHECK_ERROR(currentClientId != -1, L"NetworkProtocolChannelClient::Channel needs to be connected before sending.");
				if (receiverClientId)
				{
					CHECK_ERROR(receiverClientId.Value() > 0, L"NetworkProtocolChannelClient::Channel needs a valid receiverClientId.");
					CHECK_ERROR(blockedReceivers.Count() == 0, L"NetworkProtocolChannelClient::Channel cannot block receivers when sending to a specified client.");
				}
				for (auto blockedReceiver : blockedReceivers)
				{
					CHECK_ERROR(blockedReceiver > 0 && blockedReceiver != currentClientId, L"NetworkProtocolChannelClient::Channel needs valid blockedReceivers.");
				}
			}

			bool WriteBatch(Nullable<vint> receiverClientId, const ClientIdList& blockedReceivers, const PackageList& batch) override
			{
				return client->SendBatch(receiverClientId, blockedReceivers, this->channelName, batch);
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

		virtual bool SendBatch(Nullable<vint> receiverClientId, const NetworkPackage::ClientIdList& blockedReceivers, const WString& channelName, const PackageList& batch) = 0;

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

}

#endif
