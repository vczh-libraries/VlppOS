/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
  IChannel<T>

***********************************************************************/

#ifndef VCZH_INTERPROCESS_CHANNEL
#define VCZH_INTERPROCESS_CHANNEL

#include <Vlpp.h>

namespace vl::inter_process
{
	constexpr const wchar_t* ErrorChannel = L"!Error";
	constexpr const wchar_t* SystemChannel = L"!System";

/***********************************************************************
IGuiRemoteProtocolChannel<T>
***********************************************************************/

	/// <summary>
	/// Callbacks for channel events.
	/// </summary>
	/// <typeparam name="TPackage">The type of the package.</typeparam>
	template<typename TPackage>
	class IChannelReader : public virtual Interface
	{
	public:
		/// <summary>
		/// Called when the channel receives a message.
		/// </summary>
		/// <param name="senderClientId">The sender client id.</param>
		/// <param name="package">The message.</param>
		virtual void							OnRead(vint senderClientId, const TPackage& package) = 0;
	};

	/// <summary>
	/// Represents a channel.
	/// One server to client connection can host multiple channels, but each channel might only cover part of clients.
	/// Channels are distinguished by their names.
	/// Channel name cannot contain "!", channel name starting with "!" is reserved for system channels.
	/// There will be no system channel representations, the channel name is used as a symbol between the server and clients for system events.
	/// </summary>
	/// <typeparam name="TPackage">The type of the package.</typeparam>
	template<typename TPackage>
	class IChannel : public virtual Interface
	{
	public:
		/// <summary>
		/// Get the channel name.
		/// </summary>
		/// <returns>The name of the channel.</returns>
		virtual const WString&					GetChannelName() = 0;

		/// <summary>
		/// Get the installed reader.
		/// </summary>
		/// <returns>The installed reader.</returns>
		virtual IChannelReader<TPackage>*		GetReader() = 0;

		/// <summary>
		/// Install a reader.
		/// If multiple text messages are received before installing the reader, all messages will be pushed to the reader right away.
		/// This function can only be called once to install a reader, and no uninstallation is supported.
		/// </summary>
		/// <param name="receiver">The reader to install.</param>
		virtual void							Initialize(IChannelReader<TPackage>* receiver) = 0;

		/// <summary>
		/// Queue a message to send to a client using the same channel.
		/// If the remote client doesn't have this channel, the message will be discarded.
		/// </summary>
		/// <param name="receiverClientId">The receiver client id.</param>
		/// <param name="package">The message to send.</param>
		virtual void							SendToClient(vint receiverClientId, const TPackage& package) = 0;

		/// <summary>
		/// Queue a message to broadcast to all other clients using the same channel.
		/// If the remote client doesn't have this channel, the message will be discarded.
		/// </summary>
		/// <param name="package">The message to broadcast.</param>
		virtual void							BroadcastFromClient(const TPackage& package) = 0;

		/// <summary>
		/// Queue a message to broadcast to all other clients using the same channel.
		/// If the remote client doesn't have this channel, the message will be discarded.
		/// </summary>
		/// <param name="package">The message to broadcast.</param>
		/// <param name="blockedReceivers">The receiver client ids blocked from this broadcast.</param>
		virtual void							BroadcastFromClient(const TPackage& package, const collections::List<vint>& blockedReceivers) = 0;

		/// <summary>
		/// Send all queued messages.
		/// </summary>
		/// <param name="disconnected">Indicates whether the client is disconnected.</param>
		virtual void							BatchWrite(bool& disconnected) = 0;
	};

/***********************************************************************
IChannelClient
***********************************************************************/

	enum class ClientStatus
	{
		Ready,				// Created, ready to call WaitForServer.
		WaitingForServer,	// WaitForServer is called, blocked.
		Connected,			// Connection established.
		Disconnected,		// Connection lost.
	};

	enum class WaitForClientResult
	{
		Accept,
		Reject,
	};

	/// <summary>
	/// Represents a client.
	/// </summary>
	/// <typeparam name="TPackage">The type of the package.</typeparam>
	template<typename TPackage>
	class IChannelClient : public virtual Interface
	{
	public:
		using ChannelMap = collections::Dictionary<WString, IChannel<TPackage>*>;
		using ChannelNameList = typename ChannelMap::KeyContainer;

		/// <summary>
		/// Called when the connection is established.
		/// This function will be implemented by the user, the default implementation will be empty.
		/// </summary>
		/// <param name="clientId">The client id.</param>
		virtual void						OnConnected(vint clientId) = 0;

		/// <summary>
		/// Called when the connection is disconnected.
		/// This function will be implemented by the user, the default implementation will be empty.
		/// </summary>
		virtual void						OnDisconnected() = 0;

		/// <summary>
		/// Called when a fetal error occurs.
		/// When any fetal error is broadcasted from server side, all clients is supposed to receive such error if possible, and the server will shut down.
		/// This function will be implemented by the user, the default implementation will be empty.
		/// </summary>
		/// <param name="errorMessage">The error message.</param>
		virtual void						OnReadError(const WString& errorMessage) = 0;

		/// <summary>
		/// Called when a local error occurs.
		/// This function will be implemented by the user, the default implementation will be empty.
		/// </summary>
		/// <param name="errorMessage">The error message.</param>
		/// <param name="fatal">Indicates whether the error is not recoverable. The client will automatically stop after a fatal error.</param>
		virtual void						OnLocalError(const WString& errorMessage, bool fatal) = 0;

		/// <summary>
		/// Called when available connection names are required.
		/// This function will be implemented by the user, the default implementation will be empty.
		/// </summary>
		/// <returns>All connection names.</returns>
		virtual const ChannelNameList&		OnGetChannelNames() = 0;

		/// <summary>
		/// Get all channels.
		/// The returned map should be empty before the connection is established.
		/// Channel objects will be created by the client implementation.
		/// </summary>
		/// <returns>All connections.</returns>
		virtual const ChannelMap&			GetChannels() = 0;

		/// <summary>
		/// Get the client id.
		/// It should return -1 before the connection is established.
		/// The client id will be assigned by the server.
		/// </summary>
		/// <returns>The client id.</returns>
		virtual vint						GetClientId() = 0;

		/// <summary>
		/// Block until the connection to the server is established.
		/// Calling it more than once, after disconnecting or on a local client returns immediately.
		/// </summary>
		virtual void						WaitForServer() = 0;

		/// <summary>
		/// Returns the status of the client.
		/// </summary>
		/// <returns>The status of the client.</returns>
		virtual ClientStatus				GetStatus() = 0;

		/// <summary>
		/// Raise a fatal error.
		/// </summary>
		/// <param name="errorMessage">The fatal error.</param>
		virtual void						BroadcastError(const WString& errorMessage) = 0;
	};

/***********************************************************************
IChannelServer
***********************************************************************/

	/// <summary>
	/// Represents a server.
	/// </summary>
	/// <typeparam name="TPackage">The type of the package.</typeparam>
	template<typename TPackage>
	class IChannelServer : public virtual Interface
	{
	public:
		using ClientChannelMap = collections::Group<vint, WString>;
		using ClientIdList = typename ClientChannelMap::KeyContainer;

		/// <summary>
		/// Called when any client connects to the server.
		/// The server begins listening to client connections after <see cref="Start"/> is called.
		/// No callback happens before <see cref="Start"/> or after <see cref="Stop"/> is called.
		/// This function will be implemented by the user, the default implementation will return true.
		/// </summary>
		/// <param name="clientId">The client id.</param>
		/// <param name="availableChannels">The available channels.</param>
		/// <returns>Returns "Reject" to disconnect the client immediatelly.</returns>
		virtual WaitForClientResult			OnClientConnected(vint clientId, const IChannelClient<TPackage>::ChannelNameList& availableChannels) = 0;

		/// <summary>
		/// Start the server.
		/// </summary>
		virtual void						Start() = 0;

		/// <summary>
		/// Called when any client disconnects from the server.
		/// This function will be implemented by the user, the default implementation will be empty.
		/// </summary>
		/// <param name="clientId">The client id.</param>
		virtual void						OnClientDisconnected(vint clientId) = 0;

		/// <summary>
		/// Connect a local client to the server.
		/// Connections between such client to the server will be local and established immediately, no network transmission is involved.
		/// </summary>
		/// <param name="localClient">The local client.</param>
		/// <returns>Returns the assigned client id, or -1 if the connection is already established, no matter local or remote.</returns>
		virtual vint						ConnectLocalClient(Ptr<IChannelClient<TPackage>> localClient) = 0;

		/// <summary>
		/// Test if a client id is local.
		/// </summary>
		/// <param name="clientId">The client id.</param>
		/// <returns>Returns true if the client id is local, false otherwise.</returns>
		virtual bool						IsLocalClient(vint clientId) = 0;

		/// <summary>
		/// Disconnect a client.
		/// </summary>
		/// <param name="clientId">The client id.</param>
		/// <returns>Returns true if the client is successfully disconnected, false otherwise.</returns>
		virtual bool						DisconnectClient(vint clientId) = 0;

		/// <summary>
		/// Get all client ids.
		/// </summary>
		/// <returns>All client ids.</returns>
		virtual const ClientIdList&			GetClientIds() = 0;

		/// <summary>
		/// Get all client channels.
		/// Channel objects will be created by the server implementation.
		/// </summary>
		/// <returns>Returns a map from client id to available channels.</returns>
		virtual const ClientChannelMap&		GetClientChannels() = 0;

		/// <summary>
		/// Raise a fatal error.
		/// </summary>
		/// <param name="errorMessage">The fatal error.</param>
		virtual void						BroadcastError(const WString& errorMessage) = 0;

		/// <summary>
		/// Stop the server.
		/// </summary>
		virtual void						Stop() = 0;

		/// <summary>
		/// Test if the server has stopped.
		/// A stopped server could be caused by either calling <see cref="Stop"/> or the underlying mechanism failing.
		/// </summary>
		/// <returns>Returns true if the server has stopped, false otherwise.</returns>
		virtual bool						IsStopped() = 0;
	};
}

#endif
