/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
  IChannelServer<T>

***********************************************************************/

#ifndef VCZH_INTERPROCESS_CHANNELSERVER
#define VCZH_INTERPROCESS_CHANNELSERVER

#include  "Channel.h"

namespace vl::inter_process
{

/***********************************************************************
Interface
***********************************************************************/

	constexpr const wchar_t* ErrorChannel = L"!Error";
	constexpr const wchar_t* SystemChannel = L"!System";
	constexpr vint AdminClientId = -32768;

	template<typename TPackage>
	class IChannelClient : public virtual Interface
	{
	public:
		using ChannelMap = collections::Dictionary<WString, IChannel<TPackage>*>;
		using ChannelNameList = typename ChannelMap::KeyContainer;

		virtual void						OnConnected(vint clientId) = 0;
		virtual void						OnDisconnected() = 0;
		virtual void						OnError(const WString& errorMessage) = 0;

		virtual const ChannelNameList&		GetChannelNames() = 0;
		virtual const ChannelMap&			GetChannels() = 0;
	};

	template<typename TPackage>
	class IChannelServer : public virtual Interface
	{
	public:
		using ClientChannelMap = collections::Group<vint, WString>;
		using ClientIdList = typename ClientChannelMap::KeyContainer;

		virtual bool						OnClientConnected(vint clientId, const IChannelClient<TPackage>::ChannelNameList& availableChannels) = 0;
		virtual void						OnClientDisconnected(vint clientId) = 0;

		virtual void						ConnectLocalClient(Ptr<IChannelClient<TPackage>> localClient) = 0;
		virtual bool						IsLocalClient(vint clientId) = 0;
		virtual bool						DisconnectClient(vint clientId) = 0;
		virtual const ClientIdList&			GetClientIds() = 0;
		virtual const ClientChannelMap&		GetClientChannels() = 0;
		virtual void						BroadcastError(const WString& errorMessage) = 0;
	};
}

#endif