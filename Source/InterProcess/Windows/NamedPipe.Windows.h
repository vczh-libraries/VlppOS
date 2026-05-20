/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	NamedPipeServer
	NamedPipeClient

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_NAMEDPIPE
#define VCZH_INTERPROCESS_WINDOWS_NAMEDPIPE

#include "NetworkProtocol.Windows.h"

namespace vl::inter_process
{

class NamedPipeServer;

class NamedPipeConnection : public Object, public virtual INetworkProtocolConnection
{
	friend class NamedPipeServer;
// -----------------------------------------------------------------------
// Reading
// -----------------------------------------------------------------------

private:
	class ReadWaitContext;

	bool											firstRead = true;
	atomic_vint										stopped = 0;
	collections::Array<BYTE>						bufferReadFile;
	stream::MemoryStream							streamReadFile;
	SpinLock										lockReadWait;
	std::atomic<ReadWaitContext*>					readWaitContext = nullptr;
	OVERLAPPED										overlappedReadFile;
	HANDLE											hEventReadFile = INVALID_HANDLE_VALUE;
	atomic_vint										pendingCallbacks = 0;
	EventObject										eventPendingCallbacks;

	void											BeginReadingUnsafe();
	void											SubmitReadBufferUnsafe(vint bytes);
	void											EndReadingUnsafe();
	void											BeginPendingCallback();
	void											EndPendingCallback();

public:
	void											BeginReadingLoopUnsafe() override;

// -----------------------------------------------------------------------
// Writing
// -----------------------------------------------------------------------
private:
	SpinLock										lockWrite;
	stream::MemoryStream							streamWriteFile;
	OVERLAPPED										overlappedWriteFile;
	HANDLE											hEventWriteFile = INVALID_HANDLE_VALUE;

	vint32_t										WriteInt32ToStream(vint32_t number);
	vint32_t										WriteStringToStream(const WString& str);
	void											BeginSendStream();
	void											EndSendStream(vint32_t bytes);

protected:
	void											SendString(const WString& str) override;

// -----------------------------------------------------------------------
// Connection
// -----------------------------------------------------------------------

protected:
	// NamedPipe doesn't support a single message that is larger than 64K
	static constexpr vint32_t						MaxMessageSize = 65536;

	NamedPipeServer*								server = nullptr;
	INetworkProtocolCallback*						callback = nullptr;
	HANDLE											hPipe = INVALID_HANDLE_VALUE;

	void											OnDisconnected();

	NamedPipeConnection(HANDLE _hPipe);

public:
	~NamedPipeConnection();

	void											InstallCallback(INetworkProtocolCallback* _callback) override;
	void											Stop() override;
};

class NamedPipeServer : public Object, public virtual INetworkProtocolServer
{
	friend class NamedPipeConnection;
protected:
	class PendingConnection : public Object
	{
	public:
		class ConnectWaitContext;

		NamedPipeServer*								server = nullptr;
		Ptr<NamedPipeConnection>						connection;
		SpinLock										lockConnectWait;
		std::atomic<ConnectWaitContext*>					connectWaitContext = nullptr;
		OVERLAPPED										overlappedConnect;
		HANDLE											hEventConnect = INVALID_HANDLE_VALUE;
		atomic_vint										pendingCallbacks = 0;
		EventObject										eventPendingCallbacks;

		PendingConnection(NamedPipeServer* _server, Ptr<NamedPipeConnection> _connection);
		~PendingConnection();

		void											Stop();
		void											BeginPendingCallback();
		void											EndPendingCallback();
	};

	static HANDLE									ServerCreatePipe(const WString& pipeName);

	WString											pipeName;

	// covers started, stopped, connections and pendingConnections
	SpinLock										lockConnections;
	bool											started = false;
	bool											stopped = false;
	collections::List<Ptr< NamedPipeConnection>>	connections;
	collections::List<Ptr<PendingConnection>>		pendingConnections;

	void											BeginListening();
	void											CompletePendingConnection(Ptr<PendingConnection> pendingConnection, bool connected);
	void											CompletePendingConnection(PendingConnection* pendingConnection, bool connected);

public:
	NamedPipeServer(const WString& _pipeName);
	~NamedPipeServer();

	WaitForClientResult								OnClientConnected(INetworkProtocolConnection* connection) override;
	void											Start() override;
	void											Stop() override;
	bool											IsStopped() override;
};

class NamedPipeClient : public NamedPipeConnection, public virtual INetworkProtocolClient
{
protected:
	static HANDLE									ClientCreatePipe(const WString& pipeName);
	ClientStatus									status = ClientStatus::Ready;

public:
	NamedPipeClient(const WString& _pipeName);
	~NamedPipeClient();

	INetworkProtocolConnection*						GetConnection() override;
	void											WaitForServer() override;
	ClientStatus									GetStatus() override;
	void											Stop() override;
};

}

#endif
