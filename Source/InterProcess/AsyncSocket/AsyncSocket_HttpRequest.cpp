/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Async Socket HTTP/1.1 Connection

***********************************************************************/

#include "AsyncSocket_HttpRequest.h"

#include <chrono>
#include <cstring>
#include <limits>

namespace vl::inter_process::async_tcp_socket
{
	using namespace collections;

	namespace
	{
		constexpr vint HttpWireMessageSizeLimit = 128 * 1024 * 1024;
		constexpr vint HttpChunkCountLimit = 64 * 1024;

		bool IsOws(vuint8_t c)
		{
			return c == ' ' || c == '\t';
		}

		bool IsDigit(vuint8_t c)
		{
			return c >= '0' && c <= '9';
		}

		bool IsHexDigit(vuint8_t c)
		{
			return IsDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
		}

		vuint8_t HexDigitValue(vuint8_t c)
		{
			if (IsDigit(c)) return c - '0';
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return c - 'a' + 10;
		}

		bool IsTokenCharacter(vuint8_t c)
		{
			if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
			{
				return true;
			}
			switch (c)
			{
			case '!': case '#': case '$': case '%': case '&': case '\'': case '*': case '+':
			case '-': case '.': case '^': case '_': case '`': case '|': case '~':
				return true;
			default:
				return false;
			}
		}

		bool IsFieldValueCharacter(vuint8_t c)
		{
			return c == '\t' || (c >= 0x20 && c != 0x7F);
		}

		bool IsQuotedTextCharacter(vuint8_t c)
		{
			return c == '\t' || c == ' ' || c == '!' || (c >= '#' && c <= '[') || (c >= ']' && c != 0x7F);
		}

		vint FindCrlf(const vuint8_t* buffer, vint begin, vint end)
		{
			for (vint i = begin; i + 1 < end; i++)
			{
				if (buffer[i] == '\r' && buffer[i + 1] == '\n')
				{
					return i;
				}
			}
			return -1;
		}

		WString CopyAscii(const vuint8_t* buffer, vint begin, vint end, bool lowercase)
		{
			if (begin == end)
			{
				return L"";
			}
			Array<wchar_t> text(end - begin);
			for (vint i = begin; i < end; i++)
			{
				auto c = buffer[i];
				if (lowercase && c >= 'A' && c <= 'Z')
				{
					c += 'a' - 'A';
				}
				text[i - begin] = (wchar_t)c;
			}
			return WString::CopyFrom(&text[0], text.Count());
		}

		bool IsAsciiEqual(const WString& text, const wchar_t* expected)
		{
			return text == expected;
		}

		bool ParseQuotedString(const vuint8_t* buffer, vint& reading, vint end)
		{
			if (reading >= end || buffer[reading] != '"')
			{
				return false;
			}
			reading++;
			while (reading < end)
			{
				auto c = buffer[reading++];
				if (c == '"')
				{
					return true;
				}
				if (c == '\\')
				{
					if (reading >= end || !IsFieldValueCharacter(buffer[reading]))
					{
						return false;
					}
					reading++;
				}
				else if (!IsQuotedTextCharacter(c))
				{
					return false;
				}
			}
			return false;
		}

		bool ParseToken(const vuint8_t* buffer, vint& reading, vint end, WString* output = nullptr)
		{
			vint begin = reading;
			while (reading < end && IsTokenCharacter(buffer[reading]))
			{
				reading++;
			}
			if (reading == begin)
			{
				return false;
			}
			if (output)
			{
				*output = CopyAscii(buffer, begin, reading, true);
			}
			return true;
		}

		bool ParseSemicolonParameters(const vuint8_t* buffer, vint& reading, vint end)
		{
			while (true)
			{
				while (reading < end && IsOws(buffer[reading])) reading++;
				if (reading == end)
				{
					return true;
				}
				if (buffer[reading++] != ';')
				{
					return false;
				}
				while (reading < end && IsOws(buffer[reading])) reading++;
				if (!ParseToken(buffer, reading, end))
				{
					return false;
				}
				while (reading < end && IsOws(buffer[reading])) reading++;
				if (reading < end && buffer[reading] == '=')
				{
					reading++;
					while (reading < end && IsOws(buffer[reading])) reading++;
					if (reading < end && buffer[reading] == '"')
					{
						if (!ParseQuotedString(buffer, reading, end)) return false;
					}
					else if (!ParseToken(buffer, reading, end))
					{
						return false;
					}
				}
			}
		}

		bool ValidateChunkSizeLinePrefix(const vuint8_t* buffer, vint begin, vint end)
		{
			vint reading = begin;
			if (reading == end) return true;
			if (!IsHexDigit(buffer[reading])) return false;
			vuint64_t chunkSize = 0;
			while (reading < end && IsHexDigit(buffer[reading]))
			{
				auto digit = (vuint64_t)HexDigitValue(buffer[reading++]);
				if (chunkSize > ((std::numeric_limits<vuint64_t>::max)() - digit) / 16) return false;
				chunkSize = chunkSize * 16 + digit;
				if (chunkSize > (vuint64_t)HttpBodySizeLimit) return false;
			}
			while (true)
			{
				while (reading < end && IsOws(buffer[reading])) reading++;
				if (reading == end) return true;
				if (buffer[reading++] != ';') return false;
				while (reading < end && IsOws(buffer[reading])) reading++;
				if (reading == end) return true;
				vint nameBegin = reading;
				while (reading < end && IsTokenCharacter(buffer[reading])) reading++;
				if (reading == nameBegin) return false;
				while (reading < end && IsOws(buffer[reading])) reading++;
				if (reading == end) return true;
				if (buffer[reading] != '=') continue;
				reading++;
				while (reading < end && IsOws(buffer[reading])) reading++;
				if (reading == end) return true;
				if (buffer[reading] == '"')
				{
					reading++;
					bool closed = false;
					while (reading < end)
					{
						auto c = buffer[reading++];
						if (c == '"')
						{
							closed = true;
							break;
						}
						if (c == '\\')
						{
							if (reading == end) return true;
							if (!IsFieldValueCharacter(buffer[reading++])) return false;
						}
						else if (!IsQuotedTextCharacter(c))
						{
							return false;
						}
					}
					if (!closed) return true;
				}
				else
				{
					vint valueBegin = reading;
					while (reading < end && IsTokenCharacter(buffer[reading])) reading++;
					if (reading == valueBegin) return false;
				}
			}
		}

		bool ValidateCompleteChunkSizeLine(const vuint8_t* buffer, vint begin, vint end)
		{
			vint reading = begin;
			vuint64_t chunkSize = 0;
			while (reading < end && IsHexDigit(buffer[reading]))
			{
				auto digit = (vuint64_t)HexDigitValue(buffer[reading++]);
				if (chunkSize > ((std::numeric_limits<vuint64_t>::max)() - digit) / 16) return false;
				chunkSize = chunkSize * 16 + digit;
			}
			if (reading == begin || chunkSize > (vuint64_t)HttpBodySizeLimit) return false;
			if (reading < end)
			{
				vint firstExtension = reading;
				while (firstExtension < end && IsOws(buffer[firstExtension])) firstExtension++;
				if (firstExtension == end || buffer[firstExtension] != ';') return false;
			}
			return ParseSemicolonParameters(buffer, reading, end);
		}

		bool ParseFieldLine(const vuint8_t* buffer, vint begin, vint end, HttpField& field)
		{
			vint colon = -1;
			for (vint i = begin; i < end; i++)
			{
				if (buffer[i] == ':')
				{
					colon = i;
					break;
				}
				if (!IsTokenCharacter(buffer[i]))
				{
					return false;
				}
			}
			if (colon == begin || colon == -1)
			{
				return false;
			}
			for (vint i = begin; i < colon; i++)
			{
				if (!IsTokenCharacter(buffer[i])) return false;
			}

			vint valueBegin = colon + 1;
			vint valueEnd = end;
			while (valueBegin < valueEnd && IsOws(buffer[valueBegin])) valueBegin++;
			while (valueBegin < valueEnd && IsOws(buffer[valueEnd - 1])) valueEnd--;
			for (vint i = valueBegin; i < valueEnd; i++)
			{
				if (!IsFieldValueCharacter(buffer[i])) return false;
			}

			field.name = CopyAscii(buffer, begin, colon, true);
			field.value.Resize(valueEnd - valueBegin);
			if (valueEnd > valueBegin)
			{
				std::memcpy(&field.value[0], buffer + valueBegin, (size_t)(valueEnd - valueBegin));
			}
			return true;
		}

		bool ParseUnsignedDecimal(const Array<vuint8_t>& value, vint begin, vint end, vuint64_t& number)
		{
			if (begin == end) return false;
			number = 0;
			for (vint i = begin; i < end; i++)
			{
				if (!IsDigit(value[i])) return false;
				auto digit = (vuint64_t)(value[i] - '0');
				if (number > ((std::numeric_limits<vuint64_t>::max)() - digit) / 10)
				{
					return false;
				}
				number = number * 10 + digit;
			}
			return true;
		}

		bool ParseContentLength(const Array<vuint8_t>& value, bool& initialized, vuint64_t& contentLength, vint& valueCount)
		{
			vint reading = 0;
			while (true)
			{
				while (reading < value.Count() && IsOws(value[reading])) reading++;
				vint begin = reading;
				while (reading < value.Count() && IsDigit(value[reading])) reading++;
				vint end = reading;
				while (reading < value.Count() && IsOws(value[reading])) reading++;
				vuint64_t number = 0;
				if (!ParseUnsignedDecimal(value, begin, end, number))
				{
					return false;
				}
				valueCount++;
				if (!initialized)
				{
					initialized = true;
					contentLength = number;
				}
				else if (contentLength != number)
				{
					return false;
				}
				if (reading == value.Count()) return true;
				if (value[reading++] != ',') return false;
				if (reading == value.Count()) return false;
			}
		}

		bool ParseTransferCodings(const Array<vuint8_t>& value, List<WString>& codings, bool& hasParameters)
		{
			vint reading = 0;
			while (true)
			{
				while (reading < value.Count() && IsOws(value[reading])) reading++;
				WString coding;
				if (!ParseToken(value.Count() == 0 ? nullptr : &value[0], reading, value.Count(), &coding))
				{
					return false;
				}
				codings.Add(coding);
				while (reading < value.Count() && IsOws(value[reading])) reading++;
				while (reading < value.Count() && value[reading] == ';')
				{
					hasParameters = true;
					reading++;
					while (reading < value.Count() && IsOws(value[reading])) reading++;
					if (!ParseToken(&value[0], reading, value.Count())) return false;
					while (reading < value.Count() && IsOws(value[reading])) reading++;
					if (reading >= value.Count() || value[reading] != '=')
					{
						return false;
					}
					else
					{
						reading++;
						while (reading < value.Count() && IsOws(value[reading])) reading++;
						if (reading < value.Count() && value[reading] == '"')
						{
							if (!ParseQuotedString(&value[0], reading, value.Count())) return false;
						}
						else if (!ParseToken(&value[0], reading, value.Count()))
						{
							return false;
						}
					}
					while (reading < value.Count() && IsOws(value[reading])) reading++;
				}
				if (reading == value.Count()) return true;
				if (value[reading++] != ',' || reading == value.Count()) return false;
			}
		}

		bool HasConnectionClose(const Array<vuint8_t>& value)
		{
			vint reading = 0;
			while (reading < value.Count())
			{
				while (reading < value.Count() && IsOws(value[reading])) reading++;
				vint begin = reading;
				while (reading < value.Count() && value[reading] != ',') reading++;
				vint end = reading;
				while (begin < end && IsOws(value[begin])) begin++;
				while (begin < end && IsOws(value[end - 1])) end--;
				if (end - begin == 5)
				{
					const wchar_t* close = L"close";
					bool matched = true;
					for (vint i = 0; i < 5; i++)
					{
						auto c = value[begin + i];
						if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
						if (c != close[i]) matched = false;
					}
					if (matched) return true;
				}
				if (reading < value.Count()) reading++;
			}
			return false;
		}

		bool ParseHttpVersion(const vuint8_t* buffer, vint begin, vint end, HttpVersion& version)
		{
			const wchar_t* prefix = L"HTTP/";
			if (end - begin < 8) return false;
			for (vint i = 0; i < 5; i++)
			{
				if (buffer[begin + i] != prefix[i]) return false;
			}
			vint reading = begin + 5;
			vuint64_t major = 0;
			vuint64_t minor = 0;
			vint majorBegin = reading;
			while (reading < end && IsDigit(buffer[reading]))
			{
				auto digit = (vuint64_t)(buffer[reading++] - '0');
				if (major > ((std::numeric_limits<vuint64_t>::max)() - digit) / 10) return false;
				major = major * 10 + digit;
			}
			if (reading == majorBegin || reading >= end || buffer[reading++] != '.') return false;
			vint minorBegin = reading;
			while (reading < end && IsDigit(buffer[reading]))
			{
				auto digit = (vuint64_t)(buffer[reading++] - '0');
				if (minor > ((std::numeric_limits<vuint64_t>::max)() - digit) / 10) return false;
				minor = minor * 10 + digit;
			}
			if (reading != end || reading == minorBegin || major > (vuint64_t)(std::numeric_limits<vint>::max)() || minor > (vuint64_t)(std::numeric_limits<vint>::max)())
			{
				return false;
			}
			version.major = (vint)major;
			version.minor = (vint)minor;
			return true;
		}

		bool ParseRequestLine(const vuint8_t* buffer, vint end, HttpVersion& version, WString& method, WString& target)
		{
			vint firstSpace = -1;
			vint secondSpace = -1;
			for (vint i = 0; i < end; i++)
			{
				if (buffer[i] == ' ')
				{
					if (firstSpace == -1) firstSpace = i;
					else if (secondSpace == -1) secondSpace = i;
					else return false;
				}
			}
			if (firstSpace <= 0 || secondSpace <= firstSpace + 1 || secondSpace + 1 >= end) return false;
			for (vint i = 0; i < firstSpace; i++) if (!IsTokenCharacter(buffer[i])) return false;
			for (vint i = firstSpace + 1; i < secondSpace; i++)
			{
				if (buffer[i] < 0x21 || buffer[i] > 0x7E) return false;
			}
			method = CopyAscii(buffer, 0, firstSpace, false);
			target = CopyAscii(buffer, firstSpace + 1, secondSpace, false);
			return ParseHttpVersion(buffer, secondSpace + 1, end, version);
		}

		bool ParseStatusLine(const vuint8_t* buffer, vint end, HttpVersion& version, vint& statusCode, WString& reason)
		{
			vint firstSpace = -1;
			for (vint i = 0; i < end; i++)
			{
				if (buffer[i] == ' ')
				{
					firstSpace = i;
					break;
				}
			}
			if (firstSpace == -1 || !ParseHttpVersion(buffer, 0, firstSpace, version)) return false;
			if (firstSpace + 4 >= end || !IsDigit(buffer[firstSpace + 1]) || !IsDigit(buffer[firstSpace + 2]) || !IsDigit(buffer[firstSpace + 3])) return false;
			if (buffer[firstSpace + 4] != ' ') return false;
			statusCode = (buffer[firstSpace + 1] - '0') * 100 + (buffer[firstSpace + 2] - '0') * 10 + buffer[firstSpace + 3] - '0';
			if (statusCode < 200 || statusCode > 599) return false;
			vint reasonBegin = firstSpace + 5;
			for (vint i = reasonBegin; i < end; i++)
			{
				if (buffer[i] < 0x20 || buffer[i] > 0x7E) return false;
			}
			reason = CopyAscii(buffer, reasonBegin, end, false);
			return true;
		}

		enum class HttpBodyDetailedParsingResult
		{
			Succeeded,
			Incomplete,
			BadRequest,
			PayloadTooLarge,
			TrailerFieldsTooLarge,
		};

		HttpBodyDetailedParsingResult ParseHttpRequestBodyToChunksDetailed(
			const vuint8_t* buffer,
			vint availableBytes,
			HttpBody& output,
			vint& consumedBytes
			);

		enum class HttpMessageParsingResult
		{
			Succeeded,
			Incomplete,
			BadRequest,
			PayloadTooLarge,
			UriTooLong,
			ExpectationFailed,
			RequestHeaderFieldsTooLarge,
			NotImplemented,
			HttpVersionNotSupported,
		};

		HttpRequestFailure GetHttpRequestFailure(HttpMessageParsingResult result)
		{
			switch (result)
			{
			case HttpMessageParsingResult::PayloadTooLarge:
				return HttpRequestFailure::PayloadTooLarge;
			case HttpMessageParsingResult::UriTooLong:
				return HttpRequestFailure::UriTooLong;
			case HttpMessageParsingResult::ExpectationFailed:
				return HttpRequestFailure::ExpectationFailed;
			case HttpMessageParsingResult::RequestHeaderFieldsTooLarge:
				return HttpRequestFailure::RequestHeaderFieldsTooLarge;
			case HttpMessageParsingResult::NotImplemented:
				return HttpRequestFailure::NotImplemented;
			case HttpMessageParsingResult::HttpVersionNotSupported:
				return HttpRequestFailure::HttpVersionNotSupported;
			default:
				return HttpRequestFailure::BadRequest;
			}
		}

		bool HasHeader(const List<HttpField>& fields, const wchar_t* name)
		{
			for (auto&& field : fields)
			{
				if (IsAsciiEqual(field.name, name)) return true;
			}
			return false;
		}

		bool TryGetHttpRequestLineSize(const WString& method, const WString& requestTarget, vint& size)
		{
			if (
				method.Length() > HttpRequestLineSizeLimit - 10 ||
				requestTarget.Length() > HttpRequestLineSizeLimit - 10 - method.Length()
				)
			{
				return false;
			}
			size = 10 + method.Length() + requestTarget.Length();
			return true;
		}

		HttpMessageParsingResult ParseHttpMessage(
			const vuint8_t* buffer,
			vint availableBytes,
			bool requestMessage,
			const WString& responseToMethod,
			WString& parsedRequestMethod,
			Ptr<HttpRequest>& request,
			Ptr<HttpResponse>& response,
			vint& consumedBytes,
			bool& connectionClose
			)
		{
			consumedBytes = 0;
			connectionClose = false;
			parsedRequestMethod = L"";
			vint startLineEnd = FindCrlf(buffer, 0, availableBytes);
			if (startLineEnd == -1)
			{
				auto possibleLineBytes = availableBytes > 0 && buffer[availableBytes - 1] == '\r' ? availableBytes - 1 : availableBytes;
				return possibleLineBytes > HttpRequestLineSizeLimit
					? (requestMessage ? HttpMessageParsingResult::UriTooLong : HttpMessageParsingResult::BadRequest)
					: HttpMessageParsingResult::Incomplete;
			}
			if (startLineEnd > HttpRequestLineSizeLimit)
			{
				return requestMessage ? HttpMessageParsingResult::UriTooLong : HttpMessageParsingResult::BadRequest;
			}

			HttpVersion version;
			WString method;
			WString target;
			vint statusCode = 0;
			WString reason;
			if (requestMessage)
			{
				if (!ParseRequestLine(buffer, startLineEnd, version, method, target)) return HttpMessageParsingResult::BadRequest;
				parsedRequestMethod = method;
			}
			else
			{
				if (!ParseStatusLine(buffer, startLineEnd, version, statusCode, reason)) return HttpMessageParsingResult::BadRequest;
			}
			if (version.major != 1 || version.minor != 1)
			{
				return HttpMessageParsingResult::HttpVersionNotSupported;
			}

			List<HttpField> headers;
			vint headersBegin = startLineEnd + 2;
			vint reading = headersBegin;
			vint bodyBegin = -1;
			while (true)
			{
				vint lineEnd = FindCrlf(buffer, reading, availableBytes);
				if (lineEnd == -1)
				{
					return availableBytes - headersBegin >= HttpHeaderBlockSizeLimit
						? (requestMessage ? HttpMessageParsingResult::RequestHeaderFieldsTooLarge : HttpMessageParsingResult::BadRequest)
						: HttpMessageParsingResult::Incomplete;
				}
				if (lineEnd + 2 - headersBegin > HttpHeaderBlockSizeLimit)
				{
					return requestMessage ? HttpMessageParsingResult::RequestHeaderFieldsTooLarge : HttpMessageParsingResult::BadRequest;
				}
				if (lineEnd == reading)
				{
					bodyBegin = lineEnd + 2;
					break;
				}
				HttpField field;
				if (!ParseFieldLine(buffer, reading, lineEnd, field)) return HttpMessageParsingResult::BadRequest;
				headers.Add(std::move(field));
				reading = lineEnd + 2;
			}

			HttpFraming framing;
			auto framingResult = AnalyzeHttpFraming(headers, framing);
			if (framingResult == HttpFramingAnalysisResult::Invalid) return HttpMessageParsingResult::BadRequest;
			if (framingResult == HttpFramingAnalysisResult::UnsupportedTransferCoding) return HttpMessageParsingResult::NotImplemented;
			if (requestMessage && HasHeader(headers, L"expect")) return HttpMessageParsingResult::ExpectationFailed;
			auto hasContentLength = framing.kind == HttpFramingKind::ContentLength;
			auto hasTransferEncoding = framing.kind == HttpFramingKind::Chunked;

			auto headResponse = !requestMessage && responseToMethod == L"HEAD";
			auto noContentResponse = !requestMessage && statusCode == 204;
			auto notModifiedResponse = !requestMessage && statusCode == 304;
			auto responseWithoutBody = headResponse || noContentResponse || notModifiedResponse;
			if (noContentResponse && (hasContentLength || hasTransferEncoding)) return HttpMessageParsingResult::BadRequest;
			if (notModifiedResponse && hasTransferEncoding) return HttpMessageParsingResult::BadRequest;
			if (!requestMessage && !responseWithoutBody && !hasContentLength && !hasTransferEncoding) return HttpMessageParsingResult::BadRequest;

			HttpBody body;
			vint bodyBytes = 0;
			if (!responseWithoutBody && hasTransferEncoding)
			{
				auto result = ParseHttpRequestBodyToChunksDetailed(buffer + bodyBegin, availableBytes - bodyBegin, body, bodyBytes);
				if (result == HttpBodyDetailedParsingResult::Incomplete) return HttpMessageParsingResult::Incomplete;
				if (result == HttpBodyDetailedParsingResult::PayloadTooLarge) return HttpMessageParsingResult::PayloadTooLarge;
				if (result == HttpBodyDetailedParsingResult::TrailerFieldsTooLarge) return HttpMessageParsingResult::RequestHeaderFieldsTooLarge;
				if (result == HttpBodyDetailedParsingResult::BadRequest) return HttpMessageParsingResult::BadRequest;
			}
			else if (!responseWithoutBody && hasContentLength)
			{
				if (framing.contentLength > (vuint64_t)HttpBodySizeLimit) return HttpMessageParsingResult::PayloadTooLarge;
				bodyBytes = (vint)framing.contentLength;
				if (availableBytes - bodyBegin < bodyBytes) return HttpMessageParsingResult::Incomplete;
				if (bodyBytes > 0)
				{
					HttpBodyChunk chunk;
					chunk.data.Resize(bodyBytes);
					std::memcpy(&chunk.data[0], buffer + bodyBegin, (size_t)bodyBytes);
					body.chunks.Add(std::move(chunk));
				}
			}

			consumedBytes = bodyBegin + bodyBytes;
			connectionClose = framing.connectionClose;
			if (requestMessage)
			{
				request = Ptr(new HttpRequest);
				request->version = version;
				request->method = method;
				request->requestTarget = target;
				request->headers = std::move(headers);
				request->body = std::move(body);
			}
			else
			{
				response = Ptr(new HttpResponse);
				response->version = version;
				response->statusCode = statusCode;
				response->reason = reason;
				response->headers = std::move(headers);
				response->body = std::move(body);
			}
			return HttpMessageParsingResult::Succeeded;
		}

		bool ValidateField(const HttpField& field, bool trailer)
		{
			if (field.name.Length() == 0) return false;
			for (vint i = 0; i < field.name.Length(); i++)
			{
				auto c = field.name[i];
				if ((vuint32_t)c > 0x7F || !IsTokenCharacter((vuint8_t)c) || (c >= L'A' && c <= L'Z')) return false;
			}
			for (auto c : field.value)
			{
				if (!IsFieldValueCharacter(c)) return false;
			}
			if (trailer && (field.name == L"content-length" || field.name == L"transfer-encoding")) return false;
			return true;
		}

		void AppendAscii(List<vuint8_t>& bytes, const wchar_t* text)
		{
			while (*text)
			{
				CHECK_ERROR(*text <= 0x7F, L"HTTP serialization requires ASCII protocol text.");
				bytes.Add((vuint8_t)*text++);
			}
		}

		void AppendAscii(List<vuint8_t>& bytes, const WString& text)
		{
			for (vint i = 0; i < text.Length(); i++)
			{
				CHECK_ERROR(text[i] <= 0x7F, L"HTTP serialization requires ASCII protocol text.");
				bytes.Add((vuint8_t)text[i]);
			}
		}

		void AppendDecimal(List<vuint8_t>& bytes, vint number)
		{
			CHECK_ERROR(number >= 0, L"HTTP serialization requires a non-negative decimal number.");
			vuint8_t digits[32];
			auto count = 0;
			auto value = (vuint64_t)number;
			do
			{
				digits[count++] = (vuint8_t)('0' + value % 10);
				value /= 10;
			} while (value > 0);
			for (vint i = count - 1; i >= 0; i--) bytes.Add(digits[i]);
		}

		void AppendHexadecimal(List<vuint8_t>& bytes, vint number)
		{
			CHECK_ERROR(number > 0, L"HTTP chunk serialization requires a positive chunk size.");
			vuint8_t digits[32];
			auto count = 0;
			auto value = (vuint64_t)number;
			const wchar_t* hex = L"0123456789abcdef";
			while (value > 0)
			{
				digits[count++] = (vuint8_t)hex[value % 16];
				value /= 16;
			}
			for (vint i = count - 1; i >= 0; i--) bytes.Add(digits[i]);
		}

		void AppendCrlf(List<vuint8_t>& bytes)
		{
			bytes.Add('\r');
			bytes.Add('\n');
		}

		void AppendField(List<vuint8_t>& bytes, const HttpField& field)
		{
			AppendAscii(bytes, field.name);
			bytes.Add(':');
			bytes.Add(' ');
			for (auto c : field.value) bytes.Add(c);
			AppendCrlf(bytes);
		}

		vint DecimalDigitCount(vint number)
		{
			vint count = 1;
			while (number >= 10)
			{
				number /= 10;
				count++;
			}
			return count;
		}

		vint HexadecimalDigitCount(vint number)
		{
			vint count = 1;
			while (number >= 16)
			{
				number /= 16;
				count++;
			}
			return count;
		}

		vint AsciiLength(const wchar_t* text)
		{
			vint count = 0;
			while (text[count]) count++;
			return count;
		}

		void AddBoundedSize(vint& total, vint adding, vint limit, const wchar_t* error)
		{
			CHECK_ERROR(adding >= 0 && total <= limit - adding, error);
			total += adding;
		}

		vint ValidateBody(const HttpBody& body)
		{
			CHECK_ERROR(body.chunks.Count() <= HttpChunkCountLimit, L"HTTP body contains too many chunks.");
			vint total = 0;
			for (auto&& chunk : body.chunks)
			{
				CHECK_ERROR(chunk.data.Count() > 0, L"HTTP body chunks must contain at least one octet.");
				CHECK_ERROR(total <= HttpBodySizeLimit - chunk.data.Count(), L"HTTP body exceeds the configured size limit.");
				total += chunk.data.Count();
			}
			for (auto&& trailer : body.trailers)
			{
				CHECK_ERROR(ValidateField(trailer, true), L"HTTP body contains an invalid trailer field.");
			}
			return total;
		}

		Ptr<AsyncSocketBuffer> SerializeHttpMessage(HttpRequest* request, HttpResponse* response, const WString& responseToMethod, bool& connectionClose)
		{
			auto requestMessage = request != nullptr;
			CHECK_ERROR(requestMessage != (response != nullptr), L"HTTP serialization requires exactly one message.");
			auto&& version = requestMessage ? request->version : response->version;
			auto&& headers = requestMessage ? request->headers : response->headers;
			auto&& body = requestMessage ? request->body : response->body;
			CHECK_ERROR(version.major == 1 && version.minor == 1, L"HTTP serialization only supports HTTP/1.1.");

			vint startLineSize = 0;
			if (requestMessage)
			{
				switch (ValidateHttpRequestLine(request->method, request->requestTarget))
				{
				case HttpRequestLineValidationResult::InvalidMethod:
					CHECK_FAIL(L"HTTP request serialization received an invalid method.");
				case HttpRequestLineValidationResult::InvalidRequestTarget:
					CHECK_FAIL(L"HTTP request serialization received an invalid request target.");
				case HttpRequestLineValidationResult::TooLong:
					CHECK_FAIL(L"HTTP start line exceeds the configured size limit.");
				default:
					break;
				}
				CHECK_ERROR(TryGetHttpRequestLineSize(request->method, request->requestTarget, startLineSize), L"HTTP start line exceeds the configured size limit.");
			}
			else
			{
				CHECK_ERROR(response->statusCode >= 200 && response->statusCode <= 599, L"HTTP response serialization only supports final status codes from 200 through 599.");
				for (vint i = 0; i < response->reason.Length(); i++)
				{
					CHECK_ERROR(response->reason[i] >= 0x20 && response->reason[i] <= 0x7E, L"HTTP response serialization received an invalid reason phrase.");
				}
				startLineSize = 13;
				AddBoundedSize(startLineSize, response->reason.Length(), HttpRequestLineSizeLimit, L"HTTP start line exceeds the configured size limit.");
			}

			vint headerBlockSize = 2;
			for (auto&& field : headers)
			{
				CHECK_ERROR(ValidateField(field, false), L"HTTP serialization received an invalid header field.");
				AddBoundedSize(headerBlockSize, field.name.Length(), HttpHeaderBlockSizeLimit, L"HTTP header block exceeds the configured size limit.");
				AddBoundedSize(headerBlockSize, 2, HttpHeaderBlockSizeLimit, L"HTTP header block exceeds the configured size limit.");
				AddBoundedSize(headerBlockSize, field.value.Count(), HttpHeaderBlockSizeLimit, L"HTTP header block exceeds the configured size limit.");
				AddBoundedSize(headerBlockSize, 2, HttpHeaderBlockSizeLimit, L"HTTP header block exceeds the configured size limit.");
			}

			HttpFraming framing;
			CHECK_ERROR(AnalyzeHttpFraming(headers, framing) == HttpFramingAnalysisResult::Succeeded, L"HTTP serialization received invalid, ambiguous, or unsupported body framing.");
			auto bodySize = ValidateBody(body);
			auto headResponse = !requestMessage && responseToMethod == L"HEAD";
			auto noContentResponse = !requestMessage && response->statusCode == 204;
			auto notModifiedResponse = !requestMessage && response->statusCode == 304;
			auto suppressBody = headResponse || noContentResponse || notModifiedResponse;
			auto hasContentLength = framing.kind == HttpFramingKind::ContentLength;
			auto hasTransferEncoding = framing.kind == HttpFramingKind::Chunked;
			auto chunked = hasTransferEncoding;
			auto generateContentLength = false;
			auto generateTransferEncoding = false;
			if (noContentResponse)
			{
				CHECK_ERROR(body.chunks.Count() == 0 && body.trailers.Count() == 0, L"An HTTP 204 response cannot contain a body.");
				CHECK_ERROR(!hasContentLength && !hasTransferEncoding, L"An HTTP 204 response cannot contain body framing.");
				chunked = false;
			}
			else if (notModifiedResponse)
			{
				CHECK_ERROR(body.chunks.Count() == 0 && body.trailers.Count() == 0, L"An HTTP 304 response cannot contain a body.");
				CHECK_ERROR(!hasTransferEncoding, L"An HTTP 304 response cannot contain Transfer-Encoding.");
				chunked = false;
			}
			else if (!hasContentLength && !hasTransferEncoding)
			{
				chunked = body.chunks.Count() > 1 || body.trailers.Count() > 0;
				generateTransferEncoding = chunked;
				generateContentLength = !chunked && (!requestMessage || body.chunks.Count() > 0);
			}
			if (!noContentResponse && !notModifiedResponse && !chunked)
			{
				CHECK_ERROR(body.trailers.Count() == 0 && body.chunks.Count() <= 1, L"A fixed HTTP body cannot contain multiple chunks or trailers.");
				if (hasContentLength)
				{
					if (!headResponse || body.chunks.Count() > 0)
					{
						CHECK_ERROR(framing.contentLength == (vuint64_t)bodySize, L"HTTP Content-Length does not match the supplied body.");
					}
				}
				else if (!generateContentLength && !headResponse)
				{
					CHECK_ERROR(requestMessage && bodySize == 0, L"An HTTP response requires explicit body framing.");
				}
			}
			if (generateContentLength)
			{
				AddBoundedSize(headerBlockSize, AsciiLength(L"content-length: ") + DecimalDigitCount(bodySize) + 2, HttpHeaderBlockSizeLimit, L"HTTP header block exceeds the configured size limit.");
			}
			if (generateTransferEncoding)
			{
				AddBoundedSize(headerBlockSize, AsciiLength(L"transfer-encoding: chunked\r\n"), HttpHeaderBlockSizeLimit, L"HTTP header block exceeds the configured size limit.");
			}

			vint trailerBlockSize = 2;
			for (auto&& trailer : body.trailers)
			{
				AddBoundedSize(trailerBlockSize, trailer.name.Length(), HttpTrailerBlockSizeLimit, L"HTTP trailer block exceeds the configured size limit.");
				AddBoundedSize(trailerBlockSize, 2, HttpTrailerBlockSizeLimit, L"HTTP trailer block exceeds the configured size limit.");
				AddBoundedSize(trailerBlockSize, trailer.value.Count(), HttpTrailerBlockSizeLimit, L"HTTP trailer block exceeds the configured size limit.");
				AddBoundedSize(trailerBlockSize, 2, HttpTrailerBlockSizeLimit, L"HTTP trailer block exceeds the configured size limit.");
			}
			vint wireSize = startLineSize;
			AddBoundedSize(wireSize, 2, HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
			AddBoundedSize(wireSize, headerBlockSize, HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
			if (!suppressBody && chunked)
			{
				for (auto&& chunk : body.chunks)
				{
					AddBoundedSize(wireSize, HexadecimalDigitCount(chunk.data.Count()) + 4, HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
					AddBoundedSize(wireSize, chunk.data.Count(), HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
				}
				AddBoundedSize(wireSize, 3, HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
				AddBoundedSize(wireSize, trailerBlockSize, HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
			}
			else if (!suppressBody)
			{
				AddBoundedSize(wireSize, bodySize, HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
			}

			List<vuint8_t> bytes;
			if (requestMessage)
			{
				AppendAscii(bytes, request->method);
				bytes.Add(' ');
				AppendAscii(bytes, request->requestTarget);
				bytes.Add(' ');
			}
			AppendAscii(bytes, L"HTTP/");
			AppendDecimal(bytes, version.major);
			bytes.Add('.');
			AppendDecimal(bytes, version.minor);
			if (!requestMessage)
			{
				bytes.Add(' ');
				AppendDecimal(bytes, response->statusCode);
				bytes.Add(' ');
				AppendAscii(bytes, response->reason);
			}
			CHECK_ERROR(bytes.Count() <= HttpRequestLineSizeLimit, L"HTTP start line exceeds the configured size limit.");
			AppendCrlf(bytes);

			for (auto&& field : headers) AppendField(bytes, field);
			if (generateContentLength)
			{
				AppendAscii(bytes, L"content-length: ");
				AppendDecimal(bytes, bodySize);
				AppendCrlf(bytes);
			}
			if (generateTransferEncoding)
			{
				AppendAscii(bytes, L"transfer-encoding: chunked\r\n");
			}
			AppendCrlf(bytes);

			if (!suppressBody && chunked)
			{
				for (auto&& chunk : body.chunks)
				{
					AppendHexadecimal(bytes, chunk.data.Count());
					AppendCrlf(bytes);
					for (auto c : chunk.data) bytes.Add(c);
					AppendCrlf(bytes);
				}
				AppendAscii(bytes, L"0\r\n");
				for (auto&& trailer : body.trailers) AppendField(bytes, trailer);
				AppendCrlf(bytes);
			}
			else if (!suppressBody && body.chunks.Count() == 1)
			{
				for (auto c : body.chunks[0].data) bytes.Add(c);
			}
			CHECK_ERROR(bytes.Count() <= HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");

			auto buffer = Ptr(new AsyncSocketBuffer);
			buffer->data.Resize(bytes.Count());
			if (bytes.Count() > 0)
			{
				std::memcpy(&buffer->data[0], &bytes[0], (size_t)bytes.Count());
			}
			connectionClose = framing.connectionClose;
			return buffer;
		}
	}

	HttpFramingAnalysisResult AnalyzeHttpFraming(const List<HttpField>& fields, HttpFraming& framing)
	{
		framing = HttpFraming();
		bool contentLengthInitialized = false;
		bool hasTransferEncoding = false;
		List<WString> transferCodings;
		bool transferCodingParameters = false;
		for (auto&& field : fields)
		{
			if (field.name == L"content-length")
			{
				framing.contentLengthFieldCount++;
				if (field.value.Count() == 0)
				{
					framing.contentLengthValuesPlainDecimal = false;
				}
				else
				{
					for (auto c : field.value)
					{
						if (!IsDigit(c))
						{
							framing.contentLengthValuesPlainDecimal = false;
							break;
						}
					}
				}
				if (!ParseContentLength(field.value, contentLengthInitialized, framing.contentLength, framing.contentLengthValueCount))
				{
					return HttpFramingAnalysisResult::Invalid;
				}
			}
			else if (field.name == L"transfer-encoding")
			{
				hasTransferEncoding = true;
				if (!ParseTransferCodings(field.value, transferCodings, transferCodingParameters))
				{
					return HttpFramingAnalysisResult::Invalid;
				}
			}
			else if (field.name == L"connection")
			{
				framing.connectionClose |= HasConnectionClose(field.value);
			}
		}
		if (hasTransferEncoding && contentLengthInitialized)
		{
			return HttpFramingAnalysisResult::Invalid;
		}
		if (hasTransferEncoding)
		{
			if (
				transferCodings.Count() != 1 ||
				transferCodings[0] != L"chunked" ||
				transferCodingParameters
				)
			{
				return HttpFramingAnalysisResult::UnsupportedTransferCoding;
			}
			framing.kind = HttpFramingKind::Chunked;
		}
		else if (contentLengthInitialized)
		{
			framing.kind = HttpFramingKind::ContentLength;
		}
		return HttpFramingAnalysisResult::Succeeded;
	}

	const HttpField* FindHttpField(const List<HttpField>& fields, const WString& normalizedName)
	{
		for (auto&& field : fields)
		{
			if (field.name == normalizedName)
			{
				return &field;
			}
		}
		return nullptr;
	}

	vint CountHttpFields(const List<HttpField>& fields, const WString& normalizedName)
	{
		vint count = 0;
		for (auto&& field : fields)
		{
			if (field.name == normalizedName)
			{
				count++;
			}
		}
		return count;
	}

	HttpField CreateAsciiHttpField(const WString& name, const WString& value)
	{
		CHECK_ERROR(name.Length() > 0, L"An HTTP field name cannot be empty.");
		HttpField field;
		Array<wchar_t> normalizedName(name.Length());
		for (vint i = 0; i < name.Length(); i++)
		{
			auto c = name[i];
			CHECK_ERROR((vuint32_t)c <= 0x7F && IsTokenCharacter((vuint8_t)c), L"An HTTP field name must contain only ASCII token characters.");
			if (L'A' <= c && c <= L'Z') c += L'a' - L'A';
			normalizedName[i] = c;
		}
		field.name = WString::CopyFrom(&normalizedName[0], normalizedName.Count());
		field.value.Resize(value.Length());
		for (vint i = 0; i < value.Length(); i++)
		{
			auto c = value[i];
			CHECK_ERROR((vuint32_t)c <= 0x7F && IsFieldValueCharacter((vuint8_t)c), L"An HTTP field value must contain only valid ASCII field characters.");
			field.value[i] = (vuint8_t)c;
		}
		return field;
	}

	bool DecodeAsciiHttpFieldValue(const Array<vuint8_t>& value, WString& text)
	{
		for (auto c : value)
		{
			if (c > 0x7F) return false;
		}
		if (value.Count() == 0)
		{
			text = WString::Empty;
			return true;
		}
		Array<wchar_t> characters(value.Count());
		for (vint i = 0; i < value.Count(); i++)
		{
			characters[i] = (wchar_t)value[i];
		}
		text = WString::CopyFrom(&characters[0], characters.Count());
		return true;
	}

	bool HttpFieldValueEqualsAscii(const Array<vuint8_t>& value, const WString& expected)
	{
		if (value.Count() != expected.Length()) return false;
		for (vint i = 0; i < value.Count(); i++)
		{
			if ((vuint32_t)expected[i] > 0x7F || value[i] != (vuint8_t)expected[i]) return false;
		}
		return true;
	}

	bool TryGetHttpBodySize(const HttpBody& body, vint& size)
	{
		vint total = 0;
		for (auto&& chunk : body.chunks)
		{
			if (chunk.data.Count() > HttpBodySizeLimit - total) return false;
			total += chunk.data.Count();
		}
		size = total;
		return true;
	}

	bool FlattenHttpBody(const HttpBody& body, Array<vuint8_t>& bytes)
	{
		vint size = 0;
		if (!TryGetHttpBodySize(body, size)) return false;
		Array<vuint8_t> flattened(size);
		vint offset = 0;
		for (auto&& chunk : body.chunks)
		{
			if (chunk.data.Count() > 0)
			{
				std::memcpy(&flattened[offset], &chunk.data[0], (size_t)chunk.data.Count());
				offset += chunk.data.Count();
			}
		}
		bytes = std::move(flattened);
		return true;
	}

	void SetHttpBodyBytes(HttpBody& body, Array<vuint8_t>&& bytes)
	{
		CHECK_ERROR(bytes.Count() <= HttpBodySizeLimit, L"HTTP body exceeds the configured size limit.");
		Array<vuint8_t> replacement = std::move(bytes);
		body.chunks.Clear();
		body.trailers.Clear();
		if (replacement.Count() > 0)
		{
			HttpBodyChunk chunk;
			chunk.data = std::move(replacement);
			body.chunks.Add(std::move(chunk));
		}
	}

	bool EncodeStrictUtf8(const WString& text, Array<vuint8_t>& bytes)
	{
		List<vuint8_t> encoded;
		for (vint i = 0; i < text.Length(); i++)
		{
			auto code = (vuint32_t)text[i];
			if constexpr (sizeof(wchar_t) == 2)
			{
				if (0xD800 <= code && code <= 0xDBFF)
				{
					if (++i == text.Length()) return false;
					auto low = (vuint32_t)text[i];
					if (low < 0xDC00 || low > 0xDFFF) return false;
					code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
				}
				else if (0xDC00 <= code && code <= 0xDFFF)
				{
					return false;
				}
			}
			else if (code > 0x10FFFF || (0xD800 <= code && code <= 0xDFFF))
			{
				return false;
			}

			vint adding = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
			if (encoded.Count() > (std::numeric_limits<vint>::max)() - adding) return false;
			if (adding == 1)
			{
				encoded.Add((vuint8_t)code);
			}
			else
			{
				static const vuint8_t prefixes[] = { 0, 0xC0, 0xE0, 0xF0 };
				vuint8_t output[4];
				for (vint j = adding - 1; j > 0; j--)
				{
					output[j] = 0x80 | (code & 0x3F);
					code >>= 6;
				}
				output[0] = prefixes[adding - 1] | (vuint8_t)code;
				for (vint j = 0; j < adding; j++) encoded.Add(output[j]);
			}
		}
		Array<vuint8_t> result(encoded.Count());
		for (vint i = 0; i < encoded.Count(); i++) result[i] = encoded[i];
		bytes = std::move(result);
		return true;
	}

	bool DecodeStrictUtf8(const vuint8_t* bytes, vint count, WString& text)
	{
		if (count < 0 || (!bytes && count > 0)) return false;
		List<wchar_t> characters;
		for (vint i = 0; i < count;)
		{
			auto first = bytes[i++];
			vuint32_t code = 0;
			vint following = 0;
			if (first < 0x80)
			{
				code = first;
			}
			else if (0xC2 <= first && first <= 0xDF)
			{
				code = first & 0x1F;
				following = 1;
			}
			else if (0xE0 <= first && first <= 0xEF)
			{
				code = first & 0x0F;
				following = 2;
			}
			else if (0xF0 <= first && first <= 0xF4)
			{
				code = first & 0x07;
				following = 3;
			}
			else
			{
				return false;
			}
			if (following > count - i) return false;
			for (vint j = 0; j < following; j++)
			{
				auto next = bytes[i++];
				if ((next & 0xC0) != 0x80) return false;
				code = (code << 6) | (next & 0x3F);
			}
			if (
				(following == 1 && code < 0x80) ||
				(following == 2 && code < 0x800) ||
				(following == 3 && code < 0x10000) ||
				code > 0x10FFFF ||
				(0xD800 <= code && code <= 0xDFFF)
				)
			{
				return false;
			}
			if constexpr (sizeof(wchar_t) == 2)
			{
				if (code <= 0xFFFF)
				{
					characters.Add((wchar_t)code);
				}
				else
				{
					if (characters.Count() > (std::numeric_limits<vint>::max)() - 2) return false;
					code -= 0x10000;
					characters.Add((wchar_t)(0xD800 + (code >> 10)));
					characters.Add((wchar_t)(0xDC00 + (code & 0x3FF)));
				}
			}
			else
			{
				characters.Add((wchar_t)code);
			}
		}
		text = characters.Count() == 0 ? WString::Empty : WString::CopyFrom(&characters[0], characters.Count());
		return true;
	}

	HttpRequestLineValidationResult ValidateHttpRequestLine(const WString& method, const WString& requestTarget)
	{
		if (method.Length() == 0) return HttpRequestLineValidationResult::InvalidMethod;
		for (vint i = 0; i < method.Length(); i++)
		{
			auto c = method[i];
			if ((vuint32_t)c > 0x7F || !IsTokenCharacter((vuint8_t)c)) return HttpRequestLineValidationResult::InvalidMethod;
		}
		if (requestTarget.Length() == 0) return HttpRequestLineValidationResult::InvalidRequestTarget;
		for (vint i = 0; i < requestTarget.Length(); i++)
		{
			auto c = requestTarget[i];
			if (c < 0x21 || c > 0x7E) return HttpRequestLineValidationResult::InvalidRequestTarget;
		}
		vint requestLineSize = 0;
		if (!TryGetHttpRequestLineSize(method, requestTarget, requestLineSize)) return HttpRequestLineValidationResult::TooLong;
		return HttpRequestLineValidationResult::Succeeded;
	}

/***********************************************************************
HttpRequestBody
***********************************************************************/

	namespace
	{
		HttpBodyDetailedParsingResult ParseHttpRequestBodyToChunksDetailed(
			const vuint8_t* buffer,
			vint availableBytes,
			HttpBody& output,
			vint& consumedBytes
			)
		{
			consumedBytes = 0;
			if (availableBytes < 0 || (!buffer && availableBytes > 0)) return HttpBodyDetailedParsingResult::BadRequest;
			HttpBody parsed;
			vint reading = 0;
			vint decodedBytes = 0;
			while (true)
			{
				vint lineEnd = FindCrlf(buffer, reading, availableBytes);
				if (lineEnd == -1)
				{
					auto trailingCrlfPrefix = availableBytes > reading && buffer[availableBytes - 1] == '\r';
					vint prefixEnd = trailingCrlfPrefix ? availableBytes - 1 : availableBytes;
					auto validPrefix = trailingCrlfPrefix
						? ValidateCompleteChunkSizeLine(buffer, reading, prefixEnd)
						: ValidateChunkSizeLinePrefix(buffer, reading, prefixEnd);
					if (prefixEnd - reading > HttpChunkSizeLineLimit || !validPrefix)
					{
						return HttpBodyDetailedParsingResult::BadRequest;
					}
					return HttpBodyDetailedParsingResult::Incomplete;
				}
				if (lineEnd - reading > HttpChunkSizeLineLimit) return HttpBodyDetailedParsingResult::BadRequest;
				if (lineEnd + 2 > HttpWireMessageSizeLimit) return HttpBodyDetailedParsingResult::PayloadTooLarge;
				vint numberEnd = reading;
				vuint64_t chunkSize = 0;
				while (numberEnd < lineEnd && IsHexDigit(buffer[numberEnd]))
				{
					auto digit = (vuint64_t)HexDigitValue(buffer[numberEnd++]);
					if (chunkSize > ((std::numeric_limits<vuint64_t>::max)() - digit) / 16) return HttpBodyDetailedParsingResult::PayloadTooLarge;
					chunkSize = chunkSize * 16 + digit;
				}
				if (numberEnd == reading) return HttpBodyDetailedParsingResult::BadRequest;
				if (chunkSize > (vuint64_t)HttpBodySizeLimit) return HttpBodyDetailedParsingResult::PayloadTooLarge;
				if (numberEnd < lineEnd)
				{
					vint firstExtension = numberEnd;
					while (firstExtension < lineEnd && IsOws(buffer[firstExtension])) firstExtension++;
					if (firstExtension == lineEnd || buffer[firstExtension] != ';') return HttpBodyDetailedParsingResult::BadRequest;
				}
				vint extensionReading = numberEnd;
				if (!ParseSemicolonParameters(buffer, extensionReading, lineEnd)) return HttpBodyDetailedParsingResult::BadRequest;
				reading = lineEnd + 2;

				if (chunkSize == 0)
				{
					vint trailerBegin = reading;
					while (true)
					{
						vint trailerEnd = FindCrlf(buffer, reading, availableBytes);
						if (trailerEnd == -1)
						{
							return availableBytes - trailerBegin >= HttpTrailerBlockSizeLimit
								? HttpBodyDetailedParsingResult::TrailerFieldsTooLarge
								: HttpBodyDetailedParsingResult::Incomplete;
						}
						if (trailerEnd + 2 - trailerBegin > HttpTrailerBlockSizeLimit) return HttpBodyDetailedParsingResult::TrailerFieldsTooLarge;
						if (trailerEnd + 2 > HttpWireMessageSizeLimit) return HttpBodyDetailedParsingResult::PayloadTooLarge;
						if (trailerEnd == reading)
						{
							consumedBytes = trailerEnd + 2;
							output = std::move(parsed);
							return HttpBodyDetailedParsingResult::Succeeded;
						}
						HttpField trailer;
						if (!ParseFieldLine(buffer, reading, trailerEnd, trailer) || trailer.name == L"content-length" || trailer.name == L"transfer-encoding")
						{
							return HttpBodyDetailedParsingResult::BadRequest;
						}
						parsed.trailers.Add(std::move(trailer));
						reading = trailerEnd + 2;
					}
				}

				if (chunkSize > (vuint64_t)(HttpBodySizeLimit - decodedBytes)) return HttpBodyDetailedParsingResult::PayloadTooLarge;
				vint chunkBytes = (vint)chunkSize;
				if (availableBytes - reading < chunkBytes) return HttpBodyDetailedParsingResult::Incomplete;
				auto terminatorBytes = availableBytes - reading - chunkBytes;
				if (terminatorBytes == 0) return HttpBodyDetailedParsingResult::Incomplete;
				if (buffer[reading + chunkBytes] != '\r') return HttpBodyDetailedParsingResult::BadRequest;
				if (terminatorBytes == 1) return HttpBodyDetailedParsingResult::Incomplete;
				if (buffer[reading + chunkBytes + 1] != '\n') return HttpBodyDetailedParsingResult::BadRequest;
				if (reading > HttpWireMessageSizeLimit - chunkBytes - 2) return HttpBodyDetailedParsingResult::PayloadTooLarge;
				if (parsed.chunks.Count() >= HttpChunkCountLimit) return HttpBodyDetailedParsingResult::PayloadTooLarge;
				HttpBodyChunk chunk;
				chunk.data.Resize(chunkBytes);
				std::memcpy(&chunk.data[0], buffer + reading, (size_t)chunkBytes);
				parsed.chunks.Add(std::move(chunk));
				decodedBytes += chunkBytes;
				reading += chunkBytes + 2;
			}
		}
	}

	HttpRequestBodyParsingResult ParseHttpRequestBodyToChunks(
		const vuint8_t* buffer,
		vint availableBytes,
		HttpBody& output,
		vint& consumedBytes
		)
	{
		auto result = ParseHttpRequestBodyToChunksDetailed(buffer, availableBytes, output, consumedBytes);
		switch (result)
		{
		case HttpBodyDetailedParsingResult::Succeeded:
			return HttpRequestBodyParsingResult::Succeeded;
		case HttpBodyDetailedParsingResult::Incomplete:
			return HttpRequestBodyParsingResult::Incomplete;
		default:
			return HttpRequestBodyParsingResult::Invalid;
		}
	}

/***********************************************************************
IHttpRequestCallback
***********************************************************************/

	void IHttpRequestCallback::OnReadRequest(Ptr<HttpRequest>)
	{
	}

	void IHttpRequestCallback::OnReadRequestFailure(HttpRequestFailure)
	{
	}

	void IHttpRequestCallback::OnReadResponse(Ptr<HttpResponse>)
	{
	}

	void IHttpRequestCallback::OnWriteCompleted()
	{
	}

	void IHttpRequestCallback::OnError(const WString&, bool)
	{
	}

	void IHttpRequestCallback::OnConnected()
	{
	}

	void IHttpRequestCallback::OnDisconnected()
	{
	}

/***********************************************************************
HttpRequestTimeoutController
***********************************************************************/

	namespace
	{
		class HttpRequestTimeoutController : public Object, public virtual IHttpRequestTimeoutController
		{
		private:
			class State : public Object
			{
			public:
				CriticalSection					lock;
				ConditionVariable				cv;
				Func<void()>					callback;
				std::chrono::steady_clock::time_point
										deadline;
				vint							duration = 0;
				bool							armed = false;
				bool							workerRunning = false;
				vint							activeCallbacks = 0;
			};

			static thread_local State*		currentCallbackState;
			Ptr<State>						state = Ptr(new State);

			static void Run(Ptr<State> state)
			{
				while (true)
				{
					Func<void()> callback;
					state->lock.Enter();
					while (state->armed)
					{
						auto now = std::chrono::steady_clock::now();
						if (now >= state->deadline)
						{
							callback = state->callback;
							state->callback = {};
							state->armed = false;
							state->activeCallbacks++;
							break;
						}
						auto remaining = std::chrono::ceil<std::chrono::milliseconds>(state->deadline - now).count();
						auto wait = remaining > (std::numeric_limits<vint>::max)()
							? (std::numeric_limits<vint>::max)()
							: (vint)remaining;
						state->cv.SleepWithForTime(state->lock, wait);
					}
					if (!callback)
					{
						state->workerRunning = false;
						state->cv.WakeAllPendings();
						state->lock.Leave();
						return;
					}
					state->lock.Leave();

					auto previous = currentCallbackState;
					currentCallbackState = state.Obj();
					try
					{
						callback();
					}
					catch (...)
					{
					}
					currentCallbackState = previous;

					CS_LOCK(state->lock)
					{
						state->activeCallbacks--;
						state->cv.WakeAllPendings();
					}
				}
			}

		public:
			~HttpRequestTimeoutController()
			{
				CancelAndWait();
			}

			void Arm(vint milliseconds, const Func<void()>& callback) override
			{
				CHECK_ERROR(milliseconds > 0, L"The HTTP timeout controller requires a positive duration.");
				bool queueWorker = false;
				CS_LOCK(state->lock)
				{
					CHECK_ERROR(!state->armed, L"The HTTP timeout controller is already armed.");
					state->callback = callback;
					state->duration = milliseconds;
					state->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
					state->armed = true;
					if (!state->workerRunning)
					{
						state->workerRunning = true;
						queueWorker = true;
					}
					state->cv.WakeAllPendings();
				}
				if (queueWorker)
				{
					auto captured = state;
					auto queued = ThreadPoolLite::Queue(Func<void()>([captured]()
					{
						Run(captured);
					}));
					if (!queued)
					{
						CS_LOCK(state->lock)
						{
							state->callback = {};
							state->armed = false;
							state->workerRunning = false;
							state->cv.WakeAllPendings();
						}
						CHECK_ERROR(false, L"The HTTP timeout controller could not queue its deadline worker.");
					}
				}
			}

			void Refresh() override
			{
				CS_LOCK(state->lock)
				{
					if (state->armed)
					{
						state->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(state->duration);
						state->cv.WakeAllPendings();
					}
				}
			}

			void CancelAndWait() override
			{
				auto nestedCallback = currentCallbackState == state.Obj();
				state->lock.Enter();
				state->armed = false;
				state->callback = {};
				state->cv.WakeAllPendings();
				if (!nestedCallback)
				{
					while (state->workerRunning || state->activeCallbacks > 0)
					{
						state->cv.SleepWith(state->lock);
					}
				}
				state->lock.Leave();
			}
		};

		thread_local HttpRequestTimeoutController::State* HttpRequestTimeoutController::currentCallbackState = nullptr;
	}

	Ptr<IHttpRequestTimeoutController> CreateHttpRequestTimeoutController()
	{
		return Ptr(new HttpRequestTimeoutController);
	}

/***********************************************************************
HttpRequestCallbackDomain
***********************************************************************/

	thread_local HttpRequestCallbackDomain::CallbackFrame* HttpRequestCallbackDomain::currentCallbackFrame = nullptr;

	HttpRequestCallbackDomain::CallbackFrame::CallbackFrame(Ptr<HttpRequestCallbackDomain> _domain)
		: domain(_domain)
		, previous(currentCallbackFrame)
	{
		currentCallbackFrame = this;
		CS_LOCK(domain->lockState)
		{
			domain->activeCallbacks++;
		}
	}

	HttpRequestCallbackDomain::CallbackFrame::~CallbackFrame()
	{
		currentCallbackFrame = previous;
		CS_LOCK(domain->lockState)
		{
			domain->activeCallbacks--;
			domain->cvState.WakeAllPendings();
		}
	}

	vint HttpRequestCallbackDomain::CurrentCallbackDepth()
	{
		vint depth = 0;
		for (auto frame = currentCallbackFrame; frame; frame = frame->previous)
		{
			if (frame->domain.Obj() == this) depth++;
		}
		return depth;
	}

	void HttpRequestCallbackDomain::WaitForCallbacks(vint callbackDepth)
	{
		CS_LOCK(lockState)
		{
			while (activeCallbacks > callbackDepth)
			{
				cvState.SleepWith(lockState);
			}
		}
	}

/***********************************************************************
HttpRequestConnectionLifecycle
***********************************************************************/

	class HttpRequestConnectionLifecycle : public Object
	{
	public:
		class RetainedAdapterRelease
		{
		public:
			Ptr<Object>						adapter;
			Func<void()>						drainedCallback;

			~RetainedAdapterRelease()
			{
				if (drainedCallback)
				{
					try
					{
						drainedCallback();
					}
					catch (...)
					{
					}
				}
			}
		};

		IAsyncSocketConnection*				socketConnection = nullptr;
		HttpRequestConnectionDirection		direction = HttpRequestConnectionDirection::Server;
		Ptr<HttpRequestCallbackDomain>		callbackDomain;
		Ptr<IHttpRequestTimeoutController>	timeoutController;
		Ptr<Object>						retainedAdapter;
		Func<void()>						drainedCallback;

		CriticalSection						lockState;
		ConditionVariable					cvState;
		IHttpRequestCallback*				callback = nullptr;
		bool							callbackInstalling = false;
		vint							activeCallbacks = 0;
		vint							activeSocketCallbacks = 0;
		vint							activeSocketCalls = 0;
		bool							stopStarted = false;
		bool							stopFinished = false;
		bool							terminal = false;
		bool							disconnectedNotified = false;
		bool							disconnectDelivering = false;
		bool							disconnectFinished = false;
		bool							readingStarted = false;
		bool							parserFailed = false;
		bool							peerDisconnected = false;

		List<vuint8_t>						receiveBuffer;
		bool							timeoutArmed = false;
		bool							awaitingResponse = false;
		bool							exchangeActive = false;
		bool							closeAfterExchange = false;
		WString						activeRequestMethod;
		vint							activeResponseTimeout = HttpIncompleteMessageTimeout;
		Ptr<HttpResponse>					heldResponse;
		bool							fatalAfterResponse = false;
		bool							responseDelivering = false;
		bool							responseFinalizing = false;
		Ptr<AsyncSocketBuffer>				deferredRequestWrite;
		bool							deferredRequestClose = false;
		WString						deferredRequestMethod;
		vint							deferredResponseTimeout = HttpIncompleteMessageTimeout;
		Ptr<AsyncSocketBuffer>				pendingWrite;
		bool							writePending = false;

		void TakeRetainedAdapterIfDrained(RetainedAdapterRelease& releasing)
		{
			if (stopFinished && disconnectFinished && activeCallbacks == 0 && activeSocketCallbacks == 0 && activeSocketCalls == 0)
			{
				releasing.adapter = std::move(retainedAdapter);
				releasing.drainedCallback = drainedCallback;
				drainedCallback = {};
			}
		}
	};

/***********************************************************************
HttpRequestConnection callback frames
***********************************************************************/

	struct HttpRequestConnection::CallbackFrame
	{
		Ptr<Lifecycle>						state;
		CallbackFrame*						previous = nullptr;
		HttpRequestCallbackDomain::CallbackFrame	domainFrame;

		CallbackFrame(Ptr<Lifecycle> _state)
			: state(_state)
			, previous(currentCallbackFrame)
			, domainFrame(state->callbackDomain)
		{
			currentCallbackFrame = this;
		}

		~CallbackFrame()
		{
			currentCallbackFrame = previous;
			Lifecycle::RetainedAdapterRelease releasing;
			CS_LOCK(state->lockState)
			{
				state->activeCallbacks--;
				state->TakeRetainedAdapterIfDrained(releasing);
				state->cvState.WakeAllPendings();
			}
		}
	};

	struct HttpRequestConnection::SocketCallbackFrame
	{
		Ptr<Lifecycle>						state;
		SocketCallbackFrame*				previous = nullptr;

		SocketCallbackFrame(Ptr<Lifecycle> _state)
			: state(_state)
			, previous(currentSocketCallbackFrame)
		{
			currentSocketCallbackFrame = this;
			CS_LOCK(state->lockState)
			{
				state->activeSocketCallbacks++;
			}
		}

		~SocketCallbackFrame()
		{
			currentSocketCallbackFrame = previous;
			Lifecycle::RetainedAdapterRelease releasing;
			CS_LOCK(state->lockState)
			{
				state->activeSocketCallbacks--;
				state->TakeRetainedAdapterIfDrained(releasing);
				state->cvState.WakeAllPendings();
			}
		}
	};

	struct HttpRequestConnection::TimeoutCallbackFrame
	{
		Ptr<Lifecycle>						state;
		TimeoutCallbackFrame*				previous = nullptr;

		TimeoutCallbackFrame(Ptr<Lifecycle> _state)
			: state(_state)
			, previous(currentTimeoutCallbackFrame)
		{
			currentTimeoutCallbackFrame = this;
		}

		~TimeoutCallbackFrame()
		{
			currentTimeoutCallbackFrame = previous;
		}
	};

	thread_local HttpRequestConnection::CallbackFrame* HttpRequestConnection::currentCallbackFrame = nullptr;
	thread_local HttpRequestConnection::SocketCallbackFrame* HttpRequestConnection::currentSocketCallbackFrame = nullptr;
	thread_local HttpRequestConnection::TimeoutCallbackFrame* HttpRequestConnection::currentTimeoutCallbackFrame = nullptr;

/***********************************************************************
HttpRequestConnection helpers
***********************************************************************/

	vint HttpRequestConnection::CurrentCallbackDepth(Ptr<Lifecycle> state)
	{
		vint depth = 0;
		for (auto frame = currentCallbackFrame; frame; frame = frame->previous)
		{
			if (frame->state.Obj() == state.Obj()) depth++;
		}
		return depth;
	}

	vint HttpRequestConnection::CurrentSocketCallbackDepth(Ptr<Lifecycle> state)
	{
		vint depth = 0;
		for (auto frame = currentSocketCallbackFrame; frame; frame = frame->previous)
		{
			if (frame->state.Obj() == state.Obj()) depth++;
		}
		return depth;
	}

	vint HttpRequestConnection::CurrentTimeoutCallbackDepth(Ptr<Lifecycle> state)
	{
		vint depth = 0;
		for (auto frame = currentTimeoutCallbackFrame; frame; frame = frame->previous)
		{
			if (frame->state.Obj() == state.Obj()) depth++;
		}
		return depth;
	}

	void HttpRequestConnection::FinishSocketCall(Ptr<Lifecycle> state)
	{
		bool completePeerStop = false;
		CS_LOCK(state->lockState)
		{
			state->activeSocketCalls--;
			completePeerStop = state->activeSocketCalls == 0 && state->peerDisconnected && !state->stopFinished;
			state->cvState.WakeAllPendings();
		}
		if (completePeerStop)
		{
			StopConnection(state);
		}

		Lifecycle::RetainedAdapterRelease releasing;
		CS_LOCK(state->lockState)
		{
			state->TakeRetainedAdapterIfDrained(releasing);
		}
	}

	template<typename TCallback>
	void HttpRequestConnection::InvokeHttpCallback(Ptr<Lifecycle> state, bool allowTerminal, TCallback&& invoke)
	{
		IHttpRequestCallback* installed = nullptr;
		auto callbackDepth = CurrentCallbackDepth(state);
		state->lockState.Enter();
		while (state->callbackInstalling && callbackDepth == 0 && state->callback)
		{
			state->cvState.SleepWith(state->lockState);
		}
		if (state->callback && (allowTerminal || (!state->stopStarted && !state->terminal)))
		{
			installed = state->callback;
			state->activeCallbacks++;
		}
		state->lockState.Leave();
		if (installed)
		{
			CallbackFrame frame(state);
			invoke(installed);
		}
	}

	void HttpRequestConnection::SubmitWrite(Ptr<Lifecycle> state, IAsyncSocketConnection* connection, Ptr<AsyncSocketBuffer> buffer)
	{
		try
		{
			connection->WriteAsync(buffer);
		}
		catch (...)
		{
			CS_LOCK(state->lockState)
			{
				if (state->pendingWrite.Obj() == buffer.Obj())
				{
					state->pendingWrite = nullptr;
					state->writePending = false;
					if (state->direction == HttpRequestConnectionDirection::Client)
					{
						state->exchangeActive = false;
						state->closeAfterExchange = false;
					}
				}
				state->cvState.WakeAllPendings();
			}
			FinishSocketCall(state);
			throw;
		}
		FinishSocketCall(state);
	}

	void HttpRequestConnection::ReportRequestFailure(Ptr<Lifecycle> state, HttpRequestFailure failure, bool timeoutOnly, bool reserved)
	{
		bool report = false;
		bool cancelTimeout = false;
		CS_LOCK(state->lockState)
		{
			if (
				reserved &&
				state->direction == HttpRequestConnectionDirection::Server &&
				!state->stopStarted &&
				!state->terminal &&
				state->parserFailed &&
				state->awaitingResponse
				)
			{
				cancelTimeout = state->timeoutArmed;
				state->timeoutArmed = false;
				report = true;
			}
			else if (
				state->direction == HttpRequestConnectionDirection::Server &&
				!state->stopStarted &&
				!state->terminal &&
				!state->parserFailed &&
				!state->awaitingResponse &&
				(!timeoutOnly || state->timeoutArmed)
				)
			{
				state->parserFailed = true;
				state->awaitingResponse = true;
				state->closeAfterExchange = true;
				state->receiveBuffer.Clear();
				cancelTimeout = state->timeoutArmed;
				state->timeoutArmed = false;
				report = true;
				state->cvState.WakeAllPendings();
			}
		}
		if (!report)
		{
			return;
		}
		if (cancelTimeout)
		{
			state->timeoutController->CancelAndWait();
		}

		try
		{
			InvokeHttpCallback(state, false, [&](IHttpRequestCallback* installed)
			{
				installed->OnReadRequestFailure(failure);
			});
		}
		catch (...)
		{
			StopConnection(state);
			throw;
		}

		bool closeWithoutResponse = false;
		CS_LOCK(state->lockState)
		{
			closeWithoutResponse = !state->stopStarted && !state->terminal && !state->peerDisconnected && !state->writePending;
		}
		if (closeWithoutResponse)
		{
			StopConnection(state);
		}
	}

	void HttpRequestConnection::InstallTimeout(Ptr<Lifecycle> state, vint milliseconds, const WString& error)
	{
		CHECK_ERROR(milliseconds > 0, L"HttpRequestConnection requires a positive timeout when arming a deadline.");
		auto captured = state;
		auto capturedError = error;
		try
		{
			state->timeoutController->Arm(milliseconds, Func<void()>([captured, capturedError]()
			{
				TimeoutCallbackFrame timeoutCallbackFrame(captured);
				if (captured->direction == HttpRequestConnectionDirection::Server)
				{
					ReportRequestFailure(captured, HttpRequestFailure::RequestTimeout, true);
					return;
				}

				bool expired = false;
				CS_LOCK(captured->lockState)
				{
					if (captured->timeoutArmed && !captured->stopStarted && !captured->terminal && !captured->parserFailed)
					{
						captured->timeoutArmed = false;
						captured->parserFailed = true;
						expired = true;
					}
				}
				if (expired)
				{
					ReportFatalError(captured, capturedError);
				}
			}));
			bool cancelStaleTimeout = false;
			CS_LOCK(state->lockState)
			{
				cancelStaleTimeout = !state->timeoutArmed || state->stopStarted || state->terminal;
			}
			if (cancelStaleTimeout)
			{
				state->timeoutController->CancelAndWait();
			}
		}
		catch (...)
		{
			CS_LOCK(state->lockState)
			{
				state->timeoutArmed = false;
			}
			ReportFatalError(state, L"The HTTP timeout controller failed while arming a message timeout.");
		}
	}

	void HttpRequestConnection::RefreshTimeout(Ptr<Lifecycle> state)
	{
		try
		{
			state->timeoutController->Refresh();
		}
		catch (...)
		{
			CS_LOCK(state->lockState)
			{
				state->timeoutArmed = false;
			}
			ReportFatalError(state, L"The HTTP timeout controller failed while refreshing a message timeout.");
		}
	}

	void HttpRequestConnection::DeliverResponse(Ptr<Lifecycle> state, Ptr<HttpResponse> response, bool closeAfterDelivery)
	{
		try
		{
			InvokeHttpCallback(state, false, [&](IHttpRequestCallback* installed)
			{
				installed->OnReadResponse(response);
			});
		}
		catch (...)
		{
			bool notifyDisconnected = false;
			CS_LOCK(state->lockState)
			{
				state->responseDelivering = false;
				state->deferredRequestWrite = nullptr;
				state->deferredRequestClose = false;
				state->deferredRequestMethod = L"";
				notifyDisconnected = state->peerDisconnected;
				state->responseFinalizing = notifyDisconnected;
				state->cvState.WakeAllPendings();
			}
			if (notifyDisconnected)
			{
				StopConnection(state);
				CS_LOCK(state->lockState)
				{
					state->responseFinalizing = false;
					state->cvState.WakeAllPendings();
				}
			}
			throw;
		}

		bool fatalAfterDelivery = false;
		bool notifyDisconnected = false;
		Ptr<AsyncSocketBuffer> deferredRequestWrite;
		IAsyncSocketConnection* deferredRequestConnection = nullptr;
		CS_LOCK(state->lockState)
		{
			fatalAfterDelivery = state->fatalAfterResponse;
			state->fatalAfterResponse = false;
			notifyDisconnected = state->peerDisconnected;
			state->responseDelivering = false;
			state->responseFinalizing = fatalAfterDelivery || closeAfterDelivery || notifyDisconnected;
			if (state->responseFinalizing)
			{
				state->deferredRequestWrite = nullptr;
				state->deferredRequestClose = false;
				state->deferredRequestMethod = L"";
			}
			else if (state->deferredRequestWrite && !state->stopStarted && !state->terminal && state->socketConnection)
			{
				deferredRequestWrite = std::move(state->deferredRequestWrite);
				state->exchangeActive = true;
				state->closeAfterExchange = state->deferredRequestClose;
				state->deferredRequestClose = false;
				state->activeRequestMethod = std::move(state->deferredRequestMethod);
				state->activeResponseTimeout = state->deferredResponseTimeout;
				state->pendingWrite = deferredRequestWrite;
				state->writePending = true;
				deferredRequestConnection = state->socketConnection;
				state->activeSocketCalls++;
			}
			state->cvState.WakeAllPendings();
		}

		try
		{
			if (fatalAfterDelivery)
			{
				ReportFatalError(state, L"The HTTP client received an unsolicited response after its exchange completed.");
			}
			else if (notifyDisconnected)
			{
				StopConnection(state);
			}
			else if (closeAfterDelivery)
			{
				StopConnection(state);
			}
		}
		catch (...)
		{
			CS_LOCK(state->lockState)
			{
				state->responseFinalizing = false;
				state->cvState.WakeAllPendings();
			}
			throw;
		}

		CS_LOCK(state->lockState)
		{
			state->responseFinalizing = false;
			state->cvState.WakeAllPendings();
		}
		if (!fatalAfterDelivery && !closeAfterDelivery && !notifyDisconnected)
		{
			if (deferredRequestWrite)
			{
				try
				{
					SubmitWrite(state, deferredRequestConnection, deferredRequestWrite);
				}
				catch (...)
				{
					ReportFatalError(state, L"The HTTP client failed to submit a deferred request write.");
					return;
				}
			}
			ProcessBufferedInput(state);
		}
	}

	void HttpRequestConnection::ProcessBufferedInput(Ptr<Lifecycle> state)
	{
		Ptr<HttpRequest> request;
		Ptr<HttpResponse> response;
		bool cancelTimeout = false;
		bool deliverRequest = false;
		bool deliverResponse = false;
		bool closeAfterDelivery = false;
		bool requestFailure = false;
		bool clientFailure = false;
		HttpRequestFailure failure = HttpRequestFailure::BadRequest;
		WString parsedRequestMethod;

		state->lockState.Enter();
		if (state->stopStarted || state->terminal || state->parserFailed || state->peerDisconnected)
		{
			state->lockState.Leave();
			return;
		}
		auto parseEnabled = state->direction == HttpRequestConnectionDirection::Server
			? !state->awaitingResponse
			: state->exchangeActive && !state->heldResponse && !state->responseDelivering && !state->responseFinalizing;
		if (!parseEnabled)
		{
			state->lockState.Leave();
			return;
		}
		if (state->receiveBuffer.Count() == 0)
		{
			state->lockState.Leave();
			return;
		}

		vint consumedBytes = 0;
		bool messageClose = false;
		auto result = ParseHttpMessage(
			&state->receiveBuffer[0],
			state->receiveBuffer.Count(),
			state->direction == HttpRequestConnectionDirection::Server,
			state->activeRequestMethod,
			parsedRequestMethod,
			request,
			response,
			consumedBytes,
			messageClose
			);
		if (result == HttpMessageParsingResult::Incomplete)
		{
			bool armTimeout = false;
			bool refreshTimeout = false;
			auto serverSide = state->direction == HttpRequestConnectionDirection::Server;
			if (serverSide && parsedRequestMethod.Length() > 0)
			{
				state->activeRequestMethod = parsedRequestMethod;
			}
			auto timeout = serverSide ? HttpIncompleteMessageTimeout : state->activeResponseTimeout;
			auto canArm = timeout > 0 && (serverSide || !state->writePending);
			if (canArm && !state->timeoutArmed)
			{
				state->timeoutArmed = true;
				armTimeout = true;
			}
			else if (canArm && serverSide)
			{
				refreshTimeout = true;
			}
			state->lockState.Leave();
			if (armTimeout)
			{
				InstallTimeout(state, timeout, L"The HTTP peer timed out while sending an incomplete message.");
			}
			else if (refreshTimeout)
			{
				RefreshTimeout(state);
			}
			return;
		}
		if (result != HttpMessageParsingResult::Succeeded)
		{
			if (state->direction == HttpRequestConnectionDirection::Server)
			{
				state->activeRequestMethod = parsedRequestMethod;
				state->parserFailed = true;
				state->awaitingResponse = true;
				state->closeAfterExchange = true;
				state->receiveBuffer.Clear();
				failure = GetHttpRequestFailure(result);
				requestFailure = true;
			}
			else
			{
				clientFailure = true;
			}
			state->lockState.Leave();
		}
		else
		{
			state->receiveBuffer.RemoveRange(0, consumedBytes);
			if (state->timeoutArmed)
			{
				state->timeoutArmed = false;
				cancelTimeout = true;
			}
			state->closeAfterExchange |= messageClose;
			if (state->direction == HttpRequestConnectionDirection::Server)
			{
				state->activeRequestMethod = request->method;
				state->awaitingResponse = true;
				deliverRequest = true;
			}
			else if (state->writePending)
			{
				state->heldResponse = response;
				if (state->receiveBuffer.Count() > 0)
				{
					state->fatalAfterResponse = true;
					state->receiveBuffer.Clear();
				}
			}
			else
			{
				state->exchangeActive = false;
				state->responseDelivering = true;
				deliverResponse = true;
				closeAfterDelivery = state->closeAfterExchange;
				if (state->receiveBuffer.Count() > 0)
				{
					state->fatalAfterResponse = true;
					state->receiveBuffer.Clear();
				}
			}
			state->lockState.Leave();
		}

		if (requestFailure)
		{
			ReportRequestFailure(state, failure, false, true);
			return;
		}
		if (clientFailure)
		{
			ReportFatalError(state, L"The HTTP peer sent malformed or unsafe HTTP/1.1 framing.");
			return;
		}
		if (cancelTimeout)
		{
			state->timeoutController->CancelAndWait();
		}
		if (deliverRequest)
		{
			InvokeHttpCallback(state, false, [&](IHttpRequestCallback* installed)
			{
				installed->OnReadRequest(request);
			});
		}
		else if (deliverResponse)
		{
			DeliverResponse(state, response, closeAfterDelivery);
		}
	}

	void HttpRequestConnection::NotifyDisconnected(Ptr<Lifecycle> state)
	{
		auto callbackDepth = CurrentCallbackDepth(state);
		state->lockState.Enter();
		if (!state->disconnectedNotified)
		{
			state->disconnectedNotified = true;
			state->terminal = true;
			state->pendingWrite = nullptr;
			state->writePending = false;
			state->heldResponse = nullptr;
			state->fatalAfterResponse = false;
			state->deferredRequestWrite = nullptr;
			state->deferredRequestClose = false;
			state->deferredRequestMethod = L"";
			state->cvState.WakeAllPendings();
		}
		if (state->disconnectFinished)
		{
			state->lockState.Leave();
			return;
		}
		if (state->disconnectDelivering)
		{
			if (callbackDepth == 0)
			{
				while (!state->disconnectFinished) state->cvState.SleepWith(state->lockState);
			}
			state->lockState.Leave();
			return;
		}
		if (callbackDepth == 0)
		{
			while (state->activeCallbacks > 0 && !state->disconnectDelivering && !state->disconnectFinished)
			{
				state->cvState.SleepWith(state->lockState);
			}
			if (state->disconnectFinished)
			{
				state->lockState.Leave();
				return;
			}
		}
		state->disconnectDelivering = true;
		while (state->activeCallbacks > callbackDepth)
		{
			state->cvState.SleepWith(state->lockState);
		}
		state->lockState.Leave();

		try
		{
			InvokeHttpCallback(state, true, [](IHttpRequestCallback* installed)
			{
				installed->OnDisconnected();
			});
		}
		catch (...)
		{
			Lifecycle::RetainedAdapterRelease releasing;
			CS_LOCK(state->lockState)
			{
				state->callback = nullptr;
				state->disconnectFinished = true;
				state->TakeRetainedAdapterIfDrained(releasing);
				state->cvState.WakeAllPendings();
			}
			throw;
		}

		Lifecycle::RetainedAdapterRelease releasing;
		CS_LOCK(state->lockState)
		{
			state->callback = nullptr;
			while (state->activeCallbacks > callbackDepth)
			{
				state->cvState.SleepWith(state->lockState);
			}
			state->disconnectFinished = true;
			state->TakeRetainedAdapterIfDrained(releasing);
			state->cvState.WakeAllPendings();
		}
	}

	void HttpRequestConnection::StopConnection(Ptr<Lifecycle> state, Ptr<Object> retainedAdapter)
	{
		auto callbackDepth = CurrentCallbackDepth(state);
		auto socketCallbackDepth = CurrentSocketCallbackDepth(state);
		auto timeoutCallbackDepth = CurrentTimeoutCallbackDepth(state);
		auto nestedCallback = callbackDepth > 0 || socketCallbackDepth > 0;
		IAsyncSocketConnection* connection = nullptr;
		bool executeStop = false;
		bool nestedFollower = false;
		bool timeoutFollower = false;
		Lifecycle::RetainedAdapterRelease releasing;

		state->lockState.Enter();
		if (retainedAdapter) state->retainedAdapter = retainedAdapter;
		if (!state->stopStarted)
		{
			state->stopStarted = true;
			state->timeoutArmed = false;
			state->pendingWrite = nullptr;
			state->writePending = false;
			state->heldResponse = nullptr;
			state->fatalAfterResponse = false;
			state->deferredRequestWrite = nullptr;
			state->deferredRequestClose = false;
			state->deferredRequestMethod = L"";
			if (!nestedCallback)
			{
				while (state->activeSocketCalls > 0) state->cvState.SleepWith(state->lockState);
			}
			connection = state->peerDisconnected ? nullptr : state->socketConnection;
			executeStop = true;
		}
		else if (nestedCallback)
		{
			connection = state->socketConnection;
			nestedFollower = true;
		}
		else if (timeoutCallbackDepth > 0)
		{
			timeoutFollower = true;
		}
		else
		{
			while (!state->stopFinished) state->cvState.SleepWith(state->lockState);
			while (state->activeCallbacks > 0 || state->activeSocketCallbacks > 0 || state->activeSocketCalls > 0)
			{
				state->cvState.SleepWith(state->lockState);
			}
			state->TakeRetainedAdapterIfDrained(releasing);
			state->lockState.Leave();
			return;
		}
		state->lockState.Leave();

		if (nestedFollower)
		{
			if (connection && socketCallbackDepth > 0) connection->Stop();
			NotifyDisconnected(state);
			return;
		}
		if (timeoutFollower)
		{
			return;
		}
		state->timeoutController->CancelAndWait();
		if (executeStop && connection) connection->Stop();
		NotifyDisconnected(state);

		CS_LOCK(state->lockState)
		{
			while (state->activeCallbacks > callbackDepth || state->activeSocketCallbacks > socketCallbackDepth)
			{
				state->cvState.SleepWith(state->lockState);
			}
			state->stopFinished = true;
			state->TakeRetainedAdapterIfDrained(releasing);
			state->cvState.WakeAllPendings();
		}
	}

	void HttpRequestConnection::ReportFatalError(Ptr<Lifecycle> state, const WString& error)
	{
		bool report = false;
		CS_LOCK(state->lockState)
		{
			if (!state->terminal && !state->stopStarted)
			{
				state->terminal = true;
				state->parserFailed = true;
				state->timeoutArmed = false;
				state->pendingWrite = nullptr;
				state->writePending = false;
				state->heldResponse = nullptr;
				state->fatalAfterResponse = false;
				state->deferredRequestWrite = nullptr;
				state->deferredRequestClose = false;
				state->deferredRequestMethod = L"";
				report = true;
				state->cvState.WakeAllPendings();
			}
		}
		if (report)
		{
			state->timeoutController->CancelAndWait();
			try
			{
				InvokeHttpCallback(state, true, [&](IHttpRequestCallback* installed)
				{
					installed->OnError(error, true);
				});
			}
			catch (...)
			{
				StopConnection(state);
				throw;
			}
			StopConnection(state);
		}
	}

/***********************************************************************
HttpRequestConnection
***********************************************************************/

	HttpRequestConnection::HttpRequestConnection(
		IAsyncSocketConnection* connection,
		HttpRequestConnectionDirection direction,
		Ptr<HttpRequestCallbackDomain> callbackDomain,
		Ptr<IHttpRequestTimeoutController> timeoutController
		)
		: lifecycle(Ptr(new Lifecycle))
	{
		CHECK_ERROR(connection, L"HttpRequestConnection requires a valid async socket connection.");
		lifecycle->socketConnection = connection;
		lifecycle->direction = direction;
		lifecycle->callbackDomain = callbackDomain ? callbackDomain : Ptr(new HttpRequestCallbackDomain);
		lifecycle->timeoutController = timeoutController ? timeoutController : CreateHttpRequestTimeoutController();
		connection->InstallCallback(this);
	}

	HttpRequestConnection::~HttpRequestConnection()
	{
		StopConnection(lifecycle);
	}

	void HttpRequestConnection::RetainUntilStopped(Ptr<HttpRequestConnection> retainedAdapter, const Func<void()>& drainedCallback)
	{
		CHECK_ERROR(retainedAdapter.Obj() == this, L"HttpRequestConnection::RetainUntilStopped requires this connection as its retained adapter.");
		CHECK_ERROR(drainedCallback, L"HttpRequestConnection::RetainUntilStopped requires a drained callback.");
		bool canRetain = false;
		CS_LOCK(lifecycle->lockState)
		{
			if (!lifecycle->retainedAdapter && !lifecycle->drainedCallback && !lifecycle->stopStarted)
			{
				lifecycle->retainedAdapter = retainedAdapter;
				lifecycle->drainedCallback = drainedCallback;
				canRetain = true;
			}
		}
		CHECK_ERROR(canRetain, L"HttpRequestConnection::RetainUntilStopped can only be called once before stopping.");
	}

	void HttpRequestConnection::StopWithRetainedAdapter(Ptr<HttpRequestConnection> retainedAdapter)
	{
		StopConnection(lifecycle, retainedAdapter);
	}

	bool HttpRequestConnection::IsInsideCallback()
	{
		return CurrentCallbackDepth(lifecycle) > 0 || CurrentSocketCallbackDepth(lifecycle) > 0;
	}

	void HttpRequestConnection::InstallCallback(IHttpRequestCallback* value)
	{
		auto state = lifecycle;
		if (!value)
		{
			auto callbackDepth = CurrentCallbackDepth(state);
			bool uninstallOwner = false;
			CS_LOCK(state->lockState)
			{
				uninstallOwner = state->callback != nullptr;
				state->callback = nullptr;
				while ((callbackDepth == 0 || uninstallOwner) && state->activeCallbacks > callbackDepth)
				{
					state->cvState.SleepWith(state->lockState);
				}
			}
			return;
		}

		bool canInstall = false;
		CS_LOCK(state->lockState)
		{
			if (!state->callback && !state->callbackInstalling && !state->stopStarted && !state->terminal)
			{
				state->callback = value;
				state->callbackInstalling = true;
				state->activeCallbacks++;
				canInstall = true;
			}
		}
		CHECK_ERROR(canInstall, L"HttpRequestConnection::InstallCallback cannot replace a callback or install one on a stopped connection.");

		CallbackFrame frame(state);
		try
		{
			value->OnInstalled(this);
		}
		catch (...)
		{
			CS_LOCK(state->lockState)
			{
				if (state->callback == value) state->callback = nullptr;
				state->callbackInstalling = false;
				state->cvState.WakeAllPendings();
			}
			throw;
		}
		CS_LOCK(state->lockState)
		{
			state->callbackInstalling = false;
			state->cvState.WakeAllPendings();
		}
	}

	void HttpRequestConnection::BeginReadingLoopUnsafe()
	{
		auto state = lifecycle;
		IAsyncSocketConnection* connection = nullptr;
		Ptr<Object> callRetainer;
		bool canBegin = false;
		CS_LOCK(state->lockState)
		{
			if (!state->readingStarted && !state->stopStarted && !state->terminal && state->socketConnection)
			{
				state->readingStarted = true;
				connection = state->socketConnection;
				callRetainer = state->retainedAdapter;
				state->activeSocketCalls++;
				canBegin = true;
			}
		}
		CHECK_ERROR(canBegin, L"HttpRequestConnection::BeginReadingLoopUnsafe can only be called once on an active connection.");
		try
		{
			connection->BeginReadingLoopUnsafe();
		}
		catch (...)
		{
			FinishSocketCall(state);
			throw;
		}
		FinishSocketCall(state);
	}

	void HttpRequestConnection::SendRequest(Ptr<HttpRequest> request, vint responseTimeout)
	{
		auto state = lifecycle;
		CHECK_ERROR(state->direction == HttpRequestConnectionDirection::Client, L"HttpRequestConnection::SendRequest is only available on a client connection.");
		CHECK_ERROR(request, L"HttpRequestConnection::SendRequest requires a request.");
		bool requestClose = false;
		auto buffer = SerializeHttpMessage(request.Obj(), nullptr, L"", requestClose);
		IAsyncSocketConnection* connection = nullptr;
		Ptr<Object> callRetainer;
		bool canSend = false;
		bool deferSend = false;
		CS_LOCK(state->lockState)
		{
			auto commonStateAvailable = !state->stopStarted && !state->terminal && !state->peerDisconnected
				&& !state->exchangeActive && !state->writePending && !state->deferredRequestWrite
				&& !state->responseFinalizing && !state->fatalAfterResponse && !state->closeAfterExchange && state->socketConnection;
			if (commonStateAvailable && state->responseDelivering)
			{
				state->deferredRequestWrite = buffer;
				state->deferredRequestClose = requestClose;
				state->deferredRequestMethod = request->method;
				state->deferredResponseTimeout = responseTimeout;
				deferSend = true;
				canSend = true;
			}
			else if (commonStateAvailable && !state->responseDelivering)
			{
				state->exchangeActive = true;
				state->closeAfterExchange = requestClose;
				state->activeRequestMethod = request->method;
				state->activeResponseTimeout = responseTimeout;
				state->pendingWrite = buffer;
				state->writePending = true;
				connection = state->socketConnection;
				callRetainer = state->retainedAdapter;
				state->activeSocketCalls++;
				canSend = true;
			}
		}
		CHECK_ERROR(canSend, L"HttpRequestConnection::SendRequest requires an idle active client connection.");
		if (deferSend) return;
		SubmitWrite(state, connection, buffer);
		ProcessBufferedInput(state);
	}

	void HttpRequestConnection::SendResponse(Ptr<HttpResponse> response)
	{
		auto state = lifecycle;
		CHECK_ERROR(state->direction == HttpRequestConnectionDirection::Server, L"HttpRequestConnection::SendResponse is only available on a server connection.");
		CHECK_ERROR(response, L"HttpRequestConnection::SendResponse requires a response.");
		WString responseToMethod;
		CS_LOCK(state->lockState)
		{
			responseToMethod = state->activeRequestMethod;
		}
		bool responseClose = false;
		auto buffer = SerializeHttpMessage(nullptr, response.Obj(), responseToMethod, responseClose);
		IAsyncSocketConnection* connection = nullptr;
		Ptr<Object> callRetainer;
		bool canSend = false;
		CS_LOCK(state->lockState)
		{
			if (!state->stopStarted && !state->terminal && state->awaitingResponse && !state->writePending && state->socketConnection)
			{
				state->closeAfterExchange |= responseClose;
				state->pendingWrite = buffer;
				state->writePending = true;
				connection = state->socketConnection;
				callRetainer = state->retainedAdapter;
				state->activeSocketCalls++;
				canSend = true;
			}
		}
		CHECK_ERROR(canSend, L"HttpRequestConnection::SendResponse requires one delivered request without a response in progress.");
		SubmitWrite(state, connection, buffer);
	}

	void HttpRequestConnection::Stop()
	{
		StopConnection(lifecycle);
	}

	void HttpRequestConnection::OnRead(const vuint8_t* buffer, vint size)
	{
		auto state = lifecycle;
		SocketCallbackFrame frame(state);
		if (!buffer || size <= 0) return;
		bool tooLarge = false;
		bool requestTooLarge = false;
		bool unsolicited = false;
		CS_LOCK(state->lockState)
		{
			if (state->stopStarted || state->terminal || state->parserFailed || state->peerDisconnected) return;
			if (state->direction == HttpRequestConnectionDirection::Client && state->heldResponse)
			{
				state->fatalAfterResponse = true;
			}
			else if (state->direction == HttpRequestConnectionDirection::Client && (state->responseDelivering || state->responseFinalizing) && !state->exchangeActive)
			{
				state->fatalAfterResponse = true;
			}
			else if (state->direction == HttpRequestConnectionDirection::Client && !state->exchangeActive)
			{
				state->parserFailed = true;
				unsolicited = true;
			}
			else if (size > HttpWireMessageSizeLimit - state->receiveBuffer.Count())
			{
				if (state->direction == HttpRequestConnectionDirection::Server)
				{
					state->parserFailed = true;
					state->awaitingResponse = true;
					state->closeAfterExchange = true;
					state->receiveBuffer.Clear();
					requestTooLarge = true;
				}
				else
				{
					state->parserFailed = true;
					tooLarge = true;
				}
			}
			else
			{
				for (vint i = 0; i < size; i++) state->receiveBuffer.Add(buffer[i]);
			}
		}
		if (unsolicited)
		{
			ReportFatalError(state, L"The HTTP client received bytes without an active request exchange.");
			return;
		}
		if (tooLarge)
		{
			ReportFatalError(state, L"The HTTP peer exceeded the configured wire-message size limit.");
			return;
		}
		if (requestTooLarge)
		{
			ReportRequestFailure(state, HttpRequestFailure::PayloadTooLarge, false, true);
			return;
		}
		ProcessBufferedInput(state);
	}

	void HttpRequestConnection::OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer)
	{
		auto state = lifecycle;
		SocketCallbackFrame frame(state);
		bool mismatched = false;
		bool ignored = false;
		CS_LOCK(state->lockState)
		{
			if (!state->writePending || state->pendingWrite.Obj() != buffer.Obj())
			{
				if (state->stopStarted || state->terminal || state->peerDisconnected)
				{
					ignored = true;
				}
				else
				{
					mismatched = true;
				}
			}
			else
			{
				state->pendingWrite = nullptr;
			}
		}
		if (ignored) return;
		CHECK_ERROR(!mismatched, L"HttpRequestConnection received a completion for an unexpected async socket buffer.");

		InvokeHttpCallback(state, false, [](IHttpRequestCallback* installed)
		{
			installed->OnWriteCompleted();
		});

		Ptr<HttpResponse> response;
		bool serverSide = false;
		bool closeAfterDelivery = false;
		bool armTimeout = false;
		vint responseTimeout = 0;
		CS_LOCK(state->lockState)
		{
			if (state->stopStarted || state->terminal) return;
			state->writePending = false;
			serverSide = state->direction == HttpRequestConnectionDirection::Server;
			if (serverSide)
			{
				state->awaitingResponse = false;
				state->activeRequestMethod = L"";
				closeAfterDelivery = state->closeAfterExchange;
			}
			else if (state->heldResponse)
			{
				response = std::move(state->heldResponse);
				state->exchangeActive = false;
				state->responseDelivering = true;
				closeAfterDelivery = state->closeAfterExchange;
			}
			else if (!state->peerDisconnected && !state->timeoutArmed && state->activeResponseTimeout > 0)
			{
				state->timeoutArmed = true;
				responseTimeout = state->activeResponseTimeout;
				armTimeout = true;
			}
		}

		if (response)
		{
			DeliverResponse(state, response, closeAfterDelivery);
		}
		else if (closeAfterDelivery)
		{
			StopConnection(state);
		}
		else if (serverSide)
		{
			ProcessBufferedInput(state);
		}
		else if (armTimeout)
		{
			InstallTimeout(state, responseTimeout, L"The HTTP peer timed out before sending a response header.");
		}
	}

	void HttpRequestConnection::OnError(const WString& error, bool fatal)
	{
		auto state = lifecycle;
		SocketCallbackFrame frame(state);
		if (fatal)
		{
			ReportFatalError(state, error);
		}
		else
		{
			InvokeHttpCallback(state, false, [&](IHttpRequestCallback* installed)
			{
				installed->OnError(error, false);
			});
		}
	}

	void HttpRequestConnection::OnConnected()
	{
		auto state = lifecycle;
		SocketCallbackFrame frame(state);
		InvokeHttpCallback(state, false, [](IHttpRequestCallback* installed)
		{
			installed->OnConnected();
		});
	}

	void HttpRequestConnection::OnDisconnected()
	{
		auto state = lifecycle;
		SocketCallbackFrame frame(state);
		IAsyncSocketConnection* connection = nullptr;
		bool incomplete = false;
		bool surplusAfterHeldResponse = false;
		bool deferDisconnected = false;
		bool cancelTimeout = false;
		CS_LOCK(state->lockState)
		{
			state->peerDisconnected = true;
			state->timeoutArmed = false;
			cancelTimeout = !state->stopStarted;
			connection = state->socketConnection;
			if (!state->terminal && !state->stopStarted && state->direction == HttpRequestConnectionDirection::Client && state->heldResponse)
			{
				surplusAfterHeldResponse = state->fatalAfterResponse;
				incomplete = !surplusAfterHeldResponse;
				state->terminal = true;
				state->pendingWrite = nullptr;
				state->writePending = false;
			}
			else if (state->direction == HttpRequestConnectionDirection::Client && (state->responseDelivering || state->responseFinalizing))
			{
				deferDisconnected = true;
			}
			else
			{
				incomplete = !state->terminal && !state->stopStarted && (
					state->direction == HttpRequestConnectionDirection::Client
						? state->exchangeActive
						: !state->awaitingResponse && state->receiveBuffer.Count() > 0
					);
				state->terminal = true;
			}
		}
		if (connection)
		{
			connection->InstallCallback(nullptr);
		}
		CS_LOCK(state->lockState)
		{
			if (state->socketConnection == connection) state->socketConnection = nullptr;
			state->cvState.WakeAllPendings();
		}
		if (cancelTimeout)
		{
			state->timeoutController->CancelAndWait();
		}
		if (deferDisconnected)
		{
			return;
		}
		if (surplusAfterHeldResponse)
		{
			InvokeHttpCallback(state, true, [](IHttpRequestCallback* installed)
			{
				installed->OnError(L"The HTTP client received an unsolicited response after its exchange completed.", true);
			});
		}
		else if (incomplete)
		{
			InvokeHttpCallback(state, true, [](IHttpRequestCallback* installed)
			{
				installed->OnError(L"The HTTP peer disconnected while a message was incomplete.", true);
			});
		}
		NotifyDisconnected(state);
		StopConnection(state);
	}

	void HttpRequestConnection::OnInstalled(IAsyncSocketConnection* connection)
	{
		auto state = lifecycle;
		SocketCallbackFrame frame(state);
		CHECK_ERROR(connection == state->socketConnection, L"HttpRequestConnection was installed on an unexpected async socket connection.");
	}
}
