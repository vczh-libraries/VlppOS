/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
  IHttpRequest(Connection|Callback)

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_HTTPREQUEST
#define VCZH_INTERPROCESS_ASYNCSOCKET_HTTPREQUEST

#include "AsyncSocket.h"

namespace vl::inter_process::async_tcp_socket
{
	constexpr vint HttpIncompleteMessageTimeout = 30 * 1000;
	constexpr vint HttpRequestLineSizeLimit = 8 * 1024;
	constexpr vint HttpHeaderBlockSizeLimit = 64 * 1024;
	constexpr vint HttpBodySizeLimit = 16 * 1024 * 1024;
	constexpr vint HttpChunkSizeLineLimit = 4 * 1024;
	constexpr vint HttpTrailerBlockSizeLimit = 64 * 1024;

	struct HttpVersion
	{
		vint							major = 1;
		vint							minor = 1;
	};

	struct HttpField
	{
		WString							name;
		collections::Array<vuint8_t>		value;
	};

	enum class HttpFramingKind
	{
		None,
		ContentLength,
		Chunked,
	};

	enum class HttpFramingAnalysisResult
	{
		Succeeded,
		Invalid,
		UnsupportedTransferCoding,
	};

	struct HttpFraming
	{
		HttpFramingKind					kind = HttpFramingKind::None;
		vuint64_t						contentLength = 0;
		vint							contentLengthFieldCount = 0;
		vint							contentLengthValueCount = 0;
		bool							contentLengthValuesPlainDecimal = true;
		bool							connectionClose = false;
	};

	struct HttpBodyChunk
	{
		collections::Array<vuint8_t>		data;
	};

	struct HttpBody
	{
		collections::List<HttpBodyChunk>	chunks;
		collections::List<HttpField>		trailers;
	};

	extern HttpFramingAnalysisResult	AnalyzeHttpFraming(const collections::List<HttpField>& fields, HttpFraming& framing);
	extern const HttpField*				FindHttpField(const collections::List<HttpField>& fields, const WString& normalizedName);
	extern vint							CountHttpFields(const collections::List<HttpField>& fields, const WString& normalizedName);
	extern HttpField						CreateAsciiHttpField(const WString& name, const WString& value);
	extern bool							DecodeAsciiHttpFieldValue(const collections::Array<vuint8_t>& value, WString& text);
	extern bool							HttpFieldValueEqualsAscii(const collections::Array<vuint8_t>& value, const WString& expected);
	extern bool							TryGetHttpBodySize(const HttpBody& body, vint& size);
	extern bool							FlattenHttpBody(const HttpBody& body, collections::Array<vuint8_t>& bytes);
	extern void							SetHttpBodyBytes(HttpBody& body, collections::Array<vuint8_t>&& bytes);
	extern bool							EncodeStrictUtf8(const WString& text, collections::Array<vuint8_t>& bytes);
	extern bool							DecodeStrictUtf8(const vuint8_t* bytes, vint count, WString& text);

	enum class HttpRequestLineValidationResult
	{
		Succeeded,
		InvalidMethod,
		InvalidRequestTarget,
		TooLong,
	};

	extern HttpRequestLineValidationResult	ValidateHttpRequestLine(const WString& method, const WString& requestTarget);

	class HttpRequest : public Object
	{
	public:
		HttpVersion						version;
		WString							method;
		WString							requestTarget;
		collections::List<HttpField>		headers;
		HttpBody						body;
	};

	class HttpResponse : public Object
	{
	public:
		HttpVersion						version;
		vint							statusCode = 200;
		WString							reason;
		collections::List<HttpField>		headers;
		HttpBody						body;
	};

	enum class HttpResponseFailure
	{
		NotFound = 404,
	};

	enum class HttpRequestBodyParsingResult
	{
		Succeeded,
		Incomplete,
		Invalid,
	};

	enum class HttpRequestFailure
	{
		BadRequest = 400,
		RequestTimeout = 408,
		PayloadTooLarge = 413,
		UriTooLong = 414,
		ExpectationFailed = 417,
		RequestHeaderFieldsTooLarge = 431,
		NotImplemented = 501,
		HttpVersionNotSupported = 505,
	};

	extern HttpRequestBodyParsingResult		ParseHttpRequestBodyToChunks(
		const vuint8_t*						buffer,
		vint							availableBytes,
		HttpBody&						output,
		vint&							consumedBytes
		);

	class IHttpRequestConnection;

	class IHttpRequestCallback : public virtual Interface
	{
	public:
		virtual void						OnReadRequest(Ptr<HttpRequest> request);
		virtual void						OnReadRequestFailure(HttpRequestFailure failure);
		virtual void						OnReadResponse(Ptr<HttpResponse> response);
		virtual void						OnReadResponseFailure(HttpResponseFailure failure);
		virtual void						OnWriteCompleted();
		virtual void						OnError(const WString& error, bool fatal);
		virtual void						OnConnected();
		virtual void						OnDisconnected();
		virtual void						OnInstalled(IHttpRequestConnection* connection) = 0;
	};

	class IHttpRequestConnection : public virtual Interface
	{
	public:
		virtual void						InstallCallback(IHttpRequestCallback* callback) = 0;
		virtual void						BeginReadingLoopUnsafe() = 0;
		virtual void						SendRequest(Ptr<HttpRequest> request, vint responseTimeout = HttpIncompleteMessageTimeout) = 0;
		virtual void						SendResponse(Ptr<HttpResponse> response) = 0;
		virtual void						Stop() = 0;
	};
}

#endif
