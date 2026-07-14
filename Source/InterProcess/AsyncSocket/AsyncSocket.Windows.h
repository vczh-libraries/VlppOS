/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Windows implementation of IAsyncSocket(Server|Client)

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_WINDOWS
#define VCZH_INTERPROCESS_ASYNCSOCKET_WINDOWS

// Winsock must precede every include that can include windows.h.
#include <WinSock2.h>
#include <MSWSock.h>
#include <Windows.h>

#include "AsyncSocket.h"
#include "../../Threading.h"

namespace vl::inter_process::async_tcp_socket::windows_socket
{
	class AsyncSocketServer : public Object, public virtual IAsyncSocketServer
	{
	private:
		class Impl;
		Impl*								impl = nullptr;

	public:
		AsyncSocketServer(vint port);
		~AsyncSocketServer();

		WaitForClientResult					OnClientConnected(IAsyncSocketConnection* connection) override;
		void								Start() override;
		void								Stop() override;
		bool								IsStopped() override;
	};

	class AsyncSocketClient : public Object, public virtual IAsyncSocketClient
	{
	private:
		class Impl;
		Impl*								impl = nullptr;

	public:
		AsyncSocketClient(vint port);
		~AsyncSocketClient();

		IAsyncSocketConnection*				GetConnection() override;
		void								WaitForServer() override;
		ClientStatus						GetStatus() override;
	};
}

#endif
