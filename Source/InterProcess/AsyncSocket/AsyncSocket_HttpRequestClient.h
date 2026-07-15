/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpRequestClient

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_HTTPREQUESTCLIENT
#define VCZH_INTERPROCESS_ASYNCSOCKET_HTTPREQUESTCLIENT

#include "AsyncSocket_HttpRequest.h"

namespace vl::inter_process::async_tcp_socket
{
	/// <summary>Adapts an asynchronous TCP client to one HTTP/1.1 request connection.</summary>
	class HttpRequestClient : public Object
	{
	private:
		class Impl;
		Ptr<Impl>							impl;

	public:
		explicit HttpRequestClient(Ptr<IAsyncSocketClient> client);
		virtual ~HttpRequestClient();

		virtual IHttpRequestConnection*		GetConnection();
		virtual void							WaitForServer();
		virtual ClientStatus					GetStatus();
	};
}

#endif
