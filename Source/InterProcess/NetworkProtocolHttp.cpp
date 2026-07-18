#include "NetworkProtocolHttp.h"
#include "AsyncSocket/HttpRequest.h"
#include "../Encoding/CharFormat/CharFormat.h"
#include "../Stream/Accessor.h"
#include "../Stream/EncodingStream.h"
#include "../Stream/MemoryStream.h"
#include "../Stream/MemoryWrapperStream.h"

namespace vl::inter_process
{
	using namespace vl::collections;

	namespace
	{
		void EncodeUtf8(const WString& text, Array<char>& utf8)
		{
			stream::MemoryStream memoryStream;
			{
				stream::Utf8Encoder encoder;
				stream::EncoderStream encoderStream(memoryStream, encoder);
				stream::StreamWriter writer(encoderStream);
				writer.WriteString(text);
			}

			utf8.Resize((vint)memoryStream.Size());
			if (utf8.Count() > 0)
			{
				memoryStream.SeekFromBegin(0);
				memoryStream.Read(&utf8[0], utf8.Count());
			}
		}

		WString DecodeUtf8(const void* buffer, vint size)
		{
			if (size == 0)
			{
				return WString::Empty;
			}

			stream::MemoryWrapperStream memoryStream((void*)buffer, size);
			stream::Utf8Decoder decoder;
			stream::DecoderStream decoderStream(memoryStream, decoder);
			stream::StreamReader reader(decoderStream);
			return reader.ReadToEnd();
		}

		vint HexValue(char c)
		{
			if ('0' <= c && c <= '9') return c - '0';
			if ('a' <= c && c <= 'f') return c - 'a' + 10;
			if ('A' <= c && c <= 'F') return c - 'A' + 10;
			return -1;
		}

		bool IsHttpNetworkProtocolPathCharacter(wchar_t c)
		{
			if (L'a' <= c && c <= L'z') return true;
			if (L'A' <= c && c <= L'Z') return true;
			if (L'0' <= c && c <= L'9') return true;
			switch (c)
			{
			case L'-': case L'.': case L'_': case L'~':
			case L'!': case L'$': case L'&': case L'\'': case L'(':
			case L')': case L'*': case L'+': case L',': case L';': case L'=':
			case L':': case L'@': case L'/':
				return true;
			default:
				return false;
			}
		}

		bool ValidateHttpNetworkProtocolPath(const WString& path, bool allowEmpty, bool rejectTrailingSlash)
		{
			if (path.Length() == 0) return allowEmpty;
			if (path[0] != L'/') return false;
			if (rejectTrailingSlash && path[path.Length() - 1] == L'/') return false;

			List<vuint8_t> decoded;
			for (vint i = 0; i < path.Length(); i++)
			{
				wchar_t c = path[i];
				if (c == L'%')
				{
					if (i + 2 >= path.Length()) return false;
					wchar_t highChar = path[i + 1];
					wchar_t lowChar = path[i + 2];
					if (highChar > 0x7F || lowChar > 0x7F) return false;
					vint high = HexValue((char)highChar);
					vint low = HexValue((char)lowChar);
					if (high == -1 || low == -1) return false;
					vuint8_t byte = (vuint8_t)(high * 16 + low);
					if (byte == 0 || byte == '/' || byte == '\\') return false;
					decoded.Add(byte);
					i += 2;
				}
				else
				{
					if (c > 0x7F || !IsHttpNetworkProtocolPathCharacter(c)) return false;
					decoded.Add((vuint8_t)c);
				}
			}

			WString ignored;
			return async_tcp_socket::DecodeStrictUtf8(
				decoded.Count() == 0 ? nullptr : &decoded[0],
				decoded.Count(),
				ignored
				);
		}
	}

	WString CreateHttpNetworkProtocolConnectBody(const WString& requestPath, const WString& responsePath)
	{
		CHECK_ERROR(requestPath.Length() > 0, L"CreateHttpNetworkProtocolConnectBody(const WString&, const WString&): The request path cannot be empty.");
		CHECK_ERROR(responsePath.Length() > 0, L"CreateHttpNetworkProtocolConnectBody(const WString&, const WString&): The response path cannot be empty.");
		CHECK_ERROR(requestPath.IndexOf(L';') == -1, L"CreateHttpNetworkProtocolConnectBody(const WString&, const WString&): The request path cannot contain a semicolon.");
		CHECK_ERROR(responsePath.IndexOf(L';') == -1, L"CreateHttpNetworkProtocolConnectBody(const WString&, const WString&): The response path cannot contain a semicolon.");
		return requestPath + L";" + responsePath;
	}

	bool ParseHttpNetworkProtocolConnectBody(const WString& body, WString& requestPath, WString& responsePath)
	{
		vint delimiter = body.IndexOf(L';');
		if (delimiter <= 0 || delimiter == body.Length() - 1) return false;
		if (body.Right(body.Length() - delimiter - 1).IndexOf(L';') != -1) return false;

		WString parsedRequestPath = body.Left(delimiter);
		WString parsedResponsePath = body.Right(body.Length() - delimiter - 1);
		requestPath = parsedRequestPath;
		responsePath = parsedResponsePath;
		return true;
	}

	bool ValidateHttpNetworkProtocolBaseUrl(const WString& baseUrl)
	{
		return ValidateHttpNetworkProtocolPath(baseUrl, true, true);
	}

	bool ValidateHttpNetworkProtocolEndpointPath(const WString& path)
	{
		return ValidateHttpNetworkProtocolPath(path, false, false);
	}

	bool IsValidHttpNetworkProtocolMessage(const WString& message)
	{
		if (message.Length() == 0) return false;
		for (vint i = 0; i < message.Length(); i++)
		{
			if (message[i] == 0) return false;
		}
		return true;
	}

	WString HttpUrlEncodeQuery(const WString& query)
	{
		Array<char> utf8;
		EncodeUtf8(query, utf8);

		Array<wchar_t> encoded(utf8.Count() * 3 + 1);
		wchar_t* writing = &encoded[0];
		for (vint i = 0; i < utf8.Count(); i++)
		{
			vuint8_t x = (vuint8_t)utf8[i];
			if ((L'a' <= x && x <= L'z') || (L'A' <= x && x <= L'Z') || (L'0' <= x && x <= L'9'))
			{
				*writing++ = (wchar_t)x;
			}
			else
			{
				*writing++ = L'%';
				*writing++ = L"0123456789ABCDEF"[x / 16];
				*writing++ = L"0123456789ABCDEF"[x % 16];
			}
		}
		*writing = 0;
		return &encoded[0];
	}

	WString HttpUrlDecodeQuery(const WString& query)
	{
		Array<char> encoded;
		EncodeUtf8(query, encoded);

		List<char> utf8;
		for (vint i = 0; i < encoded.Count(); i++)
		{
			char c = encoded[i];
			if (c == '%' && i + 2 < encoded.Count())
			{
				vint high = HexValue(encoded[i + 1]);
				vint low = HexValue(encoded[i + 2]);
				if (high != -1 && low != -1)
				{
					utf8.Add((char)(high * 16 + low));
					i += 2;
					continue;
				}
			}

			utf8.Add(c == '+' ? ' ' : c);
		}

		return utf8.Count() == 0
			? WString::Empty
			: DecodeUtf8(&utf8[0], utf8.Count());
	}
}

namespace vl::inter_process::windows_http
{
	void HttpRequest::SetBodyUtf8(const WString& bodyString)
	{
		EncodeUtf8(bodyString, body);
	}

	bool HttpResponse::TryGetBodyUtf8(WString& bodyString) const
	{
		return ::vl::inter_process::async_tcp_socket::DecodeStrictUtf8(
			body.Count() == 0 ? nullptr : reinterpret_cast<const vuint8_t*>(&body[0]),
			body.Count(),
			bodyString
			);
	}

	WString HttpResponse::GetBodyUtf8() const
	{
		return body.Count() == 0
			? WString::Empty
			: DecodeUtf8(&body[0], body.Count());
	}
}

namespace vl::inter_process
{
	windows_http::HttpRequest CreateHttpNetworkProtocolConnectRequest(const WString& target)
	{
		windows_http::HttpRequest request;
		request.method = L"GET";
		request.query = target;
		request.acceptTypes.Add(HttpNetworkProtocolContentType);
		return request;
	}

	windows_http::HttpRequest CreateHttpNetworkProtocolReceiveRequest(const WString& target)
	{
		windows_http::HttpRequest request;
		request.method = L"POST";
		request.query = target;
		request.acceptTypes.Add(HttpNetworkProtocolContentType);
		request.extraHeaders.Add(L"Content-Length", L"0");
		return request;
	}

	windows_http::HttpRequest CreateHttpNetworkProtocolSendRequest(const WString& target, const Array<char>& body)
	{
		windows_http::HttpRequest request;
		request.method = L"POST";
		request.query = target;
		request.acceptTypes.Add(HttpNetworkProtocolContentType);
		request.contentType = HttpNetworkProtocolContentType;
		request.body.Resize(body.Count());
		for (vint i = 0; i < body.Count(); i++)
		{
			request.body[i] = body[i];
		}
		return request;
	}
}
