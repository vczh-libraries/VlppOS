/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
  INetworkProtocol<T>

***********************************************************************/

#ifndef VCZH_INTERPROCESS_TEXTNETWORKPROTOCOL
#define VCZH_INTERPROCESS_TEXTNETWORKPROTOCOL

#include "ChannelServer.h"

namespace vl::inter_process
{
	class INetworkProtocolCallback : public virtual Interface
	{
	public:
		virtual void				OnReadStringThreadUnsafe(const WString& channelName, const WString& str) = 0;
		virtual void				OnReadStoppedThreadUnsafe() = 0;
	};

	class INetworkProtocolCoreCallback : public virtual INetworkProtocolCallback
	{
	public:
		virtual void				OnReconnectedUnsafe() = 0;
	};

	class INetworkProtocol : public virtual Interface
	{
	public:
		virtual void				InstallCallback(INetworkProtocolCallback* callback) = 0;
		virtual void				BeginReadingLoopUnsafe() = 0;
		virtual void				SendString(Nullable<vint> clientId, const WString& channelName, const WString& messageBody) = 0;
	};
}

#endif