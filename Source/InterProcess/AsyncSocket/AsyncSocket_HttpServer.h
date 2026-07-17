/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	SocketHttpServer

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_HTTPSERVER
#define VCZH_INTERPROCESS_ASYNCSOCKET_HTTPSERVER

#include "AsyncSocket_HttpServerApi.h"

namespace vl::inter_process::async_tcp_socket
{
	class SocketHttpServer
		: public SocketHttpServerApi
		, public virtual INetworkProtocolServer
	{
		class Impl;
		Ptr<Impl>							impl;

	protected:
		void								OnHttpRequestReceived(Ptr<SocketHttpRequestContext> context) override;
		void								OnHttpServerStopping() override;

	public:
		SocketHttpServer(const WString& baseUrl, vint port);
		~SocketHttpServer();

		SocketHttpServer(const SocketHttpServer&) = delete;
		SocketHttpServer(SocketHttpServer&&) = delete;
		SocketHttpServer& operator=(const SocketHttpServer&) = delete;
		SocketHttpServer& operator=(SocketHttpServer&&) = delete;

		virtual WaitForClientResult			OnClientConnected(INetworkProtocolConnection* connection) override;
		void								Start() override;
		void								Stop() override;
		bool								IsStopped() override;
	};
}

#endif
