/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
  IAsyncSocket(Server|Client)

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET
#define VCZH_INTERPROCESS_ASYNCSOCKET

#include "../Channel.h"

namespace vl::inter_process::async_tcp_socket
{
	/// <summary>A retained buffer for one asynchronous write.</summary>
	class AsyncSocketBuffer : public Object
	{
	public:
		collections::Array<vuint8_t>			data;
	};

	class IAsyncSocketConnection;

	/// <summary>Callbacks for an asynchronous byte-stream connection.</summary>
	class IAsyncSocketCallback : public virtual Interface
	{
	public:
		/// <summary>Called with one positive borrowed read block.</summary>
		virtual void							OnRead(const vuint8_t* buffer, vint size) = 0;
		/// <summary>Called after the complete retained write buffer has been sent.</summary>
		virtual void							OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer) {}
		/// <summary>Called when an asynchronous operation fails.</summary>
		virtual void							OnError(const WString& error, bool fatal) {}
		/// <summary>Called for the client connection after it is established.</summary>
		virtual void							OnConnected() {}
		/// <summary>Called exactly once when the connection stops.</summary>
		virtual void							OnDisconnected() {}
		/// <summary>Called synchronously when this callback is installed.</summary>
		virtual void							OnInstalled(IAsyncSocketConnection* connection) = 0;
	};

	/// <summary>An ordered, full-duplex asynchronous byte stream.</summary>
	class IAsyncSocketConnection : public virtual Interface
	{
	public:
		virtual void							InstallCallback(IAsyncSocketCallback* callback) = 0;
		virtual void							BeginReadingLoopUnsafe() = 0;
		virtual void							WriteAsync(Ptr<AsyncSocketBuffer> buffer) = 0;
		virtual void							Stop() = 0;
	};

	/// <summary>An asynchronous TCP client for the local machine.</summary>
	class IAsyncSocketClient : public virtual Interface
	{
	public:
		virtual IAsyncSocketConnection*			GetConnection() = 0;
		virtual void							WaitForServer() = 0;
		virtual ClientStatus					GetStatus() = 0;
	};

	/// <summary>An asynchronous TCP server for the local machine.</summary>
	class IAsyncSocketServer : public virtual Interface
	{
	public:
		virtual WaitForClientResult			OnClientConnected(IAsyncSocketConnection* connection) = 0;
		virtual void							Start() = 0;
		virtual void							Stop() = 0;
		virtual bool							IsStopped() = 0;
	};

	// This policy is intentionally platform-neutral. Each failed attempt creates
	// a fresh native socket and is followed by an asynchronous millisecond delay.
	constexpr vint AsyncSocketClientRetryCount = 50;
	constexpr vint AsyncSocketClientRetryDelay = 100;
}

#endif
