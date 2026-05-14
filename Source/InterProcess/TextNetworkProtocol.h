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

	class INetworkProtocolConnection;

	class INetworkProtocolCallback : public virtual Interface
	{
	public:
		virtual void							OnReadString(const WString& str) = 0;
		virtual void							OnReadError(const WString& error) = 0;
		virtual void							OnConnected() = 0;
		virtual void							OnDisconnected() = 0;
		virtual void							OnInstalled(INetworkProtocolConnection* connection) = 0;
	};

	class INetworkProtocolConnection : public virtual Interface
	{
	public:
		virtual void							InstallCallback(INetworkProtocolCallback* callback) = 0;
		virtual void							BeginReadingLoopUnsafe() = 0;
		virtual void							SendString(const WString& str) = 0;
		virtual void							Stop() = 0;
	};

	class INetworkProtocolClient : public virtual Interface
	{
	public:
		virtual INetworkProtocolConnection*		GetConnection() = 0;
		virtual void							WaitForServer() = 0;
	};

	class INetworkProtocolServer : public virtual Interface
	{
	public:
		virtual INetworkProtocolConnection*		WaitForClient() = 0;
		virtual void							Stop() = 0;
	};
}

#endif