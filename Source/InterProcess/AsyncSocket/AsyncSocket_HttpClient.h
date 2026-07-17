/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	SocketHttpClient

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_HTTPCLIENT
#define VCZH_INTERPROCESS_ASYNCSOCKET_HTTPCLIENT

#include "AsyncSocket_HttpClientApi.h"

namespace vl::inter_process::async_tcp_socket
{
	class SocketHttpClient
		: public Object
		, public virtual INetworkProtocolClient
		, public virtual INetworkProtocolConnection
	{
		class Impl;
		Ptr<Impl>						impl;

	public:
		using NativeClientFactory = Func<Ptr<IAsyncSocketClient>(vint)>;

		SocketHttpClient(const WString& baseUrl, vint port);
		SocketHttpClient(const WString& baseUrl, vint port, NativeClientFactory clientFactory);
		~SocketHttpClient();

		SocketHttpClient(const SocketHttpClient&) = delete;
		SocketHttpClient(SocketHttpClient&&) = delete;
		SocketHttpClient& operator=(const SocketHttpClient&) = delete;
		SocketHttpClient& operator=(SocketHttpClient&&) = delete;

		INetworkProtocolConnection*		GetConnection() override;
		void							WaitForServer() override;
		ClientStatus					GetStatus() override;
		void							InstallCallback(INetworkProtocolCallback* callback) override;
		void							BeginReadingLoopUnsafe() override;
		void							SendString(const WString& str) override;
		void							Stop() override;
	};
}

#endif

