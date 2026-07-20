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
		/// <remarks>Requiring an asynchronous socket server is intentional. This protocol adapter takes its port from the server, forwards the caller-selected transport to <see cref="SocketHttpServerApi"/>, and never creates another server. Keep this dependency explicit; do not add an overload that selects a platform server internally.</remarks>
		SocketHttpServer(Ptr<IAsyncSocketServer> server, const WString& urlPrefix);
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
