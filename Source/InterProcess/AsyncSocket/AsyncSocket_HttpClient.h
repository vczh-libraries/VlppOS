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
		/// <remarks>Requiring an asynchronous socket client is intentional. The supplied client is used directly for the first physical lane and creates fresh same-endpoint clients for the second lane and transport recovery. Keep this dependency explicit; do not add a factory parameter or select a platform socket internally.</remarks>
		SocketHttpClient(
			Ptr<IAsyncSocketClient> client,
			const WString& server,
			const WString& urlPrefix
			);
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

