/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	SocketHttp(ServerApi|RequestContext)

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_HTTPSERVERAPI
#define VCZH_INTERPROCESS_ASYNCSOCKET_HTTPSERVERAPI

#include "AsyncSocket_HttpRequestServer.h"

namespace vl::inter_process::async_tcp_socket
{
	class SocketHttpServerApi;
	class SocketHttpServerApiDispatcher;

	class SocketHttpRequestContext : public Object
	{
		friend class SocketHttpServerApiDispatcher;

		class Impl;
		Ptr<Impl>							impl;

		SocketHttpRequestContext(Ptr<Impl> _impl);

	public:
		~SocketHttpRequestContext();

		SocketHttpRequestContext(const SocketHttpRequestContext&) = delete;
		SocketHttpRequestContext(SocketHttpRequestContext&&) = delete;
		SocketHttpRequestContext& operator=(const SocketHttpRequestContext&) = delete;
		SocketHttpRequestContext& operator=(SocketHttpRequestContext&&) = delete;

		Ptr<HttpRequest>					GetRequest();
		WString								GetRelativePath();
		WString								GetQuery();
		bool								TryGetBodyUtf8(WString& body);

		bool								Respond(
			Ptr<HttpResponse> response,
			Func<void(bool)> completion = {}
			);
		bool								RespondStatus(
			vint statusCode,
			const WString& reason,
			Func<void(bool)> completion = {}
			);
		bool								RespondBytes(
			vint statusCode,
			const WString& reason,
			const WString& contentType,
			const collections::Array<vuint8_t>& body,
			Func<void(bool)> completion = {}
			);
		bool								RespondUtf8(
			vint statusCode,
			const WString& reason,
			const WString& contentType,
			const WString& body,
			Func<void(bool)> completion = {}
			);
		bool								Cancel();
	};

	class SocketHttpServerApi : public Object
	{
		friend class SocketHttpServerApiDispatcher;

		class Impl;
		Ptr<Impl>							impl;

	protected:
		virtual void						OnHttpRequestReceived(
			Ptr<SocketHttpRequestContext> context
			) = 0;
		virtual void						OnHttpServerStopping();

	public:
		SocketHttpServerApi(
			const WString& urlPrefix,
			bool respondToOptions
			);
		virtual ~SocketHttpServerApi();

		SocketHttpServerApi(const SocketHttpServerApi&) = delete;
		SocketHttpServerApi(SocketHttpServerApi&&) = delete;
		SocketHttpServerApi& operator=(const SocketHttpServerApi&) = delete;
		SocketHttpServerApi& operator=(SocketHttpServerApi&&) = delete;

		void								Start();
		void								Stop();
		bool								IsStopped();
		WString								GetUrlPrefix();
	};
}

#endif
