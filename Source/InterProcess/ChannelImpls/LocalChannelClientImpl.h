/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
***********************************************************************/

#ifndef VCZH_INTERPROCESS_CHANNELIMPLS_LOCALCHANNELCLIENTIMPL
#define VCZH_INTERPROCESS_CHANNELIMPLS_LOCALCHANNELCLIENTIMPL

#include "ChannelClientBaseImpl.h"
#include "../NetworkProtocol.h"
#include "ChannelPackage.h"

namespace vl::inter_process
{
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
		bool SendBatch(Nullable<vint> receiverClientId, const NetworkPackage::ClientIdList& blockedReceivers, const WString& channelName, const PackageList& batch) override
		{
			if (this->GetStatus() != ClientStatus::Connected)
			{
				return true;
			}

			if (localServer)
			{
				return localServer->SendFromLocalClient(receiverClientId, blockedReceivers, this->GetClientId(), channelName, batch);
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

}

#endif
