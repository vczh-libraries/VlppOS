#include "NetworkProtocolHttp.h"
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

	WString HttpResponse::GetBodyUtf8() const
	{
		return body.Count() == 0
			? WString::Empty
			: DecodeUtf8(&body[0], body.Count());
	}
}

