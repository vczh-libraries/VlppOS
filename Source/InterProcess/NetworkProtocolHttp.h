/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpRequest
	HttpResponse
	HttpError

***********************************************************************/

#ifndef VCZH_INTERPROCESS_NETWORKPROTOCOLHTTP
#define VCZH_INTERPROCESS_NETWORKPROTOCOLHTTP

#include "NetworkProtocol.h"

namespace vl::inter_process
{
	/*
	* GET: /Connect
	* Creates a new logical connection and returns its request and response URLs.
	* Repeated calls create separate logical connections.
	*/
	constexpr const wchar_t* HttpServerUrl_Connect = L"/VlppInterProcess/Connect";

	/*
	* POST: /Request/GUID
	* Client should always maintain a living request on the server.
	*
	* Returns only when a request is issued.
	* It will be pending or timeout if no request is issued.
	* If a request is issued but no living request available, it waits.
	*/
	constexpr const wchar_t* HttpServerUrl_Request = L"/VlppInterProcess/Request";

	/*
	* POST: /Response/GUID
	* To send responses or events to the server.
	* May return one queued server message in the same HTTP response.
	*/
	constexpr const wchar_t* HttpServerUrl_Response = L"/VlppInterProcess/Response";

	extern WString HttpUrlEncodeQuery(const WString& query);
	extern WString HttpUrlDecodeQuery(const WString& query);
}

namespace vl::inter_process::windows_http
{
	/// <summary>An http request.</summary>
	class HttpRequest
	{
		typedef collections::Array<char>					BodyBuffer;
		typedef collections::List<WString>					StringList;
		typedef collections::Dictionary<WString, WString>	HeaderMap;
	public:
		/// <summary>Query of the request, like "/index.html".</summary>
		WString												query;
		/// <summary>Set to true if the request uses SSL, or https.</summary>
		bool												secure = false;
		/// <summary>User name to authorize. Set to empty if authorization is not needed.</summary>
		WString												username;
		/// <summary>Password to authorize. Set to empty if authorization is not needed.</summary>
		WString												password;
		/// <summary>HTTP method, like "GET", "POST", "PUT", "DELETE", etc.</summary>
		WString												method;
		/// <summary>Cookie. Set to empty if cookie is not needed.</summary>
		WString												cookie;
		/// <summary>Request body. This is a byte array.</summary>
		BodyBuffer											body;
		/// <summary>Content type, like "text/xml".</summary>
		WString												contentType;
		/// <summary>Accept type list, elements like "text/xml".</summary>
		StringList											acceptTypes;
		/// <summary>A dictionary to contain extra headers.</summary>
		HeaderMap											extraHeaders;
		/// <summary>Set to true to let this request finish when <see cref="HttpClientApi.Stop"/> is called.</summary>
		bool												keepAliveOnStop = false;
		/// <summary>Timeout for resolving the host name. 0 or -1 means infinite.</summary>
		vint												resolveTimeout = 0;
		/// <summary>Timeout for connecting to the server. 0 or -1 means infinite.</summary>
		vint												connectTimeout = 60000;
		/// <summary>Timeout for sending the request. 0 or -1 means infinite.</summary>
		vint												sendTimeout = 30000;
		/// <summary>Timeout for receiving the response. 0 or -1 means infinite.</summary>
		vint												receiveTimeout = 30000;

		HttpRequest() = default;
		void												SetBodyUtf8(const WString& bodyString);
	};

	/// <summary>A type representing an http response.</summary>
	class HttpResponse
	{
		typedef collections::Array<char>					BodyBuffer;
	public:
		/// <summary>Status code, like 200.</summary>
		vint												statusCode = 0;
		/// <summary>Response body. This is a byte array.</summary>
		BodyBuffer											body;
		/// <summary>Returned cookie from the server.</summary>
		WString												cookie;
		/// <summary>Returned content type from the server.</summary>
		WString												contentType;

		HttpResponse() = default;
		WString												GetBodyUtf8() const;
	};

	/// <summary>A transport error reported by the underlying HTTP implementation.</summary>
	class HttpError
	{
	public:
		vuint32_t											errorCode = 0;
		WString												operation;
		WString												message;
	};
}

#endif
