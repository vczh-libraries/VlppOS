/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Linux implementation of IAsyncSocket(Server|Client)

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_LINUX
#define VCZH_INTERPROCESS_ASYNCSOCKET_LINUX

#include "AsyncSocket.h"

#if defined VCZH_GCC && !defined VCZH_APPLE

namespace vl::inter_process::async_tcp_socket::linux_socket
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

#endif
