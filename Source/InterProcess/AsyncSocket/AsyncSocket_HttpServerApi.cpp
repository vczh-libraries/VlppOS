#include "AsyncSocket_HttpServerApi.h"

#if defined VCZH_MSVC
namespace vl::inter_process::async_tcp_socket::windows_socket
{
	extern Ptr<IAsyncSocketServer> CreateDefaultAsyncSocketServer(vint port);
}
#elif defined VCZH_GCC && defined VCZH_APPLE
namespace vl::inter_process::async_tcp_socket::macos_socket
{
	extern Ptr<IAsyncSocketServer> CreateDefaultAsyncSocketServer(vint port);
}
#elif defined VCZH_GCC
namespace vl::inter_process::async_tcp_socket::linux_socket
{
	extern Ptr<IAsyncSocketServer> CreateDefaultAsyncSocketServer(vint port);
}
#endif

#include <limits>

namespace vl::inter_process::async_tcp_socket
{
	using namespace collections;

	namespace
	{
		class ApiRegistration;
		class RegistryEntry;
		class SharedServer;
		class ConnectionState;

		struct PrefixInfo
		{
			WString						normalizedUrl;
			WString						authority;
			WString						decodedPath;
			vint						port = 0;
		};

		wchar_t Lower(wchar_t c)
		{
			return c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c;
		}

		WString Lower(const WString& text)
		{
			if (text.Length() == 0) return {};
			auto buffer = new wchar_t[text.Length() + 1];
			for (vint i = 0; i < text.Length(); i++) buffer[i] = Lower(text[i]);
			buffer[text.Length()] = 0;
			return WString::TakeOver(buffer, text.Length());
		}

		bool Token(wchar_t c)
		{
			if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return true;
			switch (c)
			{
			case L'!': case L'#': case L'$': case L'%': case L'&': case L'\'': case L'*':
			case L'+': case L'-': case L'.': case L'^': case L'_': case L'`': case L'|': case L'~':
				return true;
			default:
				return false;
			}
		}

		vint Hex(wchar_t c)
		{
			if (c >= L'0' && c <= L'9') return c - L'0';
			if (c >= L'a' && c <= L'f') return c - L'a' + 10;
			if (c >= L'A' && c <= L'F') return c - L'A' + 10;
			return -1;
		}

		bool DecodePath(const WString& raw, WString& decoded)
		{
			List<vuint8_t> bytes;
			for (vint i = 0; i < raw.Length(); i++)
			{
				auto c = raw[i];
				if (c == L'%')
				{
					if (i + 2 >= raw.Length()) return false;
					auto h1 = Hex(raw[i + 1]);
					auto h2 = Hex(raw[i + 2]);
					if (h1 < 0 || h2 < 0) return false;
					auto b = (vuint8_t)(h1 * 16 + h2);
					if (b == 0 || b == '/' || b == '\\') return false;
					bytes.Add(b);
					i += 2;
				}
				else
				{
					if (c == 0 || c == L'\\' || c > 0x7F) return false;
					bytes.Add((vuint8_t)c);
				}
			}

			Array<vuint8_t> encoded(bytes.Count());
			for (vint i = 0; i < bytes.Count(); i++) encoded[i] = bytes[i];
			return ::vl::inter_process::async_tcp_socket::DecodeStrictUtf8(
				encoded.Count() == 0 ? nullptr : &encoded[0],
				encoded.Count(),
				decoded
				);
		}

		bool ParseAuthority(const WString& text, WString& authority, vint& port)
		{
			vint colon = -1;
			for (vint i = 0; i < text.Length(); i++)
			{
				if (text[i] == L':')
				{
					if (colon != -1) return false;
					colon = i;
				}
				else if (text[i] == L'@' || text[i] == L'/' || text[i] == L'?' || text[i] == L'#') return false;
			}
			if (colon <= 0 || colon + 1 >= text.Length()) return false;
			auto host = Lower(text.Left(colon));
			if (host != L"localhost" && host != L"127.0.0.1") return false;
			vuint64_t number = 0;
			for (vint i = colon + 1; i < text.Length(); i++)
			{
				if (text[i] < L'0' || text[i] > L'9') return false;
				number = number * 10 + text[i] - L'0';
				if (number > 65535) return false;
			}
			if (number == 0) return false;
			port = (vint)number;
			authority = host + L":" + itow(port);
			return true;
		}

		PrefixInfo ParsePrefix(const WString& url)
		{
#define ERROR_PREFIX L"vl::inter_process::async_tcp_socket::SocketHttpServerApi::SocketHttpServerApi(const WString&, bool)#"
			CHECK_ERROR(url.Length() >= 7 && url.Left(7) == L"http://", ERROR_PREFIX L"Requires a plain http:// prefix.");
			auto rest = url.Right(url.Length() - 7);
			vint slash = rest.IndexOf(L'/');
			auto rawAuthority = slash == -1 ? rest : rest.Left(slash);
			auto rawPath = slash == -1 ? WString::Empty : rest.Right(rest.Length() - slash);
			CHECK_ERROR(rawAuthority.Length() > 0 && rawPath.IndexOf(L'?') == -1 && rawPath.IndexOf(L'#') == -1, ERROR_PREFIX L"Query and fragment components are not supported.");
			PrefixInfo info;
			CHECK_ERROR(ParseAuthority(rawAuthority, info.authority, info.port), ERROR_PREFIX L"Requires localhost or 127.0.0.1 with an explicit port in 1..65535.");
			while (rawPath.Length() > 0 && rawPath[rawPath.Length() - 1] == L'/') rawPath = rawPath.Left(rawPath.Length() - 1);
			CHECK_ERROR(DecodePath(rawPath, info.decodedPath), ERROR_PREFIX L"The path contains invalid escaping, UTF-8, NUL, or an encoded separator.");
			info.normalizedUrl = L"http://" + info.authority + rawPath;
			return info;
#undef ERROR_PREFIX
		}

		WString TrimOws(const WString& text)
		{
			vint begin = 0, end = text.Length();
			while (begin < end && (text[begin] == L' ' || text[begin] == L'\t')) begin++;
			while (begin < end && (text[end - 1] == L' ' || text[end - 1] == L'\t')) end--;
			return text.Sub(begin, end - begin);
		}

		bool AnalyzeCacheControl(const Array<vuint8_t>& bytes, bool& noStore)
		{
			WString text;
			if (!DecodeAsciiHttpFieldValue(bytes, text)) return false;
			noStore = false;
			vint begin = 0;
			bool quoted = false, escaped = false;
			for (vint i = 0; i <= text.Length(); i++)
			{
				if (i < text.Length())
				{
					auto c = text[i];
					if (escaped) { escaped = false; continue; }
					if (quoted)
					{
						if (c == L'\\') escaped = true;
						else if (c == L'"') quoted = false;
						continue;
					}
					if (c == L'"') { quoted = true; continue; }
					if (c != L',') continue;
				}
				else if (quoted || escaped) return false;

				auto item = TrimOws(text.Sub(begin, i - begin));
				if (item.Length() == 0) return false;
				auto equals = item.IndexOf(L'=');
				auto name = TrimOws(equals == -1 ? item : item.Left(equals));
				if (name.Length() == 0) return false;
				for (vint j = 0; j < name.Length(); j++) if (!Token(name[j])) return false;
				if (equals != -1 && TrimOws(item.Right(item.Length() - equals - 1)).Length() == 0) return false;
				if (Lower(name) == L"no-store")
				{
					if (equals != -1) return false;
					noStore = true;
				}
				begin = i + 1;
			}
			return true;
		}

		bool ValidHttpDate(const Array<vuint8_t>& bytes)
		{
			WString text;
			if (!DecodeAsciiHttpFieldValue(bytes, text)) return false;
			text = TrimOws(text);
			if (text.Length() != 29
				|| text[3] != L',' || text[4] != L' ' || text[7] != L' ' || text[11] != L' '
				|| text[16] != L' ' || text[19] != L':' || text[22] != L':' || text[25] != L' '
				|| text.Sub(26, 3) != L"GMT") return false;
			auto dayName = text.Sub(0, 3);
			if (dayName != L"Sun" && dayName != L"Mon" && dayName != L"Tue" && dayName != L"Wed" && dayName != L"Thu" && dayName != L"Fri" && dayName != L"Sat") return false;
			vint digitIndexes[] = { 5, 6, 12, 13, 14, 15, 17, 18, 20, 21, 23, 24 };
			for (auto index : digitIndexes)
			{
				if (text[index] < L'0' || text[index] > L'9') return false;
			}
			auto monthName = text.Sub(8, 3);
			vint month =
				monthName == L"Jan" ? 1 : monthName == L"Feb" ? 2 : monthName == L"Mar" ? 3 :
				monthName == L"Apr" ? 4 : monthName == L"May" ? 5 : monthName == L"Jun" ? 6 :
				monthName == L"Jul" ? 7 : monthName == L"Aug" ? 8 : monthName == L"Sep" ? 9 :
				monthName == L"Oct" ? 10 : monthName == L"Nov" ? 11 : monthName == L"Dec" ? 12 : 0;
			if (month == 0) return false;
			auto day = (text[5] - L'0') * 10 + text[6] - L'0';
			auto year = (text[12] - L'0') * 1000 + (text[13] - L'0') * 100 + (text[14] - L'0') * 10 + text[15] - L'0';
			auto hour = (text[17] - L'0') * 10 + text[18] - L'0';
			auto minute = (text[20] - L'0') * 10 + text[21] - L'0';
			auto second = (text[23] - L'0') * 10 + text[24] - L'0';
			if (year == 0 || hour > 23 || minute > 59 || second > 60) return false;
			vint monthDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
			if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0))) monthDays[1] = 29;
			return day >= 1 && day <= monthDays[month - 1];
		}

		const wchar_t* Reason(vint code)
		{
			switch (code)
			{
			case 200: return L"OK"; case 204: return L"No Content"; case 304: return L"Not Modified";
			case 400: return L"Bad Request"; case 404: return L"Not Found"; case 405: return L"Method Not Allowed";
			case 408: return L"Request Timeout"; case 413: return L"Payload Too Large"; case 414: return L"URI Too Long";
			case 415: return L"Unsupported Media Type"; case 417: return L"Expectation Failed";
			case 431: return L"Request Header Fields Too Large"; case 500: return L"Internal Server Error";
			case 501: return L"Not Implemented"; case 505: return L"HTTP Version Not Supported";
			default: return L"Response";
			}
		}

		void ValidateConvenienceResponseArguments(vint statusCode, const WString& reason, const WString& contentType, vint bodySize)
		{
			CHECK_ERROR(statusCode >= 200 && statusCode <= 599, L"Invalid HTTP response status.");
			for (vint i = 0; i < reason.Length(); i++)
			{
				CHECK_ERROR((vuint32_t)reason[i] >= 0x20 && (vuint32_t)reason[i] <= 0x7E, L"Invalid HTTP response reason phrase.");
			}
			if (contentType != WString::Empty)
			{
				CreateAsciiHttpField(L"content-type", contentType);
			}
			CHECK_ERROR(bodySize <= HttpBodySizeLimit, L"The response body is too large.");
		}

		Ptr<HttpResponse> CreateConvenienceResponse(vint statusCode, const WString& reason, const WString& contentType, Array<vuint8_t>&& body)
		{
			auto response = Ptr(new HttpResponse);
			response->statusCode = statusCode;
			response->reason = reason;
			if (contentType != WString::Empty)
			{
				response->headers.Add(CreateAsciiHttpField(L"content-type", contentType));
			}
			SetHttpBodyBytes(response->body, std::move(body));
			return response;
		}

		WString Two(vint value) { return value < 10 ? L"0" + itow(value) : itow(value); }

		WString DateHeader()
		{
			const wchar_t* days[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
			const wchar_t* months[] = { L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
			auto now = DateTime::UtcTime();
			return WString::Unmanaged(days[now.dayOfWeek]) + L", " + Two(now.day) + L" " + months[now.month - 1] + L" " + itow(now.year) + L" " + Two(now.hour) + L":" + Two(now.minute) + L":" + Two(now.second) + L" GMT";
		}

		Ptr<HttpResponse> Normalize(Ptr<HttpResponse> input, const WString& method)
		{
			CHECK_ERROR(input, L"SocketHttpRequestContext::Respond requires a response.");
			CHECK_ERROR(input->statusCode >= 200 && input->statusCode <= 599, L"Invalid HTTP response status.");
			CHECK_ERROR(input->body.trailers.Count() == 0, L"Response trailers are not supported.");
			Array<vuint8_t> body;
			CHECK_ERROR(FlattenHttpBody(input->body, body), L"The response body is too large.");
			auto bodySize = body.Count();
			auto bodyAllowed = method != L"HEAD" && input->statusCode != 204 && input->statusCode != 304;
			auto length = bodyAllowed || method == L"HEAD" ? bodySize : 0;
			auto output = Ptr(new HttpResponse);
			output->statusCode = input->statusCode;
			output->reason = input->reason == WString::Empty ? Reason(input->statusCode) : input->reason;
			for (vint i = 0; i < output->reason.Length(); i++)
			{
				CHECK_ERROR(output->reason[i] >= 0x20 && output->reason[i] <= 0x7E, L"Invalid HTTP response reason phrase.");
			}

			List<HttpField> normalizedHeaders;
			for (auto&& source : input->headers)
			{
				auto copy = CreateAsciiHttpField(source.name, WString::Empty);
				for (auto c : source.value) CHECK_ERROR(c == '\t' || (c >= 0x20 && c != 0x7F), L"Invalid response field value.");
				copy.value.Resize(source.value.Count());
				for (vint i = 0; i < source.value.Count(); i++) copy.value[i] = source.value[i];
				normalizedHeaders.Add(std::move(copy));
			}

			HttpFraming framing;
			CHECK_ERROR(AnalyzeHttpFraming(normalizedHeaders, framing) == HttpFramingAnalysisResult::Succeeded, L"Invalid or unsupported HTTP response framing.");
			CHECK_ERROR(framing.kind != HttpFramingKind::Chunked, L"Response Transfer-Encoding is not supported.");
			CHECK_ERROR(framing.contentLengthValuesPlainDecimal, L"Application Content-Length values must be plain decimal numbers.");
			CHECK_ERROR(framing.contentLength <= (vuint64_t)std::numeric_limits<vint>::max(), L"The application Content-Length is too large.");

			bool date = false, noStore = false, cors = false;
			for (auto&& source : normalizedHeaders)
			{
				if (source.name == L"content-length") continue;
				if (source.name == L"date")
				{
					CHECK_ERROR(!date && ValidHttpDate(source.value), L"A response requires at most one valid IMF-fixdate Date field.");
					date = true;
				}
				else if (source.name == L"cache-control")
				{
					bool sourceNoStore = false;
					CHECK_ERROR(AnalyzeCacheControl(source.value, sourceNoStore), L"Invalid Cache-Control response field.");
					noStore |= sourceNoStore;
				}
				else if (source.name == L"access-control-allow-origin")
				{
					WString value;
					CHECK_ERROR(!cors && DecodeAsciiHttpFieldValue(source.value, value) && TrimOws(value) == L"*", L"The loopback CORS policy requires exactly one Access-Control-Allow-Origin: * field.");
					cors = true;
				}
				HttpField copy;
				copy.name = source.name;
				copy.value.Resize(source.value.Count());
				if (source.value.Count() > 0)
				{
					memcpy(&copy.value[0], &source.value[0], source.value.Count());
				}
				output->headers.Add(std::move(copy));
			}
			auto contentLength = framing.kind == HttpFramingKind::ContentLength;
			auto suppliedLength = (vint)framing.contentLength;
			auto outputLength = length;
			if (contentLength && input->statusCode == 304 && method != L"HEAD") outputLength = suppliedLength;
			else CHECK_ERROR(!contentLength || suppliedLength == length, L"Contradictory response Content-Length.");
			if (!date) output->headers.Add(CreateAsciiHttpField(L"date", DateHeader()));
			if (!noStore) output->headers.Add(CreateAsciiHttpField(L"cache-control", L"no-store"));
			if (!cors) output->headers.Add(CreateAsciiHttpField(L"access-control-allow-origin", L"*"));
			if (input->statusCode != 204) output->headers.Add(CreateAsciiHttpField(L"content-length", itow(outputLength)));
			if (bodyAllowed && bodySize > 0)
			{
				SetHttpBodyBytes(output->body, std::move(body));
			}
			return output;
		}

		Ptr<HttpResponse> Automatic(vint code, bool close, bool preflight = false)
		{
			auto response = Ptr(new HttpResponse);
			response->statusCode = code;
			response->reason = Reason(code);
			if (close) response->headers.Add(CreateAsciiHttpField(L"connection", L"close"));
			if (preflight)
			{
				response->headers.Add(CreateAsciiHttpField(L"access-control-allow-methods", L"GET, HEAD, POST, OPTIONS"));
				response->headers.Add(CreateAsciiHttpField(L"access-control-allow-headers", L"Accept, Content-Type"));
				response->headers.Add(CreateAsciiHttpField(L"allow", L"GET, HEAD, POST, OPTIONS"));
			}
			return response;
		}
	}
}

namespace vl::inter_process::async_tcp_socket
{
	using namespace collections;

	namespace
	{
		class ApiRegistration : public Object
		{
		public:
			struct Frame
			{
				ApiRegistration*				registration;
				Frame*						previous;
				Frame(ApiRegistration* value);
				~Frame();
			};
			static thread_local Frame*		currentFrame;

			PrefixInfo						prefix;
			bool							respondToOptions;
			Ptr<RegistryEntry>				entry;
			CriticalSection					lock;
			ConditionVariable				cv;
			List<Ptr<SocketHttpRequestContext>> contexts;
			List<Func<void()>>				contextCancellations;
			Func<void(Ptr<SocketHttpRequestContext>)> requestCallback;
			Func<void()>						stoppingCallback;
			vint							activeCallbacks = 0;
			bool							stopping = false;
			bool							stopped = false;

			ApiRegistration(const PrefixInfo& _prefix, bool _respondToOptions)
				: prefix(_prefix), respondToOptions(_respondToOptions)
			{
			}

			static vint Depth(ApiRegistration* registration)
			{
				vint depth = 0;
				for (auto frame = currentFrame; frame; frame = frame->previous) if (frame->registration == registration) depth++;
				return depth;
			}

			bool AddContext(Ptr<SocketHttpRequestContext> context, const Func<void()>& cancellation)
			{
				bool added = false;
				CS_LOCK(lock)
				{
					if (!stopping)
					{
						contexts.Add(context);
						contextCancellations.Add(cancellation);
						added = true;
					}
				}
				return added;
			}

			void RemoveContext(SocketHttpRequestContext* context)
			{
				CS_LOCK(lock)
				{
					vint index = -1;
					for (vint i = 0; i < contexts.Count(); i++)
					{
						if (contexts[i].Obj() == context) { index = i; break; }
					}
					if (index != -1)
					{
						contexts.RemoveAt(index);
						contextCancellations.RemoveAt(index);
					}
					cv.WakeAllPendings();
				}
			}

			bool InvokeRequest(Ptr<SocketHttpRequestContext> context)
			{
				Func<void(Ptr<SocketHttpRequestContext>)> callback;
				CS_LOCK(lock)
				{
					if (!stopping && requestCallback)
					{
						callback = requestCallback;
						activeCallbacks++;
					}
				}
				if (!callback) return false;
				Frame frame(this);
				try
				{
					callback(context);
				}
				catch (...)
				{
					CS_LOCK(lock) { activeCallbacks--; cv.WakeAllPendings(); }
					throw;
				}
				CS_LOCK(lock) { activeCallbacks--; cv.WakeAllPendings(); }
				return true;
			}

			bool ReserveCompletion(const Func<void(bool)>& callback)
			{
				if (!callback) return false;
				bool reserved = false;
				CS_LOCK(lock)
				{
					if (!stopped) { activeCallbacks++; reserved = true; }
				}
				return reserved;
			}

			void InvokeReservedCompletion(const Func<void(bool)>& callback, bool succeeded)
			{
				Frame frame(this);
				try { callback(succeeded); } catch (...) {}
				CS_LOCK(lock) { activeCallbacks--; cv.WakeAllPendings(); }
			}

			void Stop(bool notify)
			{
				List<Func<void()>> cancelling;
				Func<void()> callback;
				auto depth = Depth(this);
				lock.Enter();
				if (stopped) { lock.Leave(); return; }
				if (stopping)
				{
					if (depth > 0) { lock.Leave(); return; }
					while (!stopped) cv.SleepWith(lock);
					lock.Leave();
					return;
				}
				stopping = true;
				if (notify) callback = stoppingCallback;
				for (auto cancellation : contextCancellations) cancelling.Add(cancellation);
				lock.Leave();

				if (callback)
				{
					CS_LOCK(lock) { activeCallbacks++; }
					Frame frame(this);
					try { callback(); } catch (...) {}
					CS_LOCK(lock) { activeCallbacks--; cv.WakeAllPendings(); }
				}
				for (auto cancellation : cancelling) cancellation();
				lock.Enter();
				while (contexts.Count() > 0 || activeCallbacks > depth) cv.SleepWith(lock);
				requestCallback = {};
				stoppingCallback = {};
				stopped = true;
				cv.WakeAllPendings();
				lock.Leave();
			}

			bool IsStopped()
			{
				bool result = false;
				CS_LOCK(lock) { result = stopping; }
				return result;
			}
		};

		thread_local ApiRegistration::Frame* ApiRegistration::currentFrame = nullptr;
		ApiRegistration::Frame::Frame(ApiRegistration* value) : registration(value), previous(currentFrame) { currentFrame = this; }
		ApiRegistration::Frame::~Frame() { currentFrame = previous; }

		class RegistryEntry : public Object
		{
		public:
			vint							port;
			vint							bindAttempt;
			EventObject						readyEvent;
			List<Ptr<ApiRegistration>>		registrations;
			Ptr<HttpRequestServer>				server;
			WString							failureMessage;
			AsyncSocketServerStartFailure		failure = AsyncSocketServerStartFailure::Other;
			bool ready = false, succeeded = false, terminal = false;

			RegistryEntry(vint _port, vint _bindAttempt)
				: port(_port)
				, bindAttempt(_bindAttempt)
			{
				CHECK_ERROR(readyEvent.CreateManualUnsignal(false), L"Failed to create a socket HTTP registry event.");
			}
		};

		class ConnectionState : public Object
		{
		public:
			CriticalSection					lock;
			Ptr<RegistryEntry>				entry;
			IHttpRequestConnection*			connection = nullptr;
			Ptr<SocketHttpRequestContext>	context;
			bool automaticWrite = false;
			bool closeAfterWrite = false;
			bool terminal = false;

			ConnectionState(Ptr<RegistryEntry> _entry) : entry(_entry) {}
		};
	}

	class SocketHttpRequestContext::Impl : public Object
	{
	public:
		enum class State { Pending, Sending, Completed, Cancelled };
		CriticalSection					lock;
		Ptr<ConnectionState>				connectionState;
		Ptr<ApiRegistration>				registration;
		Ptr<HttpRequest>					request;
		WString relativePath, query;
		SocketHttpRequestContext*			owner = nullptr;
		Func<void(bool)>					completion;
		State state = State::Pending;

		Impl(Ptr<ConnectionState> cs, Ptr<ApiRegistration> reg, Ptr<HttpRequest> req, const WString& relative, const WString& _query)
			: connectionState(cs), registration(reg), request(req), relativePath(relative), query(_query)
		{
		}

		bool Finish(bool succeeded, bool includePending, bool includeSending, bool stopConnection)
		{
			Func<void(bool)> callback;
			bool won = false;
			CS_LOCK(lock)
			{
				if ((includeSending && state == State::Sending) || (includePending && state == State::Pending))
				{
					if (state == State::Sending)
					{
						callback = completion;
						completion = {};
					}
					state = succeeded ? State::Completed : State::Cancelled;
					won = true;
				}
			}
			if (!won) return false;
			auto completionReserved = registration->ReserveCompletion(callback);
			IHttpRequestConnection* connection = nullptr;
			CS_LOCK(connectionState->lock)
			{
				if (connectionState->context.Obj() == owner) connectionState->context = nullptr;
				if (stopConnection) { connectionState->terminal = true; connection = connectionState->connection; }
			}
			registration->RemoveContext(owner);
			if (connection) try { connection->Stop(); } catch (...) {}
			if (completionReserved) registration->InvokeReservedCompletion(callback, succeeded);
			return true;
		}
	};

	class SocketHttpServerApiDispatcher : public Object, public virtual IHttpRequestCallback
	{
	private:
		CriticalSection					lockSelf;
		Ptr<SocketHttpServerApiDispatcher>	selfReference;
		Ptr<ConnectionState>				state;
		Ptr<SocketHttpServerApiDispatcher> Retain();
		void SendAutomatic(vint code, bool close, bool preflight = false);
		void Process(Ptr<HttpRequest> request);

	public:
		SocketHttpServerApiDispatcher(Ptr<RegistryEntry> entry) : state(Ptr(new ConnectionState(entry))) {}
		void InitializeSelf(Ptr<SocketHttpServerApiDispatcher> self) { CS_LOCK(lockSelf) { selfReference = self; } }
		void OnReadRequest(Ptr<HttpRequest> request) override;
		void OnReadRequestFailure(HttpRequestFailure failure) override;
		void OnWriteCompleted() override;
		void OnError(const WString& error, bool fatal) override;
		void OnDisconnected() override;
		void OnInstalled(IHttpRequestConnection* connection) override;
	};

	namespace
	{
		void UnexpectedStop(Ptr<RegistryEntry> entry);

		class SharedServer : public HttpRequestServer
		{
		private:
			CriticalSection				lock;
			Ptr<SharedServer>				selfReference;
			Ptr<RegistryEntry>				entry;
		protected:
			void OnServerStopped() override;
		public:
			SharedServer(
				Ptr<IAsyncSocketServer> server,
				Ptr<RegistryEntry> _entry,
				const Func<Ptr<IHttpRequestTimeoutController>()>& timeoutControllerFactory
				)
				: HttpRequestServer(server, timeoutControllerFactory)
				, entry(_entry)
			{
			}
			void InitializeSelf(Ptr<SharedServer> self) { CS_LOCK(lock) { selfReference = self; } }
			WaitForClientResult OnClientConnected(IHttpRequestConnection* connection) override;
			void StopAndRelease();
		};

		BEGIN_GLOBAL_STORAGE_CLASS(SocketHttpRegistry)
			SpinLock						lock;
			Dictionary<vint, Ptr<RegistryEntry>> entries;
			Func<Ptr<IAsyncSocketServer>(vint)>	listenerFactory;
			Func<Ptr<IHttpRequestTimeoutController>()>
										timeoutControllerFactory;
		INITIALIZE_GLOBAL_STORAGE_CLASS
		FINALIZE_GLOBAL_STORAGE_CLASS
			List<Ptr<HttpRequestServer>> servers;
			List<Ptr<RegistryEntry>> retainedEntries;
			SPIN_LOCK(lock)
			{
				for (auto entry : entries.Values())
				{
					retainedEntries.Add(entry);
					if (entry->server) servers.Add(entry->server);
				}
				entries.Clear();
				listenerFactory = {};
				timeoutControllerFactory = {};
			}
			for (auto server : servers)
			{
				auto shared = server.Cast<SharedServer>();
				if (shared) shared->StopAndRelease(); else server->Stop();
			}
		END_GLOBAL_STORAGE_CLASS(SocketHttpRegistry)
	}
}

namespace vl::inter_process::async_tcp_socket
{
	using namespace collections;

	namespace
	{
		Ptr<IAsyncSocketServer> CreateListener(vint port)
		{
			Func<Ptr<IAsyncSocketServer>(vint)> factory;
			auto& registry = GetSocketHttpRegistry();
			SPIN_LOCK(registry.lock) { factory = registry.listenerFactory; }
			if (factory) return factory(port);
#if defined VCZH_MSVC
			return windows_socket::CreateDefaultAsyncSocketServer(port);
#elif defined VCZH_GCC && defined VCZH_APPLE
			return macos_socket::CreateDefaultAsyncSocketServer(port);
#elif defined VCZH_GCC
			return linux_socket::CreateDefaultAsyncSocketServer(port);
#else
			CHECK_FAIL(L"No async socket server is available on this platform.");
#endif
		}

		Func<Ptr<IHttpRequestTimeoutController>()> GetTimeoutControllerFactory()
		{
			Func<Ptr<IHttpRequestTimeoutController>()> factory;
			auto& registry = GetSocketHttpRegistry();
			SPIN_LOCK(registry.lock) { factory = registry.timeoutControllerFactory; }
			return factory;
		}

		bool CurrentEntry(SocketHttpRegistry& registry, Ptr<RegistryEntry> entry)
		{
			auto index = registry.entries.Keys().IndexOf(entry->port);
			return index != -1 && registry.entries.Values()[index] == entry;
		}

		bool Duplicate(Ptr<RegistryEntry> entry, Ptr<ApiRegistration> registration)
		{
			for (auto existing : entry->registrations)
			{
				if (existing != registration && existing->prefix.authority == registration->prefix.authority && existing->prefix.decodedPath == registration->prefix.decodedPath) return true;
			}
			return false;
		}

		void RegisterApi(Ptr<ApiRegistration> registration)
		{
			auto& registry = GetSocketHttpRegistry();
			vint observedAttempt = 0;
			WString lastFailure = L"The listener address is already in use.";
			while (true)
			{
				Ptr<RegistryEntry> entry;
				bool creator = false;
				SPIN_LOCK(registry.lock)
				{
					auto index = registry.entries.Keys().IndexOf(registration->prefix.port);
					if (index != -1) entry = registry.entries.Values()[index];
				}
				if (!entry)
				{
					if (observedAttempt >= 5)
					{
						throw AsyncSocketServerStartException(AsyncSocketServerStartFailure::AddressInUse, lastFailure);
					}
					auto candidate = Ptr(new RegistryEntry(registration->prefix.port, observedAttempt + 1));
					SPIN_LOCK(registry.lock)
					{
						auto index = registry.entries.Keys().IndexOf(registration->prefix.port);
						if (index == -1)
						{
							entry = candidate;
							entry->registrations.Add(registration);
							registration->entry = entry;
							registry.entries.Add(entry->port, entry);
							creator = true;
						}
						else entry = registry.entries.Values()[index];
					}
				}
				if (entry && entry->bindAttempt > observedAttempt) observedAttempt = entry->bindAttempt;

				if (!creator)
				{
					CHECK_ERROR(entry->readyEvent.Wait(), L"Failed to wait for a shared socket HTTP listener.");
					bool joined = false, duplicate = false, retry = false, replace = false, exhausted = false;
					vint nextAttempt = 0;
					AsyncSocketServerStartFailure failure = AsyncSocketServerStartFailure::Other;
					WString message;
					SPIN_LOCK(registry.lock)
					{
						if (entry->succeeded && !entry->terminal && CurrentEntry(registry, entry))
						{
							duplicate = Duplicate(entry, registration);
							if (!duplicate)
							{
								entry->registrations.Add(registration);
								registration->entry = entry;
								joined = true;
							}
						}
						else if (entry->ready && !entry->succeeded)
						{
							failure = entry->failure;
							message = entry->failureMessage;
							if (failure == AsyncSocketServerStartFailure::AddressInUse)
							{
								if (entry->bindAttempt >= 5)
								{
									exhausted = true;
									if (CurrentEntry(registry, entry)) registry.entries.Remove(entry->port);
								}
								else if (CurrentEntry(registry, entry))
								{
									replace = true;
									nextAttempt = entry->bindAttempt + 1;
								}
								else retry = true;
							}
						}
						else retry = true;
					}
					CHECK_ERROR(!duplicate, L"A SocketHttpServerApi with the same normalized prefix has already started.");
					if (joined) return;
					if (message != WString::Empty) lastFailure = message;
					if (failure != AsyncSocketServerStartFailure::AddressInUse && !retry)
					{
						throw AsyncSocketServerStartException(failure, message);
					}
					if (exhausted)
					{
						throw AsyncSocketServerStartException(AsyncSocketServerStartFailure::AddressInUse, lastFailure);
					}
					if (replace)
					{
						auto candidate = Ptr(new RegistryEntry(registration->prefix.port, nextAttempt));
						SPIN_LOCK(registry.lock)
						{
							if (CurrentEntry(registry, entry)
								&& entry->ready && !entry->succeeded
								&& entry->failure == AsyncSocketServerStartFailure::AddressInUse
								&& entry->bindAttempt + 1 == nextAttempt)
							{
								registry.entries.Remove(entry->port);
								entry = candidate;
								entry->registrations.Add(registration);
								registration->entry = entry;
								registry.entries.Add(entry->port, entry);
								creator = true;
							}
						}
					}
					if (!creator) continue;
				}

				Ptr<SharedServer> server;
				AsyncSocketServerStartFailure failure = AsyncSocketServerStartFailure::Other;
				WString message;
				bool started = false;
				try
				{
					auto listener = CreateListener(entry->port);
					CHECK_ERROR(listener, L"The socket HTTP listener factory returned null.");
					server = Ptr(new SharedServer(listener, entry, GetTimeoutControllerFactory()));
					server->InitializeSelf(server);
					server->Start();
					started = true;
				}
				catch (const AsyncSocketServerStartException& exception) { failure = exception.GetFailure(); message = exception.Message(); }
				catch (const Exception& exception) { message = exception.Message(); }
				catch (...) { message = L"The socket HTTP listener failed to start."; }

				bool published = false;
				SPIN_LOCK(registry.lock)
				{
					if (started && !entry->terminal && CurrentEntry(registry, entry))
					{
						entry->server = server;
						entry->ready = true;
						entry->succeeded = true;
						published = true;
					}
					else
					{
						if (CurrentEntry(registry, entry) && failure != AsyncSocketServerStartFailure::AddressInUse) registry.entries.Remove(entry->port);
						entry->registrations.Remove(registration.Obj());
						registration->entry = nullptr;
						entry->failure = failure;
						entry->failureMessage = message;
						entry->ready = true;
						entry->terminal = true;
					}
				}
				entry->readyEvent.Signal();
				if (published) return;
				if (server) server->StopAndRelease();
				if (started) { failure = AsyncSocketServerStartFailure::Other; message = L"The listener stopped while starting."; }
				if (failure != AsyncSocketServerStartFailure::AddressInUse) throw AsyncSocketServerStartException(failure, message);
				if (message != WString::Empty) lastFailure = message;
			}
		}

		Ptr<HttpRequestServer> UnregisterApi(Ptr<ApiRegistration> registration)
		{
			Ptr<HttpRequestServer> server;
			Ptr<RegistryEntry> entry;
			auto& registry = GetSocketHttpRegistry();
			SPIN_LOCK(registry.lock)
			{
				entry = registration->entry;
				if (entry)
				{
					entry->registrations.Remove(registration.Obj());
					registration->entry = nullptr;
					if (entry->registrations.Count() == 0)
					{
						if (CurrentEntry(registry, entry)) registry.entries.Remove(entry->port);
						entry->terminal = true;
						server = entry->server;
						entry->server = nullptr;
					}
				}
			}
			return server;
		}

		List<Ptr<ApiRegistration>> Registrations(Ptr<RegistryEntry> entry)
		{
			List<Ptr<ApiRegistration>> result;
			auto& registry = GetSocketHttpRegistry();
			SPIN_LOCK(registry.lock)
			{
				if (!entry->terminal) for (auto item : entry->registrations) result.Add(item);
			}
			return result;
		}

		void UnexpectedStop(Ptr<RegistryEntry> entry)
		{
			List<Ptr<ApiRegistration>> stopping;
			auto& registry = GetSocketHttpRegistry();
			SPIN_LOCK(registry.lock)
			{
				if (!entry->terminal)
				{
					entry->terminal = true;
					if (CurrentEntry(registry, entry)) registry.entries.Remove(entry->port);
					for (auto registration : entry->registrations) { registration->entry = nullptr; stopping.Add(registration); }
					entry->registrations.Clear();
					entry->server = nullptr;
				}
			}
			for (auto registration : stopping) registration->Stop(true);
		}

		bool Match(const WString& prefix, const WString& path)
		{
			return prefix.Length() == 0 || path == prefix || (path.Length() > prefix.Length() && path.Left(prefix.Length()) == prefix && path[prefix.Length()] == L'/');
		}

		bool SingleHeader(Ptr<HttpRequest> request, const WString& name, WString& value, bool& exists)
		{
			auto count = CountHttpFields(request->headers, name);
			if (count > 1) return false;
			exists = count == 1;
			if (!exists) return true;
			return DecodeAsciiHttpFieldValue(FindHttpField(request->headers, name)->value, value);
		}

		WString Trim(const WString& text)
		{
			vint begin = 0, end = text.Length();
			while (begin < end && (text[begin] == L' ' || text[begin] == L'\t')) begin++;
			while (begin < end && (text[end - 1] == L' ' || text[end - 1] == L'\t')) end--;
			return text.Sub(begin, end - begin);
		}

		bool Preflight(Ptr<HttpRequest> request, bool& present, bool& supportedMethod)
		{
			present = false;
			supportedMethod = false;
			WString method;
			bool exists = false;
			if (!SingleHeader(request, L"access-control-request-method", method, exists)) return false;
			if (!exists) return true;
			present = true;
			method = Trim(method);
			supportedMethod = method == L"GET" || method == L"HEAD" || method == L"POST" || method == L"OPTIONS";
			if (method.Length() == 0) return false;
			for (auto&& field : request->headers)
			{
				if (field.name != L"access-control-request-headers") continue;
				WString headers;
				if (!DecodeAsciiHttpFieldValue(field.value, headers)) return false;
				vint reading = 0;
				while (reading <= headers.Length())
				{
					vint comma = reading;
					while (comma < headers.Length() && headers[comma] != L',') comma++;
					auto item = Lower(Trim(headers.Sub(reading, comma - reading)));
					if (item.Length() == 0 || (item != L"accept" && item != L"content-type")) return false;
					if (comma == headers.Length()) break;
					reading = comma + 1;
				}
			}
			return true;
		}
	}

	void SetSocketHttpServerListenerFactoryForTesting(const Func<Ptr<IAsyncSocketServer>(vint)>& factory)
	{
		auto& registry = GetSocketHttpRegistry();
		SPIN_LOCK(registry.lock)
		{
			CHECK_ERROR(registry.entries.Count() == 0, L"The test listener factory can only change while no API is started.");
			registry.listenerFactory = factory;
		}
	}

	void ResetSocketHttpServerListenerFactoryForTesting() { SetSocketHttpServerListenerFactoryForTesting({}); }

	void SetSocketHttpServerTimeoutControllerFactoryForTesting(const Func<Ptr<IHttpRequestTimeoutController>()>& factory)
	{
		auto& registry = GetSocketHttpRegistry();
		SPIN_LOCK(registry.lock)
		{
			CHECK_ERROR(registry.entries.Count() == 0, L"The test timeout controller factory can only change while no API is started.");
			registry.timeoutControllerFactory = factory;
		}
	}

	void ResetSocketHttpServerTimeoutControllerFactoryForTesting() { SetSocketHttpServerTimeoutControllerFactoryForTesting({}); }

	WaitForClientResult SharedServer::OnClientConnected(IHttpRequestConnection* connection)
	{
		Ptr<RegistryEntry> retained;
		CS_LOCK(lock) { retained = entry; }
		if (!retained) return WaitForClientResult::Reject;
		try
		{
			auto dispatcher = Ptr(new SocketHttpServerApiDispatcher(retained));
			dispatcher->InitializeSelf(dispatcher);
			connection->InstallCallback(dispatcher.Obj());
			return WaitForClientResult::Accept;
		}
		catch (...) { return WaitForClientResult::Reject; }
	}

	void SharedServer::StopAndRelease()
	{
		Ptr<SharedServer> retained;
		CS_LOCK(lock) { retained = selfReference; }
		try { HttpRequestServer::Stop(); } catch (...) {}
		CS_LOCK(lock) { entry = nullptr; selfReference = nullptr; }
	}

	void SharedServer::OnServerStopped()
	{
		Ptr<SharedServer> retained;
		Ptr<RegistryEntry> retainedEntry;
		CS_LOCK(lock) { retained = selfReference; retainedEntry = entry; }
		if (retainedEntry) UnexpectedStop(retainedEntry);
		try { HttpRequestServer::OnServerStopped(); } catch (...) {}
		CS_LOCK(lock) { entry = nullptr; selfReference = nullptr; }
	}
}

namespace vl::inter_process::async_tcp_socket
{
	using namespace collections;

	Ptr<SocketHttpServerApiDispatcher> SocketHttpServerApiDispatcher::Retain()
	{
		Ptr<SocketHttpServerApiDispatcher> retained;
		CS_LOCK(lockSelf) { retained = selfReference; }
		return retained;
	}

	void SocketHttpServerApiDispatcher::SendAutomatic(vint code, bool close, bool preflight)
	{
		auto response = Normalize(Automatic(code, close, preflight), L"GET");
		IHttpRequestConnection* connection = nullptr;
		CS_LOCK(state->lock)
		{
			if (state->terminal || state->automaticWrite || state->context) return;
			state->automaticWrite = true;
			state->closeAfterWrite = close;
			connection = state->connection;
		}
		if (!connection) return;
		try
		{
			connection->SendResponse(response);
		}
		catch (...)
		{
			CS_LOCK(state->lock)
			{
				state->automaticWrite = false;
				state->terminal = true;
			}
			try { connection->Stop(); } catch (...) {}
		}
	}

	void SocketHttpServerApiDispatcher::Process(Ptr<HttpRequest> request)
	{
		if (!request || request->version.major != 1 || request->version.minor != 1)
		{
			SendAutomatic(505, true);
			return;
		}

		WString host;
		bool hostExists = false;
		if (!SingleHeader(request, L"host", host, hostExists) || !hostExists)
		{
			SendAutomatic(400, true);
			return;
		}
		WString authority;
		vint port = 0;
		if (!ParseAuthority(host, authority, port))
		{
			SendAutomatic(400, true);
			return;
		}

		auto supportedMethod = request->method == L"GET" || request->method == L"HEAD" || request->method == L"POST" || request->method == L"OPTIONS";
		if (!supportedMethod)
		{
			SendAutomatic(501, false);
			return;
		}

		auto registrations = Registrations(state->entry);
		if (request->requestTarget == L"*")
		{
			if (request->method != L"OPTIONS")
			{
				SendAutomatic(400, true);
				return;
			}
			bool automaticOptions = false;
			for (auto registration : registrations)
			{
				if (registration->prefix.authority == authority && registration->respondToOptions)
				{
					automaticOptions = true;
					break;
				}
			}
			if (!automaticOptions)
			{
				SendAutomatic(501, false);
				return;
			}
			bool present = false, methodSupported = false;
			if (!Preflight(request, present, methodSupported))
			{
				SendAutomatic(400, false, true);
				return;
			}
			SendAutomatic(present && !methodSupported ? 405 : 200, false, true);
			return;
		}

		auto target = request->requestTarget;
		if (target.Length() == 0 || target[0] != L'/' || target.IndexOf(L'#') != -1)
		{
			SendAutomatic(400, true);
			return;
		}
		vint question = target.IndexOf(L'?');
		auto rawPath = question == -1 ? target : target.Left(question);
		auto query = question == -1 ? WString::Empty : target.Right(target.Length() - question - 1);
		WString path;
		if (!DecodePath(rawPath, path))
		{
			SendAutomatic(400, true);
			return;
		}

		Ptr<ApiRegistration> selected;
		for (auto registration : registrations)
		{
			if (registration->prefix.authority == authority && Match(registration->prefix.decodedPath, path))
			{
				if (!selected || registration->prefix.decodedPath.Length() > selected->prefix.decodedPath.Length()) selected = registration;
			}
		}
		if (!selected)
		{
			SendAutomatic(404, false);
			return;
		}

		if (request->method == L"OPTIONS" && selected->respondToOptions)
		{
			bool present = false, methodSupported = false;
			if (!Preflight(request, present, methodSupported))
			{
				SendAutomatic(400, false, true);
				return;
			}
			if (present)
			{
				SendAutomatic(methodSupported ? 200 : 405, false, true);
				return;
			}
		}

		WString relative;
		if (selected->prefix.decodedPath.Length() == 0) relative = path;
		else if (path == selected->prefix.decodedPath) relative = L"/";
		else relative = path.Right(path.Length() - selected->prefix.decodedPath.Length());
		if (relative.Length() == 0) relative = L"/";

		auto contextImpl = Ptr(new SocketHttpRequestContext::Impl(state, selected, request, relative, query));
		auto context = Ptr(new SocketHttpRequestContext(contextImpl));
		contextImpl->owner = context.Obj();
		auto cancelForStop = Func<void()>([contextImpl]()
		{
			contextImpl->Finish(false, true, true, true);
		});
		bool installed = false;
		CS_LOCK(state->lock)
		{
			if (!state->terminal && !state->context && !state->automaticWrite)
			{
				state->context = context;
				installed = true;
			}
		}
		if (!installed || !selected->AddContext(context, cancelForStop))
		{
			CS_LOCK(state->lock) { if (state->context == context) state->context = nullptr; }
			SendAutomatic(404, false);
			return;
		}

		try
		{
			if (!selected->InvokeRequest(context)) context->Cancel();
		}
		catch (...)
		{
			context->Respond(Automatic(500, false));
		}
	}

	void SocketHttpServerApiDispatcher::OnReadRequest(Ptr<HttpRequest> request)
	{
		auto retained = Retain();
		if (!retained) return;
		try { Process(request); } catch (...) { SendAutomatic(500, true); }
	}

	void SocketHttpServerApiDispatcher::OnReadRequestFailure(HttpRequestFailure failure)
	{
		auto retained = Retain();
		if (!retained) return;
		SendAutomatic((vint)failure, true);
	}

	void SocketHttpServerApiDispatcher::OnWriteCompleted()
	{
		auto retained = Retain();
		if (!retained) return;
		Ptr<SocketHttpRequestContext> context;
		bool automatic = false, close = false;
		CS_LOCK(state->lock)
		{
			context = state->context;
			if (!context && state->automaticWrite)
			{
				automatic = true;
				close = state->closeAfterWrite;
				state->automaticWrite = false;
				state->closeAfterWrite = false;
			}
		}
		if (context) context->impl->Finish(true, false, true, false);
		if (!context && !automatic) return;

		if (close)
		{
			IHttpRequestConnection* stopping = nullptr;
			CS_LOCK(state->lock)
			{
				state->terminal = true;
				stopping = state->connection;
			}
			if (stopping) try { stopping->Stop(); } catch (...) {}
		}
	}

	void SocketHttpServerApiDispatcher::OnError(const WString&, bool fatal)
	{
		auto retained = Retain();
		if (!retained) return;
		Ptr<SocketHttpRequestContext> context;
		IHttpRequestConnection* connection = nullptr;
		CS_LOCK(state->lock)
		{
			context = state->context;
			if (fatal || context || state->automaticWrite)
			{
				state->terminal = true;
				connection = state->connection;
			}
		}
		if (context) context->impl->Finish(false, true, true, true);
		else if (connection) try { connection->Stop(); } catch (...) {}
	}

	void SocketHttpServerApiDispatcher::OnDisconnected()
	{
		auto retained = Retain();
		if (!retained) return;
		Ptr<SocketHttpRequestContext> context;
		CS_LOCK(state->lock)
		{
			state->terminal = true;
			state->connection = nullptr;
			state->automaticWrite = false;
			context = state->context;
		}
		if (context) context->impl->Finish(false, true, true, false);
		CS_LOCK(lockSelf) { selfReference = nullptr; }
	}

	void SocketHttpServerApiDispatcher::OnInstalled(IHttpRequestConnection* connection)
	{
		CS_LOCK(state->lock) { state->connection = connection; }
		connection->BeginReadingLoopUnsafe();
	}

	SocketHttpRequestContext::SocketHttpRequestContext(Ptr<Impl> _impl) : impl(_impl) {}
	SocketHttpRequestContext::~SocketHttpRequestContext() {}
	Ptr<HttpRequest> SocketHttpRequestContext::GetRequest() { return impl->request; }
	WString SocketHttpRequestContext::GetRelativePath() { return impl->relativePath; }
	WString SocketHttpRequestContext::GetQuery() { return impl->query; }
	bool SocketHttpRequestContext::TryGetBodyUtf8(WString& body)
	{
		Array<vuint8_t> bytes;
		if (!FlattenHttpBody(impl->request->body, bytes)) return false;
		return ::vl::inter_process::async_tcp_socket::DecodeStrictUtf8(
			bytes.Count() == 0 ? nullptr : &bytes[0],
			bytes.Count(),
			body
			);
	}

	bool SocketHttpRequestContext::Respond(Ptr<HttpResponse> response, Func<void(bool)> completion)
	{
		Ptr<HttpResponse> normalized;
		IHttpRequestConnection* connection = nullptr;
		CS_LOCK(impl->lock)
		{
			if (impl->state != Impl::State::Pending) return false;
			normalized = Normalize(response, impl->request->method);
			impl->state = Impl::State::Sending;
			impl->completion = completion;
		}
		CS_LOCK(impl->connectionState->lock)
		{
			if (!impl->connectionState->terminal && impl->connectionState->context.Obj() == this) connection = impl->connectionState->connection;
		}
		if (!connection)
		{
			impl->Finish(false, false, true, false);
			return true;
		}
		try { connection->SendResponse(normalized); }
		catch (...) { impl->Finish(false, false, true, true); }
		return true;
	}

	bool SocketHttpRequestContext::RespondStatus(vint statusCode, const WString& reason, Func<void(bool)> completion)
	{
		ValidateConvenienceResponseArguments(statusCode, reason, WString::Empty, 0);
		Array<vuint8_t> body;
		return Respond(CreateConvenienceResponse(statusCode, reason, WString::Empty, std::move(body)), completion);
	}

	bool SocketHttpRequestContext::RespondBytes(vint statusCode, const WString& reason, const WString& contentType, const Array<vuint8_t>& body, Func<void(bool)> completion)
	{
		ValidateConvenienceResponseArguments(statusCode, reason, contentType, body.Count());
		Array<vuint8_t> copy(body.Count());
		if (body.Count() > 0) memcpy(&copy[0], &body[0], body.Count());
		return Respond(CreateConvenienceResponse(statusCode, reason, contentType, std::move(copy)), completion);
	}

	bool SocketHttpRequestContext::RespondUtf8(vint statusCode, const WString& reason, const WString& contentType, const WString& body, Func<void(bool)> completion)
	{
		ValidateConvenienceResponseArguments(statusCode, reason, contentType, 0);
		Array<vuint8_t> encoded;
		CHECK_ERROR(EncodeStrictUtf8(body, encoded), L"The response body contains invalid Unicode.");
		CHECK_ERROR(encoded.Count() <= HttpBodySizeLimit, L"The response body is too large.");
		return Respond(CreateConvenienceResponse(statusCode, reason, contentType, std::move(encoded)), completion);
	}

	bool SocketHttpRequestContext::Cancel()
	{
		return impl->Finish(false, true, false, true);
	}
}

namespace vl::inter_process::async_tcp_socket
{
	class SocketHttpServerApi::Impl : public Object
	{
	private:
		SocketHttpServerApi*				owner;
		Ptr<ApiRegistration>				registration;
		CriticalSection					lock;
		ConditionVariable				cv;
		bool startCalled = false;
		bool startInProgress = false;
		bool startSucceeded = false;
		bool stopRequested = false;
		bool stopRequestedNotify = false;
		bool stopStarted = false;
		bool stopFinished = false;

		void Request(Ptr<SocketHttpRequestContext> context)
		{
			SocketHttpServerApi* target = nullptr;
			CS_LOCK(lock) { target = owner; }
			if (target) target->OnHttpRequestReceived(context);
			else context->Cancel();
		}

		void Stopping()
		{
			SocketHttpServerApi* target = nullptr;
			CS_LOCK(lock) { target = owner; }
			if (target) target->OnHttpServerStopping();
		}

		void StopInternal(bool notify)
		{
			bool first = false;
			bool notifyRegistration = false;
			auto callbackDepth = ApiRegistration::Depth(registration.Obj());
			lock.Enter();
			if (startInProgress && callbackDepth > 0)
			{
				stopRequested = true;
				stopRequestedNotify |= notify;
				lock.Leave();
				return;
			}
			while (startInProgress) cv.SleepWith(lock);
			if (!stopStarted)
			{
				stopStarted = true;
				first = true;
				notifyRegistration = notify && startSucceeded;
			}
			else if (callbackDepth > 0)
			{
				lock.Leave();
				return;
			}
			else
			{
				while (!stopFinished) cv.SleepWith(lock);
			}
			lock.Leave();
			if (!first) return;

			auto server = UnregisterApi(registration);
			registration->Stop(notifyRegistration);
			if (server)
			{
				auto shared = server.Cast<SharedServer>();
				if (shared) shared->StopAndRelease();
				else server->Stop();
			}
			CS_LOCK(lock)
			{
				stopFinished = true;
				cv.WakeAllPendings();
			}
		}

	public:
		Impl(SocketHttpServerApi* _owner, const WString& urlPrefix, bool respondToOptions)
			: owner(_owner)
			, registration(Ptr(new ApiRegistration(ParsePrefix(urlPrefix), respondToOptions)))
		{
			registration->requestCallback = Func<void(Ptr<SocketHttpRequestContext>)>([this](Ptr<SocketHttpRequestContext> context) { Request(context); });
			registration->stoppingCallback = Func<void()>([this]() { Stopping(); });
		}

		void Start()
		{
#define ERROR_PREFIX L"vl::inter_process::async_tcp_socket::SocketHttpServerApi::Start()#"
			CS_LOCK(lock)
			{
				CHECK_ERROR(!startCalled && !stopStarted, ERROR_PREFIX L"Can only be called once before stopping.");
				startCalled = true;
				startInProgress = true;
			}
			try
			{
				RegisterApi(registration);
			}
			catch (...)
			{
				bool stopAfterStart = false, notifyAfterStart = false;
				CS_LOCK(lock)
				{
					startInProgress = false;
					stopAfterStart = stopRequested;
					notifyAfterStart = stopRequestedNotify;
					stopRequested = false;
					stopRequestedNotify = false;
					cv.WakeAllPendings();
				}
				if (stopAfterStart)
				{
					try { StopInternal(notifyAfterStart); } catch (...) {}
				}
				throw;
			}
			bool stopAfterStart = false, notifyAfterStart = false;
			CS_LOCK(lock)
			{
				startSucceeded = true;
				startInProgress = false;
				stopAfterStart = stopRequested;
				notifyAfterStart = stopRequestedNotify;
				stopRequested = false;
				stopRequestedNotify = false;
				cv.WakeAllPendings();
			}
			if (stopAfterStart) StopInternal(notifyAfterStart);
#undef ERROR_PREFIX
		}

		void Stop() { StopInternal(true); }

		void Destroy()
		{
			CS_LOCK(lock) { owner = nullptr; }
			StopInternal(false);
		}

		bool IsStopped()
		{
			CS_LOCK(lock)
			{
				if (stopStarted) return true;
			}
			return registration->IsStopped();
		}

		WString GetUrlPrefix() { return registration->prefix.normalizedUrl; }
	};

	SocketHttpServerApi::SocketHttpServerApi(const WString& urlPrefix, bool respondToOptions)
		: impl(Ptr(new Impl(this, urlPrefix, respondToOptions)))
	{
	}

	SocketHttpServerApi::~SocketHttpServerApi() { impl->Destroy(); }
	void SocketHttpServerApi::OnHttpServerStopping() {}
	void SocketHttpServerApi::Start() { impl->Start(); }
	void SocketHttpServerApi::Stop() { impl->Stop(); }
	bool SocketHttpServerApi::IsStopped() { return impl->IsStopped(); }
	WString SocketHttpServerApi::GetUrlPrefix() { return impl->GetUrlPrefix(); }
}
