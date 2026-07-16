/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	SocketHttpClientApi

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_HTTPCLIENTAPI
#define VCZH_INTERPROCESS_ASYNCSOCKET_HTTPCLIENTAPI

#include "AsyncSocket_HttpRequestClient.h"
#include "../NetworkProtocolHttp.h"

namespace vl::inter_process::async_tcp_socket
{
	class SocketHttpClientApi : public Object
	{
		class Impl;
		Ptr<Impl>							impl;

	public:
		SocketHttpClientApi(
			Ptr<IAsyncSocketClient> client,
			const WString& authority
			);
		~SocketHttpClientApi();

		SocketHttpClientApi(const SocketHttpClientApi&) = delete;
		SocketHttpClientApi(SocketHttpClientApi&&) = delete;
		SocketHttpClientApi& operator=(const SocketHttpClientApi&) = delete;
		SocketHttpClientApi& operator=(SocketHttpClientApi&&) = delete;

		void								WaitForServer();
		ClientStatus						GetStatus();
		/// <summary>Send an HTTP request on the injected socket connection.</summary>
		/// <remarks>The injected socket owns name-resolution, connection, and send-phase timing. Only <see cref="windows_http::HttpRequest.receiveTimeout"/> controls the response deadline for this exchange.</remarks>
		void								HttpQuery(
			const windows_http::HttpRequest& request,
			Func<void(Variant<
				windows_http::HttpResponse,
				windows_http::HttpError
				>)> callback
			);
		void								Stop();

		static WString						UrlEncodeQuery(const WString& query);
		static WString						UrlDecodeQuery(const WString& query);
	};
}

#endif
