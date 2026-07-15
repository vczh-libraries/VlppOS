/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

macOS implementation of IAsyncSocket(Server|Client)

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_MACOS
#define VCZH_INTERPROCESS_ASYNCSOCKET_MACOS

#include "AsyncSocket.h"

#if defined VCZH_GCC && defined VCZH_APPLE

namespace vl::inter_process::async_tcp_socket::macos_socket
{
	class AsyncSocketServer : public Object, public virtual IAsyncSocketServer
	{
	private:
		class Impl;
		Impl*								impl = nullptr;

	public:
		AsyncSocketServer(vint port);
		~AsyncSocketServer();

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

		IAsyncSocketConnection*				GetConnection() override;
		void								WaitForServer() override;
		ClientStatus						GetStatus() override;
	};
}

#endif

#endif
