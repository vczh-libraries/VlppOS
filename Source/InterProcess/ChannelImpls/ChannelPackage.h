/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
***********************************************************************/

#ifndef VCZH_INTERPROCESS_CHANNELIMPLS_CHANNELPACKAGE
#define VCZH_INTERPROCESS_CHANNELIMPLS_CHANNELPACKAGE

#include "../Channel.h"

namespace vl::inter_process
{
	struct NetworkPackage
	{
		using ClientIdList = collections::List<vint>;

		Nullable<vint>				clientId;
		Nullable<ClientIdList>		extraClientIds;
		WString						channelName;
		WString						messageBody;

		static NetworkPackage Create(Nullable<vint> _clientId, const WString& _channelName, const WString& _messageBody);
		static NetworkPackage Create(Nullable<vint> _clientId, const ClientIdList& _extraClientIds, const WString& _channelName, const WString& _messageBody);
		static WString ToString(const NetworkPackage& package);
		static void Parse(const WString& str, NetworkPackage& package);
	};
}

#endif
