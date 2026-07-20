/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Windows implementation of IAsyncSocket(Server|Client)

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_WINDOWS
#define VCZH_INTERPROCESS_ASYNCSOCKET_WINDOWS

// Winsock must precede every include that can include windows.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <MSWSock.h>
#define _WINSOCKAPI_
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

		vint								GetPort() override;
		void								Start(IAsyncSocketServerCallback* callback) override;
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

		vint								GetPort() override;
		Ptr<IAsyncSocketClient>				CreateSameEndpointClient() override;
		IAsyncSocketConnection*				GetConnection() override;
		void								WaitForServer() override;
		ClientStatus						GetStatus() override;
	};
}

#endif
