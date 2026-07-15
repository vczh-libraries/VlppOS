/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Async Socket HTTP/1.1 Connection

***********************************************************************/

#include "AsyncSocket_HttpRequest.h"

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

		bool ParseContentLength(const Array<vuint8_t>& value, bool& initialized, vint& contentLength)
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
				if (!ParseUnsignedDecimal(value, begin, end, number) || number > (vuint64_t)HttpBodySizeLimit)
				{
					return false;
				}
				if (!initialized)
				{
					initialized = true;
					contentLength = (vint)number;
				}
				else if (contentLength != (vint)number)
				{
					return false;
				}
				if (reading == value.Count()) return true;
				if (value[reading++] != ',') return false;
				if (reading == value.Count()) return false;
			}
		}

		bool ParseTransferCodings(const Array<vuint8_t>& value, List<WString>& codings)
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

		struct HttpFraming
		{
			bool							hasContentLength = false;
			vint							contentLength = 0;
			bool							hasTransferEncoding = false;
			List<WString>					transferCodings;
			bool							connectionClose = false;
		};

		bool AnalyzeFraming(const List<HttpField>& fields, HttpFraming& framing)
		{
			for (auto&& field : fields)
			{
				if (IsAsciiEqual(field.name, L"content-length"))
				{
					if (!ParseContentLength(field.value, framing.hasContentLength, framing.contentLength)) return false;
				}
				else if (IsAsciiEqual(field.name, L"transfer-encoding"))
				{
					framing.hasTransferEncoding = true;
					if (!ParseTransferCodings(field.value, framing.transferCodings)) return false;
				}
				else if (IsAsciiEqual(field.name, L"connection"))
				{
					framing.connectionClose |= HasConnectionClose(field.value);
				}
			}
			if (framing.hasTransferEncoding && framing.hasContentLength) return false;
			if (framing.hasTransferEncoding)
			{
				if (framing.transferCodings.Count() == 0 || !IsAsciiEqual(framing.transferCodings[framing.transferCodings.Count() - 1], L"chunked"))
				{
					return false;
				}
				for (vint i = 0; i + 1 < framing.transferCodings.Count(); i++)
				{
					if (IsAsciiEqual(framing.transferCodings[i], L"chunked")) return false;
				}
			}
			return true;
		}

		bool ParseHttpVersion(const vuint8_t* buffer, vint begin, vint end, HttpVersion& version)
		{
			const wchar_t* prefix = L"HTTP/";
			if (end - begin != 8) return false;
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
			return version.major == 1 && version.minor == 1;
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
			if (!ParseHttpVersion(buffer, secondSpace + 1, end, version)) return false;
			method = CopyAscii(buffer, 0, firstSpace, false);
			target = CopyAscii(buffer, firstSpace + 1, secondSpace, false);
			return true;
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

		enum class HttpMessageParsingResult
		{
			Succeeded,
			Incomplete,
			Invalid,
		};

		HttpMessageParsingResult ParseHttpMessage(
			const vuint8_t* buffer,
			vint availableBytes,
			bool requestMessage,
			Ptr<HttpRequest>& request,
			Ptr<HttpResponse>& response,
			vint& consumedBytes,
			bool& connectionClose
			)
		{
			consumedBytes = 0;
			connectionClose = false;
			vint startLineEnd = FindCrlf(buffer, 0, availableBytes);
			if (startLineEnd == -1)
			{
				auto possibleLineBytes = availableBytes > 0 && buffer[availableBytes - 1] == '\r' ? availableBytes - 1 : availableBytes;
				return possibleLineBytes > HttpRequestLineSizeLimit ? HttpMessageParsingResult::Invalid : HttpMessageParsingResult::Incomplete;
			}
			if (startLineEnd > HttpRequestLineSizeLimit) return HttpMessageParsingResult::Invalid;

			HttpVersion version;
			WString method;
			WString target;
			vint statusCode = 0;
			WString reason;
			if (requestMessage)
			{
				if (!ParseRequestLine(buffer, startLineEnd, version, method, target)) return HttpMessageParsingResult::Invalid;
			}
			else
			{
				if (!ParseStatusLine(buffer, startLineEnd, version, statusCode, reason)) return HttpMessageParsingResult::Invalid;
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
					return availableBytes - headersBegin >= HttpHeaderBlockSizeLimit ? HttpMessageParsingResult::Invalid : HttpMessageParsingResult::Incomplete;
				}
				if (lineEnd + 2 - headersBegin > HttpHeaderBlockSizeLimit) return HttpMessageParsingResult::Invalid;
				if (lineEnd == reading)
				{
					bodyBegin = lineEnd + 2;
					break;
				}
				HttpField field;
				if (!ParseFieldLine(buffer, reading, lineEnd, field)) return HttpMessageParsingResult::Invalid;
				headers.Add(std::move(field));
				reading = lineEnd + 2;
			}

			HttpFraming framing;
			if (!AnalyzeFraming(headers, framing)) return HttpMessageParsingResult::Invalid;
			if (!requestMessage && !framing.hasContentLength && !framing.hasTransferEncoding) return HttpMessageParsingResult::Invalid;

			HttpBody body;
			vint bodyBytes = 0;
			if (framing.hasTransferEncoding)
			{
				auto result = ParseHttpRequestBodyToChunks(buffer + bodyBegin, availableBytes - bodyBegin, body, bodyBytes);
				if (result == HttpRequestBodyParsingResult::Incomplete) return HttpMessageParsingResult::Incomplete;
				if (result == HttpRequestBodyParsingResult::Invalid) return HttpMessageParsingResult::Invalid;
			}
			else if (framing.hasContentLength)
			{
				if (availableBytes - bodyBegin < framing.contentLength) return HttpMessageParsingResult::Incomplete;
				bodyBytes = framing.contentLength;
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
				if (c > 0x7F || !IsTokenCharacter((vuint8_t)c) || (c >= L'A' && c <= L'Z')) return false;
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

		Ptr<AsyncSocketBuffer> SerializeHttpMessage(HttpRequest* request, HttpResponse* response, bool& connectionClose)
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
				CHECK_ERROR(request->method.Length() > 0, L"HTTP request serialization requires a method.");
				for (vint i = 0; i < request->method.Length(); i++)
				{
					CHECK_ERROR(request->method[i] <= 0x7F && IsTokenCharacter((vuint8_t)request->method[i]), L"HTTP request serialization received an invalid method.");
				}
				CHECK_ERROR(request->requestTarget.Length() > 0, L"HTTP request serialization requires a request target.");
				for (vint i = 0; i < request->requestTarget.Length(); i++)
				{
					CHECK_ERROR(request->requestTarget[i] >= 0x21 && request->requestTarget[i] <= 0x7E, L"HTTP request serialization received an invalid request target.");
				}
				startLineSize = 10;
				AddBoundedSize(startLineSize, request->method.Length(), HttpRequestLineSizeLimit, L"HTTP start line exceeds the configured size limit.");
				AddBoundedSize(startLineSize, request->requestTarget.Length(), HttpRequestLineSizeLimit, L"HTTP start line exceeds the configured size limit.");
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
			CHECK_ERROR(AnalyzeFraming(headers, framing), L"HTTP serialization received invalid or ambiguous body framing.");
			auto bodySize = ValidateBody(body);
			auto chunked = framing.hasTransferEncoding;
			auto generateContentLength = false;
			auto generateTransferEncoding = false;
			if (!framing.hasContentLength && !framing.hasTransferEncoding)
			{
				chunked = body.chunks.Count() > 1 || body.trailers.Count() > 0;
				generateTransferEncoding = chunked;
				generateContentLength = !chunked && (!requestMessage || body.chunks.Count() > 0);
			}
			if (!chunked)
			{
				CHECK_ERROR(body.trailers.Count() == 0 && body.chunks.Count() <= 1, L"A fixed HTTP body cannot contain multiple chunks or trailers.");
				if (framing.hasContentLength)
				{
					CHECK_ERROR(framing.contentLength == bodySize, L"HTTP Content-Length does not match the supplied body.");
				}
				else if (!generateContentLength)
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
			if (chunked)
			{
				for (auto&& chunk : body.chunks)
				{
					AddBoundedSize(wireSize, HexadecimalDigitCount(chunk.data.Count()) + 4, HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
					AddBoundedSize(wireSize, chunk.data.Count(), HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
				}
				AddBoundedSize(wireSize, 3, HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
				AddBoundedSize(wireSize, trailerBlockSize, HttpWireMessageSizeLimit, L"HTTP wire message exceeds the configured size limit.");
			}
			else
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

			if (chunked)
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
			else if (body.chunks.Count() == 1)
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

/***********************************************************************
HttpRequestBody
***********************************************************************/

	HttpRequestBodyParsingResult ParseHttpRequestBodyToChunks(
		const vuint8_t* buffer,
		vint availableBytes,
		HttpBody& output,
		vint& consumedBytes
		)
	{
		consumedBytes = 0;
		if (availableBytes < 0 || (!buffer && availableBytes > 0)) return HttpRequestBodyParsingResult::Invalid;
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
					return HttpRequestBodyParsingResult::Invalid;
				}
				return HttpRequestBodyParsingResult::Incomplete;
			}
			if (lineEnd - reading > HttpChunkSizeLineLimit) return HttpRequestBodyParsingResult::Invalid;
			if (lineEnd + 2 > HttpWireMessageSizeLimit) return HttpRequestBodyParsingResult::Invalid;
			vint numberEnd = reading;
			vuint64_t chunkSize = 0;
			while (numberEnd < lineEnd && IsHexDigit(buffer[numberEnd]))
			{
				auto digit = (vuint64_t)HexDigitValue(buffer[numberEnd++]);
				if (chunkSize > ((std::numeric_limits<vuint64_t>::max)() - digit) / 16) return HttpRequestBodyParsingResult::Invalid;
				chunkSize = chunkSize * 16 + digit;
			}
			if (numberEnd == reading || chunkSize > (vuint64_t)HttpBodySizeLimit) return HttpRequestBodyParsingResult::Invalid;
			if (numberEnd < lineEnd)
			{
				vint firstExtension = numberEnd;
				while (firstExtension < lineEnd && IsOws(buffer[firstExtension])) firstExtension++;
				if (firstExtension == lineEnd || buffer[firstExtension] != ';') return HttpRequestBodyParsingResult::Invalid;
			}
			vint extensionReading = numberEnd;
			if (!ParseSemicolonParameters(buffer, extensionReading, lineEnd)) return HttpRequestBodyParsingResult::Invalid;
			reading = lineEnd + 2;

			if (chunkSize == 0)
			{
				vint trailerBegin = reading;
				while (true)
				{
					vint trailerEnd = FindCrlf(buffer, reading, availableBytes);
					if (trailerEnd == -1)
					{
						return availableBytes - trailerBegin >= HttpTrailerBlockSizeLimit ? HttpRequestBodyParsingResult::Invalid : HttpRequestBodyParsingResult::Incomplete;
					}
					if (trailerEnd + 2 - trailerBegin > HttpTrailerBlockSizeLimit) return HttpRequestBodyParsingResult::Invalid;
					if (trailerEnd + 2 > HttpWireMessageSizeLimit) return HttpRequestBodyParsingResult::Invalid;
					if (trailerEnd == reading)
					{
						consumedBytes = trailerEnd + 2;
						output = std::move(parsed);
						return HttpRequestBodyParsingResult::Succeeded;
					}
					HttpField trailer;
					if (!ParseFieldLine(buffer, reading, trailerEnd, trailer) || trailer.name == L"content-length" || trailer.name == L"transfer-encoding")
					{
						return HttpRequestBodyParsingResult::Invalid;
					}
					parsed.trailers.Add(std::move(trailer));
					reading = trailerEnd + 2;
				}
			}

			if (chunkSize > (vuint64_t)(HttpBodySizeLimit - decodedBytes)) return HttpRequestBodyParsingResult::Invalid;
			vint chunkBytes = (vint)chunkSize;
			if (availableBytes - reading < chunkBytes) return HttpRequestBodyParsingResult::Incomplete;
			auto terminatorBytes = availableBytes - reading - chunkBytes;
			if (terminatorBytes == 0) return HttpRequestBodyParsingResult::Incomplete;
			if (buffer[reading + chunkBytes] != '\r') return HttpRequestBodyParsingResult::Invalid;
			if (terminatorBytes == 1) return HttpRequestBodyParsingResult::Incomplete;
			if (buffer[reading + chunkBytes] != '\r' || buffer[reading + chunkBytes + 1] != '\n') return HttpRequestBodyParsingResult::Invalid;
			if (reading > HttpWireMessageSizeLimit - chunkBytes - 2) return HttpRequestBodyParsingResult::Invalid;
			if (parsed.chunks.Count() >= HttpChunkCountLimit) return HttpRequestBodyParsingResult::Invalid;
			HttpBodyChunk chunk;
			chunk.data.Resize(chunkBytes);
			std::memcpy(&chunk.data[0], buffer + reading, (size_t)chunkBytes);
			parsed.chunks.Add(std::move(chunk));
			decodedBytes += chunkBytes;
			reading += chunkBytes + 2;
		}
	}

/***********************************************************************
IHttpRequestCallback
***********************************************************************/

	void IHttpRequestCallback::OnReadRequest(Ptr<HttpRequest>)
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
				vuint64_t						deadline = 0;
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
						auto now = DateTime::LocalTime().osMilliseconds;
						if (now >= state->deadline)
						{
							callback = state->callback;
							state->callback = {};
							state->armed = false;
							state->activeCallbacks++;
							break;
						}
						auto remaining = state->deadline - now;
						auto wait = remaining > (vuint64_t)(std::numeric_limits<vint>::max)()
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

			void Arm(const Func<void()>& callback) override
			{
				bool queueWorker = false;
				CS_LOCK(state->lock)
				{
					CHECK_ERROR(!state->armed, L"The HTTP timeout controller is already armed.");
					state->callback = callback;
					state->deadline = DateTime::LocalTime().osMilliseconds + HttpIncompleteMessageTimeout;
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
						state->deadline = DateTime::LocalTime().osMilliseconds + HttpIncompleteMessageTimeout;
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
		Ptr<HttpResponse>					heldResponse;
		bool							fatalAfterResponse = false;
		bool							responseDelivering = false;
		bool							responseFinalizing = false;
		Ptr<AsyncSocketBuffer>				deferredRequestWrite;
		bool							deferredRequestClose = false;
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

	thread_local HttpRequestConnection::CallbackFrame* HttpRequestConnection::currentCallbackFrame = nullptr;
	thread_local HttpRequestConnection::SocketCallbackFrame* HttpRequestConnection::currentSocketCallbackFrame = nullptr;

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

	void HttpRequestConnection::InstallTimeout(Ptr<Lifecycle> state, const WString& error)
	{
		auto captured = state;
		auto capturedError = error;
		try
		{
			state->timeoutController->Arm(Func<void()>([captured, capturedError]()
			{
				bool expired = false;
				CS_LOCK(captured->lockState)
				{
					if (captured->timeoutArmed && !captured->stopStarted && !captured->terminal)
					{
						captured->timeoutArmed = false;
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
			}
			else if (state->deferredRequestWrite && !state->stopStarted && !state->terminal && state->socketConnection)
			{
				deferredRequestWrite = std::move(state->deferredRequestWrite);
				state->exchangeActive = true;
				state->closeAfterExchange = state->deferredRequestClose;
				state->deferredRequestClose = false;
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
		bool invalid = false;

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
			request,
			response,
			consumedBytes,
			messageClose
			);
		if (result == HttpMessageParsingResult::Incomplete)
		{
			bool armTimeout = false;
			if (!state->timeoutArmed)
			{
				state->timeoutArmed = true;
				armTimeout = true;
			}
			state->lockState.Leave();
			if (armTimeout)
			{
				InstallTimeout(state, L"The HTTP peer timed out while sending an incomplete message.");
			}
			else
			{
				RefreshTimeout(state);
			}
			return;
		}
		if (result == HttpMessageParsingResult::Invalid)
		{
			state->parserFailed = true;
			invalid = true;
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

		if (invalid)
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
		auto nestedCallback = callbackDepth > 0 || socketCallbackDepth > 0;
		IAsyncSocketConnection* connection = nullptr;
		bool executeStop = false;
		bool nestedFollower = false;
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
			if (!nestedCallback)
			{
				while (state->activeSocketCalls > 0) state->cvState.SleepWith(state->lockState);
			}
			connection = state->socketConnection;
			executeStop = true;
		}
		else if (nestedCallback)
		{
			connection = state->socketConnection;
			nestedFollower = true;
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

		state->timeoutController->CancelAndWait();
		if (nestedFollower)
		{
			if (connection && socketCallbackDepth > 0) connection->Stop();
			NotifyDisconnected(state);
			return;
		}
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

	void HttpRequestConnection::SendRequest(Ptr<HttpRequest> request)
	{
		auto state = lifecycle;
		CHECK_ERROR(state->direction == HttpRequestConnectionDirection::Client, L"HttpRequestConnection::SendRequest is only available on a client connection.");
		CHECK_ERROR(request, L"HttpRequestConnection::SendRequest requires a request.");
		bool requestClose = false;
		auto buffer = SerializeHttpMessage(request.Obj(), nullptr, requestClose);
		IAsyncSocketConnection* connection = nullptr;
		Ptr<Object> callRetainer;
		bool canSend = false;
		bool deferSend = false;
		auto callbackDepth = CurrentCallbackDepth(state);
		CS_LOCK(state->lockState)
		{
			auto commonStateAvailable = !state->stopStarted && !state->terminal && !state->peerDisconnected
				&& !state->exchangeActive && !state->writePending && !state->deferredRequestWrite
				&& !state->responseFinalizing && !state->fatalAfterResponse && !state->closeAfterExchange && state->socketConnection;
			if (commonStateAvailable && callbackDepth > 0 && state->responseDelivering)
			{
				state->deferredRequestWrite = buffer;
				state->deferredRequestClose = requestClose;
				deferSend = true;
				canSend = true;
			}
			else if (commonStateAvailable && !state->responseDelivering)
			{
				state->exchangeActive = true;
				state->closeAfterExchange = requestClose;
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
		bool responseClose = false;
		auto buffer = SerializeHttpMessage(nullptr, response.Obj(), responseClose);
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
				state->parserFailed = true;
				tooLarge = true;
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
		CS_LOCK(state->lockState)
		{
			if (state->stopStarted || state->terminal) return;
			state->writePending = false;
			serverSide = state->direction == HttpRequestConnectionDirection::Server;
			if (serverSide)
			{
				state->awaitingResponse = false;
				closeAfterDelivery = state->closeAfterExchange;
			}
			else if (state->heldResponse)
			{
				response = std::move(state->heldResponse);
				state->exchangeActive = false;
				state->responseDelivering = true;
				closeAfterDelivery = state->closeAfterExchange;
			}
			else if (!state->peerDisconnected && !state->timeoutArmed)
			{
				state->timeoutArmed = true;
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
			InstallTimeout(state, L"The HTTP peer timed out before sending a response header.");
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
		CS_LOCK(state->lockState)
		{
			state->peerDisconnected = true;
			state->timeoutArmed = false;
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
		state->timeoutController->CancelAndWait();
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
