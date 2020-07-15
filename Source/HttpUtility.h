/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_HTTPUTILITY
#define VCZH_HTTPUTILITY

#include <Vlpp.h>

#ifdef VCZH_MSVC

namespace vl
{

/***********************************************************************
HTTP Utility
***********************************************************************/

	/// <summary>An http requiest.</summary>
	class HttpRequest
	{
		typedef collections::Array<char>					BodyBuffer;
		typedef collections::List<WString>					StringList;
		typedef collections::Dictionary<WString, WString>	HeaderMap;
	public:
		/// <summary>Name of the server, like "gaclib.net".</summary>
		WString				server;
		/// <summary>Port of the server, like 80.</summary>
		vint				port = 0;
		/// <summary>Query of the request, like "/index.html".</summary>
		WString				query;
		/// <summary>Set to true if the request uses SSL, or https.</summary>
		bool				secure = false;
		/// <summary>User name to authorize. Set to empty if authorization is not needed.</summary>
		WString				username;
		/// <summary>Password to authorize. Set to empty if authorization is not needed.</summary>
		WString				password;
		/// <summary>HTTP method, like "GET", "POST", "PUT", "DELETE", etc.</summary>
		WString				method;
		/// <summary>Cookie. Set to empty if cookie is not needed.</summary>
		WString				cookie;
		/// <summary>Request body. This is a byte array.</summary>
		BodyBuffer			body;
		/// <summary>Content type, like "text/xml".</summary>
		WString				contentType;
		/// <summary>Accept type list, elements like "text/xml".</summary>
		StringList			acceptTypes;
		/// <summary>A dictionary to contain extra headers.</summary>
		HeaderMap			extraHeaders;

		/// <summary>Create an empty request.</summary>
		HttpRequest() = default;

		/// <summary>Set <see cref="server"/>, <see cref="port"/>, <see cref="query"/> and <see cref="secure"/> fields for you using an URL.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="inputQuery">The URL.</param>
		bool				SetHost(const WString& inputQuery);

		/// <summary>Fill the text body in UTF-8.</summary>
		/// <param name="bodyString">The text to fill.</param>
		void				SetBodyUtf8(const WString& bodyString);
	};
	
	/// <summary>A type representing an http response.</summary>
	class HttpResponse
	{
		typedef collections::Array<char>		BodyBuffer;
	public:
		/// <summary>Status code, like 200.</summary>
		vint				statusCode = 0;
		/// <summary>Response body. This is a byte array.</summary>
		BodyBuffer			body;
		/// <summary>Returned cookie from the server.</summary>
		WString				cookie;

		HttpResponse() = default;

		/// <summary>Get the text body, encoding is assumed to be UTF-8.</summary>
		/// <returns>The response body as text.</returns>
		WString				GetBodyUtf8();
	};

	/// <summary>Send an http request and receive a response.</summary>
	/// <returns>Returns true if this operation succeeded, even when the server returns 404.</returns>
	/// <param name="request">The request to send.</param>
	/// <param name="response">Returns the response.</param>
	/// <remarks>
	/// <p>
	/// This function will block the calling thread until the respons is returned.
	/// </p>
	/// <p>
	/// This function is only available in Windows.
	/// </p>
	/// </remarks>
	/// <example><![CDATA[
	/// int main()
	/// {
	///     HttpRequest request;
	///     HttpResponse response;
	///     request.SetHost(L"http://www.msftncsi.com/ncsi.txt");
	///     HttpQuery(request, response);
	///     Console::WriteLine(L"Status:" + itow(response.statusCode));
	///     Console::WriteLine(L"Body:" + response.GetBodyUtf8());
	/// }
	/// ]]></example>
	extern bool				HttpQuery(const HttpRequest& request, HttpResponse& response);

	/// <summary>Encode a text as part of the url. This function can be used to create arguments in an URL.</summary>
	/// <returns>The encoded text.</returns>
	/// <param name="query">The text to encode.</param>
	/// <remarks>
	/// <p>
	/// When a character is not a digit or a letter,
	/// it is first encoded to UTF-8,
	/// then each byte is written as "%" with two hex digits.
	/// </p>
	/// <p>
	/// This function is only available in Windows.
	/// </p>
	///</remarks>
	extern WString			UrlEncodeQuery(const WString& query);
}

#endif

#endif
