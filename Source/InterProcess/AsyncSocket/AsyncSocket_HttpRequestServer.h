/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpRequestServer

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_HTTPREQUESTSERVER
#define VCZH_INTERPROCESS_ASYNCSOCKET_HTTPREQUESTSERVER

#include "AsyncSocket_HttpRequest.h"

namespace vl::inter_process::async_tcp_socket
{
	/// <summary>Adapts an asynchronous TCP server to HTTP/1.1 request connections.</summary>
	class HttpRequestServer : public Object
	{
	private:
		class Impl;
		Ptr<Impl>							impl;

	public:
		explicit HttpRequestServer(Ptr<IAsyncSocketServer> server);
		/// <remarks>A derived destructor must call <see cref="Stop"/> before destroying any state accessed by <see cref="OnClientConnected"/>.</remarks>
		virtual ~HttpRequestServer();

		virtual WaitForClientResult			OnClientConnected(IHttpRequestConnection* connection);
		void							Start();
		void							Stop();
		bool							IsStopped();
	};
}

#endif
