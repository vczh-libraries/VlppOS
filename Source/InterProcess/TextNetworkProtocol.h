/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
  INetworkProtocol<T>

***********************************************************************/

#ifndef VCZH_INTERPROCESS_TEXTNETWORKPROTOCOL
#define VCZH_INTERPROCESS_TEXTNETWORKPROTOCOL

#include "Channel.h"

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
		/// Called when a localerror message is raised.
		/// </summary>
		/// <param name="error">The error message.</param>
		virtual void							OnReadError(const WString& error) = 0;

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
		/// Block until a client connects to the server, returning the connection between the server and this client.
		/// </summary>
		/// <returns>The connection to the client.</returns>
		virtual INetworkProtocolConnection*		WaitForClient() = 0;

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
  Empty means there is no source client, for example, a message generated from the server.

When a client establishes a connection to the server, channel names will be sent to the server:
  clientId will be empty, it does not mean broadcasting.
  channelName will be empty.
  messageBody will be all available channel names joined by "!", as "!" cannot be part of the channel name anyway.
After the server receives the first message from a client, an client id will be sent to the client:
  clientId is the assigned client id, starting from 1.
  channelName will be empty.
  messageBody will be empty.
After the client receives the first message from the server, WaitForClient unblocks.
  This can be implemented using EventObject.
If the server stops in any reason before the client receiving the client id, WaitForClient should also unblock.

Later 
***********************************************************************/

/***********************************************************************
NetworkProtocolChannelClient
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannelClient : public Object, public virtual IChannelClient<TPackage>
	{
		static_assert(std::is_same_v<collections::List<typename TSerialization::SourceType>, TPackage>);
		static_assert(std::is_same_v<typename TSerialization::DestType, WString>);
	protected:
		typename TSerialization::ContextType					context;
		Ptr<INetworkProtocolClient>								npClient;

	public:
		void OnConnected(vint clientId) override
		{
			// default implementation does nothing
		}

		void OnDisconnected() override
		{
			// default implementation does nothing
		}

		void OnError(const WString& errorMessage) override
		{
			// default implementation does nothing
		}

		NetworkProtocolChannelClient(
			Ptr<INetworkProtocolClient> _npClient,
			const typename TSerialization::ContextType& _context = {}
		)
			: npClient(_npClient)
			, context(_context)
		{
		}
	};

/***********************************************************************
NetworkProtocolChannelServer
***********************************************************************/

	template<typename TPackage, typename TSerialization>
	class NetworkProtocolChannelServer : public Object, public virtual IChannelServer<TPackage>
	{
		static_assert(std::is_same_v<collections::List<typename TSerialization::SourceType>, TPackage>);
		static_assert(std::is_same_v<typename TSerialization::DestType, WString>);
	protected:
		typename TSerialization::ContextType					context;
		Ptr<INetworkProtocolServer>								npServer;

	public:

		bool OnClientConnected(vint clientId, const typename IChannelClient<TPackage>::ChannelNameList& availableChannels) override
		{
			// default implementation allows all clients to connect
			return true;
		}

		void OnClientDisconnected(vint clientId) override
		{
			// default implementation does nothing
		}

		NetworkProtocolChannelServer(
			Ptr<INetworkProtocolServer> _npServer,
			const typename TSerialization::ContextType& _context = {}
		)
			: npServer(_npServer)
			, context(_context)
		{
		}
	};
}

#endif