#include "AsyncSocket_HttpServer.h"
#include "../NetworkProtocolHttp.h"

#include <random>

namespace vl::inter_process::async_tcp_socket
{
	using namespace collections;

	namespace
	{
		constexpr const wchar_t*				JsonContentType = L"application/json; charset=utf8";
		constexpr vint						GeneratedTokenLength = 36;

		wchar_t FoldAscii(wchar_t c)
		{
			return L'A' <= c && c <= L'Z' ? c - L'A' + L'a' : c;
		}

		bool AsciiEqualsIgnoreCase(const WString& a, const WString& b)
		{
			if (a.Length() != b.Length()) return false;
			for (vint i = 0; i < a.Length(); i++)
			{
				if (FoldAscii(a[i]) != FoldAscii(b[i])) return false;
			}
			return true;
		}

		vint HexValue(wchar_t c)
		{
			if (L'0' <= c && c <= L'9') return c - L'0';
			if (L'a' <= c && c <= L'f') return c - L'a' + 10;
			if (L'A' <= c && c <= L'F') return c - L'A' + 10;
			return -1;
		}

		bool IsLegalOriginPathCharacter(wchar_t c)
		{
			if (L'a' <= c && c <= L'z') return true;
			if (L'A' <= c && c <= L'Z') return true;
			if (L'0' <= c && c <= L'9') return true;
			switch (c)
			{
			case L'-': case L'.': case L'_': case L'~':
			case L'!': case L'$': case L'&': case L'\'':
			case L'(': case L')': case L'*': case L'+':
			case L',': case L';': case L'=': case L':':
			case L'@': case L'/':
				return true;
			default:
				return false;
			}
		}

		bool IsValidWString(const WString& text)
		{
			for (vint i = 0; i < text.Length(); i++)
			{
				auto code = (vuint32_t)text[i];
				if (code == 0) return false;
				if constexpr (sizeof(wchar_t) == 2)
				{
					if (0xD800 <= code && code <= 0xDBFF)
					{
						if (++i == text.Length()) return false;
						auto low = (vuint32_t)text[i];
						if (low < 0xDC00 || 0xDFFF < low) return false;
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
			}
			return true;
		}

		bool DecodeStrictUtf8(const vuint8_t* bytes, vint count, WString& text)
		{
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

				if (i + following > count) return false;
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
					code == 0 ||
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

			text = characters.Count() == 0
				? WString::Empty
				: WString::CopyFrom(&characters[0], characters.Count());
			return true;
		}

		bool ValidateBasePath(const WString& baseUrl)
		{
			if (baseUrl == WString::Empty) return true;
			if (baseUrl[0] != L'/' || baseUrl[baseUrl.Length() - 1] == L'/') return false;

			List<vuint8_t> bytes;
			for (vint i = 0; i < baseUrl.Length(); i++)
			{
				auto c = baseUrl[i];
				if (c == L'%')
				{
					if (i + 2 >= baseUrl.Length()) return false;
					auto high = HexValue(baseUrl[i + 1]);
					auto low = HexValue(baseUrl[i + 2]);
					if (high < 0 || low < 0) return false;
					auto byte = (vuint8_t)(high * 16 + low);
					if (byte == 0 || byte == '/' || byte == '\\') return false;
					bytes.Add(byte);
					i += 2;
				}
				else
				{
					if (!IsLegalOriginPathCharacter(c)) return false;
					bytes.Add((vuint8_t)c);
				}
			}

			WString decoded;
			return DecodeStrictUtf8(&bytes[0], bytes.Count(), decoded);
		}

		bool ValidRequestTargetSize(const WString& method, const WString& target)
		{
			return method.Length() + target.Length() + 10 <= HttpRequestLineSizeLimit;
		}

		WString CreateServerUrlPrefix(const WString& baseUrl, vint port)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::SocketHttpServer::SocketHttpServer(const WString&, vint)#"
			CHECK_ERROR(ValidateBasePath(baseUrl), ERROR_MESSAGE_PREFIX L"baseUrl must be empty or a legal ASCII origin-form path prefix without a trailing slash.");
			CHECK_ERROR(1 <= port && port <= 65535, ERROR_MESSAGE_PREFIX L"port must be in 1..65535.");
			CHECK_ERROR(ValidRequestTargetSize(L"GET", baseUrl + HttpServerUrl_Connect), ERROR_MESSAGE_PREFIX L"The /Connect target exceeds the HTTP request-line limit.");
			const WString tokenPlaceholder = L"000000000000000000000000000000000000";
			CHECK_ERROR(ValidRequestTargetSize(L"POST", baseUrl + HttpServerUrl_Request + L"/" + tokenPlaceholder), ERROR_MESSAGE_PREFIX L"The /Request target plus a generated token exceeds the HTTP request-line limit.");
			CHECK_ERROR(ValidRequestTargetSize(L"POST", baseUrl + HttpServerUrl_Response + L"/" + tokenPlaceholder), ERROR_MESSAGE_PREFIX L"The /Response target plus a generated token exceeds the HTTP request-line limit.");
			return L"http://localhost:" + itow(port) + baseUrl;
#undef ERROR_MESSAGE_PREFIX
		}

		void EncodeMessage(const WString& message, Array<vuint8_t>& bytes)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::SocketHttpServerConnection::SendString(const WString&)#"
			CHECK_ERROR(message.Length() > 0, ERROR_MESSAGE_PREFIX L"A logical HTTP message cannot be empty.");
			CHECK_ERROR(IsValidWString(message), ERROR_MESSAGE_PREFIX L"A logical HTTP message must contain valid Unicode without NUL.");
			auto utf8 = wtou8(message);
			CHECK_ERROR(utf8.Length() <= HttpBodySizeLimit, ERROR_MESSAGE_PREFIX L"The UTF-8 message exceeds HttpBodySizeLimit.");
			bytes.Resize(utf8.Length());
			if (bytes.Count() > 0)
			{
				memcpy(&bytes[0], utf8.Buffer(), bytes.Count());
			}
#undef ERROR_MESSAGE_PREFIX
		}

		HttpField CreateAsciiField(const WString& name, const WString& value)
		{
			HttpField field;
			field.name = name;
			field.value.Resize(value.Length());
			for (vint i = 0; i < value.Length(); i++)
			{
				CHECK_ERROR(value[i] <= 0x7F, L"An HTTP field helper requires ASCII.");
				field.value[i] = (vuint8_t)value[i];
			}
			return field;
		}

		Ptr<HttpResponse> CreateSuccessResponse(const WString& message)
		{
			auto response = Ptr(new HttpResponse);
			response->statusCode = 200;
			response->reason = L"OK";
			response->headers.Add(CreateAsciiField(L"content-type", JsonContentType));
			if (message != WString::Empty)
			{
				HttpBodyChunk chunk;
				EncodeMessage(message, chunk.data);
				response->body.chunks.Add(std::move(chunk));
			}
			return response;
		}

		Ptr<HttpResponse> CreateErrorResponse(const WString& reason)
		{
			auto response = Ptr(new HttpResponse);
			response->statusCode = 404;
			response->reason = reason;
			return response;
		}

		bool FieldValueEquals(const Array<vuint8_t>& value, const wchar_t* expected)
		{
			vint length = 0;
			while (expected[length]) length++;
			if (value.Count() != length) return false;
			for (vint i = 0; i < length; i++)
			{
				if (value[i] != (vuint8_t)expected[i]) return false;
			}
			return true;
		}

		bool ParseContentLength(const Array<vuint8_t>& value, vint& length)
		{
			vint begin = 0;
			vint end = value.Count();
			while (begin < end && (value[begin] == ' ' || value[begin] == '\t')) begin++;
			while (begin < end && (value[end - 1] == ' ' || value[end - 1] == '\t')) end--;
			if (begin == end) return false;
			vuint64_t number = 0;
			for (vint i = begin; i < end; i++)
			{
				if (value[i] < '0' || value[i] > '9') return false;
				auto digit = (vuint64_t)(value[i] - '0');
				if (number > ((vuint64_t)HttpBodySizeLimit - digit) / 10) return false;
				number = number * 10 + digit;
			}
			length = (vint)number;
			return true;
		}

		bool HasEmptyBody(Ptr<HttpRequest> request)
		{
			if (!request || request->body.chunks.Count() != 0 || request->body.trailers.Count() != 0) return false;
			vint contentLengths = 0;
			for (auto&& field : request->headers)
			{
				if (AsciiEqualsIgnoreCase(field.name, L"transfer-encoding")) return false;
				if (AsciiEqualsIgnoreCase(field.name, L"content-length"))
				{
					vint length = -1;
					if (!ParseContentLength(field.value, length) || length != 0) return false;
					contentLengths++;
				}
			}
			return contentLengths <= 1;
		}

		bool DecodeSubmittedMessage(Ptr<HttpRequest> request, WString& message)
		{
			if (!request || request->body.trailers.Count() != 0) return false;
			vint contentLength = -1;
			vint contentLengths = 0;
			vint contentTypes = 0;
			for (auto&& field : request->headers)
			{
				if (AsciiEqualsIgnoreCase(field.name, L"transfer-encoding")) return false;
				if (AsciiEqualsIgnoreCase(field.name, L"content-length"))
				{
					if (!ParseContentLength(field.value, contentLength)) return false;
					contentLengths++;
				}
				else if (AsciiEqualsIgnoreCase(field.name, L"content-type"))
				{
					if (!FieldValueEquals(field.value, JsonContentType)) return false;
					contentTypes++;
				}
			}
			if (contentLengths != 1 || contentTypes != 1 || contentLength <= 0) return false;

			Array<vuint8_t> bytes(contentLength);
			vint offset = 0;
			for (auto&& chunk : request->body.chunks)
			{
				if (chunk.data.Count() > contentLength - offset) return false;
				if (chunk.data.Count() > 0)
				{
					memcpy(&bytes[offset], &chunk.data[0], chunk.data.Count());
					offset += chunk.data.Count();
				}
			}
			if (offset != contentLength) return false;
			return DecodeStrictUtf8(&bytes[0], bytes.Count(), message) && message.Length() > 0;
		}

		bool ExtractToken(const WString& path, const wchar_t* route, WString& token)
		{
			auto prefix = WString::Unmanaged(route) + L"/";
			if (path.Length() <= prefix.Length() || path.Left(prefix.Length()) != prefix) return false;
			token = path.Right(path.Length() - prefix.Length());
			return token.Length() > 0;
		}

		WString GenerateToken()
		{
			vuint8_t bytes[16];
			std::random_device random;
			for (vint i = 0; i < 16; i++) bytes[i] = (vuint8_t)random();
			bytes[6] = (bytes[6] & 0x0F) | 0x40;
			bytes[8] = (bytes[8] & 0x3F) | 0x80;

			const wchar_t* hex = L"0123456789abcdef";
			wchar_t text[GeneratedTokenLength];
			vint writing = 0;
			for (vint i = 0; i < 16; i++)
			{
				if (i == 4 || i == 6 || i == 8 || i == 10) text[writing++] = L'-';
				text[writing++] = hex[bytes[i] >> 4];
				text[writing++] = hex[bytes[i] & 0x0F];
			}
			return WString::CopyFrom(text, GeneratedTokenLength);
		}

		BEGIN_GLOBAL_STORAGE_CLASS(SocketHttpServerTestHooks)
			SpinLock							lock;
			Func<void(const WString&)>			claimed;
			Func<void(const WString&, bool)>		completed;
			Func<void(const WString&)>			registered;
		INITIALIZE_GLOBAL_STORAGE_CLASS
		FINALIZE_GLOBAL_STORAGE_CLASS
			SPIN_LOCK(lock)
			{
				claimed = {};
				completed = {};
				registered = {};
			}
		END_GLOBAL_STORAGE_CLASS(SocketHttpServerTestHooks)

		void InvokePollClaimed(const WString& token)
		{
			Func<void(const WString&)> callback;
			auto& hooks = GetSocketHttpServerTestHooks();
			SPIN_LOCK(hooks.lock) { callback = hooks.claimed; }
			if (callback) try { callback(token); } catch (...) {}
		}

		void InvokePollCompleted(const WString& token, bool succeeded)
		{
			Func<void(const WString&, bool)> callback;
			auto& hooks = GetSocketHttpServerTestHooks();
			SPIN_LOCK(hooks.lock) { callback = hooks.completed; }
			if (callback) try { callback(token, succeeded); } catch (...) {}
		}

		void InvokePollRegistered(const WString& token)
		{
			Func<void(const WString&)> callback;
			auto& hooks = GetSocketHttpServerTestHooks();
			SPIN_LOCK(hooks.lock) { callback = hooks.registered; }
			if (callback) try { callback(token); } catch (...) {}
		}

		class SocketHttpServerConnection;
		class SocketHttpServerLifecycle;

		class SocketHttpServerConnectionLifecycle : public Object
		{
		public:
			CriticalSection						lockState;
			ConditionVariable					cvState;
			SocketHttpServerConnection*			owner = nullptr;
			Ptr<SocketHttpServerLifecycle>		server;
			WString								token;
			INetworkProtocolCallback*			callback = nullptr;
			List<WString>						queuedInbound;
			List<WString>						queuedOutbound;
			List<Ptr<SocketHttpRequestContext>>
										queuedPollRegistrations;
			Ptr<SocketHttpRequestContext>		pendingPoll;
			Ptr<SocketHttpRequestContext>		inFlightPoll;
			WString								inFlightMessage;
			vint								activeCallbacks = 0;
			bool								callbackInstalling = false;
			bool								pollRegistrationProcessing = false;
			bool								accepted = false;
			bool								stopStarted = false;
			bool								stopCancellationFinished = false;
			bool								stopFinished = false;
			bool								stopAssistProcessing = false;
			bool								disconnectDelivering = false;
			bool								disconnectFinished = false;
		};

		class SocketHttpServerConnection
			: public Object
			, public virtual INetworkProtocolConnection
		{
			struct CallbackFrame
			{
				Ptr<SocketHttpServerConnectionLifecycle>	state;
				CallbackFrame*					previous = nullptr;

				CallbackFrame(Ptr<SocketHttpServerConnectionLifecycle> _state);
				~CallbackFrame();
			};

			struct InboundFrame
			{
				Ptr<SocketHttpServerConnectionLifecycle>	state;
				InboundFrame*					previous = nullptr;
				List<WString>					generated;

				InboundFrame(Ptr<SocketHttpServerConnectionLifecycle> _state);
				~InboundFrame();
			};

			struct PollWork
			{
				Ptr<SocketHttpRequestContext>		context;
				WString							message;

				operator bool() const { return context != nullptr; }
			};

			static thread_local CallbackFrame*	currentCallbackFrame;
			static thread_local InboundFrame*	currentInboundFrame;
			Ptr<SocketHttpServerConnectionLifecycle>
										lifecycle;

			static vint CurrentCallbackDepth(Ptr<SocketHttpServerConnectionLifecycle> state);
			static bool ClaimPollUnsafe(Ptr<SocketHttpServerConnectionLifecycle> state, PollWork& work);
			static void StartPollResponse(Ptr<SocketHttpServerConnectionLifecycle> state, PollWork work);
			static void FinishPollResponse(Ptr<SocketHttpServerConnectionLifecycle> state, Ptr<SocketHttpRequestContext> context, bool succeeded);
			static void ProcessPollRegistrations(Ptr<SocketHttpServerConnectionLifecycle> state);
			void StopCore(bool removeFromServer, bool waitForPoll);

		public:
			SocketHttpServerConnection(Ptr<SocketHttpServerLifecycle> server, const WString& token);

			void DetachServer(SocketHttpServerLifecycle* server);
			bool MarkAccepted();
			bool IsAccepted();
			bool HasCurrentCallback();
			WString GetToken();
			WaitForClientResult InvokeClientConnected(SocketHttpServer* server);
			bool RegisterPoll(Ptr<SocketHttpRequestContext> context);
			bool DispatchInbound(const WString& message, WString& response);
			void StopFromServer();
			void WaitForPollCompletion();

			void InstallCallback(INetworkProtocolCallback* callback) override;
			void BeginReadingLoopUnsafe() override;
			void SendString(const WString& str) override;
			void Stop() override;
		};

		class SocketHttpServerLifecycle : public Object
		{
		public:
			SocketHttpServer*					owner = nullptr;
			CriticalSection						lockState;
			ConditionVariable					cvState;
			Dictionary<WString, Ptr<SocketHttpServerConnection>>
										connections;
			List<Ptr<SocketHttpServerConnection>>
										stoppingConnections;
			bool								startCalled = false;
			bool								started = false;
			bool								stopStarted = false;
			bool								stopProcessing = false;
			bool								stopProcessed = false;
			bool								stopAssistProcessing = false;

			SocketHttpServerLifecycle(SocketHttpServer* _owner)
				: owner(_owner)
			{
			}

			void PrepareStart()
			{
				CS_LOCK(lockState)
				{
					CHECK_ERROR(!startCalled && !stopStarted, L"SocketHttpServer::Start can only be called once before stopping.");
					startCalled = true;
					started = true;
				}
			}

			Ptr<SocketHttpServerConnection> CreateConnection(Ptr<SocketHttpServerLifecycle> retainedSelf)
			{
				while (true)
				{
					auto token = GenerateToken();
					auto connection = Ptr(new SocketHttpServerConnection(retainedSelf, token));
					CS_LOCK(lockState)
					{
						if (!started || stopStarted) return nullptr;
						if (!connections.Keys().Contains(token))
						{
							connections.Add(token, connection);
							return connection;
						}
					}
				}
			}

			Ptr<SocketHttpServerConnection> FindConnection(const WString& token)
			{
				CS_LOCK(lockState)
				{
					if (stopStarted) return nullptr;
					auto index = connections.Keys().IndexOf(token);
					return index == -1 ? nullptr : connections.Values()[index];
				}
				return nullptr;
			}

			bool TryAccept(const WString& token, SocketHttpServerConnection* connection)
			{
				CS_LOCK(lockState)
				{
					if (stopStarted) return false;
					auto index = connections.Keys().IndexOf(token);
					if (index == -1 || connections.Values()[index].Obj() != connection) return false;
					return connection->MarkAccepted();
				}
				return false;
			}

			bool IsStopped()
			{
				CS_LOCK(lockState) { return stopStarted; }
				return true;
			}

			void RemoveConnection(const WString& token, SocketHttpServerConnection* connection)
			{
				bool removed = false;
				bool retained = false;
				CS_LOCK(lockState)
				{
					auto index = connections.Keys().IndexOf(token);
					if (index != -1 && connections.Values()[index].Obj() == connection)
					{
						if (connection->IsAccepted())
						{
							stoppingConnections.Add(connections.Values()[index]);
							retained = true;
						}
						connections.Remove(token);
						removed = true;
					}
				}
				if (removed && !retained) connection->DetachServer(this);
			}

			void PrepareStop(List<Ptr<SocketHttpServerConnection>>& stopping)
			{
				CS_LOCK(lockState)
				{
					if (!stopStarted)
					{
						stopStarted = true;
						started = false;
						for (auto connection : connections.Values()) stoppingConnections.Add(connection);
						connections.Clear();
					}
					for (auto connection : stoppingConnections) stopping.Add(connection);
				}
			}

			void PrepareStopProcessing(bool callbackNested, bool inheritsAssist, bool& execute, bool& assist)
			{
				CS_LOCK(lockState)
				{
					if (!stopProcessing && !stopProcessed)
					{
						stopProcessing = true;
						execute = true;
						if (callbackNested)
						{
							stopAssistProcessing = true;
							assist = true;
						}
					}
					else if (callbackNested && !inheritsAssist && !stopProcessed && !stopAssistProcessing)
					{
						stopAssistProcessing = true;
						assist = true;
					}
				}
			}

			void FinishStop(bool ownsAssist)
			{
				lockState.Enter();
				if (ownsAssist)
				{
					stopAssistProcessing = false;
				}
				else
				{
					while (stopAssistProcessing) cvState.SleepWith(lockState);
				}
				stopProcessing = false;
				stopProcessed = true;
				cvState.WakeAllPendings();
				lockState.Leave();
			}

			void FinishStopAssist()
			{
				CS_LOCK(lockState)
				{
					stopAssistProcessing = false;
					cvState.WakeAllPendings();
				}
			}

			void WaitForStop()
			{
				CS_LOCK(lockState)
				{
					while (!stopProcessed) cvState.SleepWith(lockState);
				}
			}

			Ptr<SocketHttpServerConnection> ReleaseStoppedConnection(SocketHttpServerConnection* connection)
			{
				Ptr<SocketHttpServerConnection> releasing;
				CS_LOCK(lockState)
				{
					for (vint i = 0; i < stoppingConnections.Count(); i++)
					{
						if (stoppingConnections[i].Obj() == connection)
						{
							releasing = stoppingConnections[i];
							stoppingConnections.RemoveAt(i);
							break;
						}
					}
				}
				if (releasing) connection->DetachServer(this);
				return releasing;
			}
		};

		struct SocketHttpServerStopFrame
		{
			SocketHttpServerLifecycle*		lifecycle = nullptr;
			SocketHttpServerStopFrame*		previous = nullptr;
			bool							ownsAssist = false;
		};

		thread_local SocketHttpServerStopFrame* currentSocketHttpServerStopFrame = nullptr;

		SocketHttpServerStopFrame* FindSocketHttpServerStopFrame(SocketHttpServerLifecycle* lifecycle)
		{
			for (auto frame = currentSocketHttpServerStopFrame; frame; frame = frame->previous)
			{
				if (frame->lifecycle == lifecycle) return frame;
			}
			return nullptr;
		}

		struct SocketHttpServerStopScope
		{
			SocketHttpServerStopFrame		frame;

			SocketHttpServerStopScope(SocketHttpServerLifecycle* lifecycle, bool ownsAssist)
			{
				frame.lifecycle = lifecycle;
				frame.previous = currentSocketHttpServerStopFrame;
				frame.ownsAssist = ownsAssist;
				currentSocketHttpServerStopFrame = &frame;
			}

			~SocketHttpServerStopScope()
			{
				currentSocketHttpServerStopFrame = frame.previous;
			}
		};

		thread_local SocketHttpServerConnection::CallbackFrame* SocketHttpServerConnection::currentCallbackFrame = nullptr;
		thread_local SocketHttpServerConnection::InboundFrame* SocketHttpServerConnection::currentInboundFrame = nullptr;

		SocketHttpServerConnection::CallbackFrame::CallbackFrame(Ptr<SocketHttpServerConnectionLifecycle> _state)
			: state(_state)
			, previous(currentCallbackFrame)
		{
			currentCallbackFrame = this;
		}

		SocketHttpServerConnection::CallbackFrame::~CallbackFrame()
		{
			currentCallbackFrame = previous;
			Ptr<SocketHttpServerLifecycle> server;
			SocketHttpServerConnection* owner = nullptr;
			CS_LOCK(state->lockState)
			{
				state->activeCallbacks--;
				if (state->activeCallbacks == 0 && state->stopFinished && state->server)
				{
					server = state->server;
					owner = state->owner;
				}
				state->cvState.WakeAllPendings();
			}
			Ptr<SocketHttpServerConnection> releasing;
			if (server && owner) releasing = server->ReleaseStoppedConnection(owner);
		}

		SocketHttpServerConnection::InboundFrame::InboundFrame(Ptr<SocketHttpServerConnectionLifecycle> _state)
			: state(_state)
			, previous(currentInboundFrame)
		{
			currentInboundFrame = this;
		}

		SocketHttpServerConnection::InboundFrame::~InboundFrame()
		{
			currentInboundFrame = previous;
		}

		vint SocketHttpServerConnection::CurrentCallbackDepth(Ptr<SocketHttpServerConnectionLifecycle> state)
		{
			vint depth = 0;
			for (auto frame = currentCallbackFrame; frame; frame = frame->previous)
			{
				if (frame->state == state) depth++;
			}
			return depth;
		}

		bool SocketHttpServerConnection::ClaimPollUnsafe(Ptr<SocketHttpServerConnectionLifecycle> state, PollWork& work)
		{
			if (
				state->stopStarted ||
				!state->accepted ||
				state->inFlightPoll ||
				!state->pendingPoll ||
				state->queuedOutbound.Count() == 0
				)
			{
				return false;
			}

			state->inFlightPoll = state->pendingPoll;
			state->pendingPoll = nullptr;
			state->inFlightMessage = state->queuedOutbound[0];
			state->queuedOutbound.RemoveAt(0);
			work.context = state->inFlightPoll;
			work.message = state->inFlightMessage;
			return true;
		}

		void SocketHttpServerConnection::StartPollResponse(Ptr<SocketHttpServerConnectionLifecycle> state, PollWork work)
		{
			if (!work) return;
			InvokePollClaimed(state->token);
			bool submitted = false;
			try
			{
				submitted = work.context->Respond(
					CreateSuccessResponse(work.message),
					Func<void(bool)>([state, context = work.context](bool succeeded)
					{
						FinishPollResponse(state, context, succeeded);
					})
					);
			}
			catch (...)
			{
			}
			if (!submitted) FinishPollResponse(state, work.context, false);
		}

		void SocketHttpServerConnection::FinishPollResponse(Ptr<SocketHttpServerConnectionLifecycle> state, Ptr<SocketHttpRequestContext> context, bool succeeded)
		{
			PollWork next;
			bool completed = false;
			CS_LOCK(state->lockState)
			{
				if (state->inFlightPoll == context)
				{
					if (!succeeded && !state->stopStarted)
					{
						state->queuedOutbound.Insert(0, state->inFlightMessage);
					}
					state->inFlightPoll = nullptr;
					state->inFlightMessage = WString::Empty;
					ClaimPollUnsafe(state, next);
					state->cvState.WakeAllPendings();
					completed = true;
				}
			}
			if (!completed) return;
			InvokePollCompleted(state->token, succeeded);
			StartPollResponse(state, next);
		}

		void SocketHttpServerConnection::ProcessPollRegistrations(Ptr<SocketHttpServerConnectionLifecycle> state)
		{
			while (true)
			{
				Ptr<SocketHttpRequestContext> context;
				Ptr<SocketHttpRequestContext> replaced;
				PollWork work;
				bool cancel = false;
				state->lockState.Enter();
				if (state->queuedPollRegistrations.Count() == 0)
				{
					auto registered = state->pendingPoll && !state->stopStarted && state->accepted;
					state->pollRegistrationProcessing = false;
					state->cvState.WakeAllPendings();
					state->lockState.Leave();
					if (registered) InvokePollRegistered(state->token);
					return;
				}
				context = state->queuedPollRegistrations[0];
				state->queuedPollRegistrations.RemoveAt(0);
				replaced = state->pendingPoll;
				state->pendingPoll = nullptr;
				state->lockState.Leave();

				if (replaced) replaced->Cancel();

				CS_LOCK(state->lockState)
				{
					if (state->stopStarted || !state->accepted)
					{
						cancel = true;
					}
					else
					{
						state->pendingPoll = context;
						ClaimPollUnsafe(state, work);
					}
				}
				if (cancel) context->Cancel();
				StartPollResponse(state, work);
			}
		}

		SocketHttpServerConnection::SocketHttpServerConnection(Ptr<SocketHttpServerLifecycle> server, const WString& token)
			: lifecycle(Ptr(new SocketHttpServerConnectionLifecycle))
		{
			lifecycle->owner = this;
			lifecycle->server = server;
			lifecycle->token = token;
		}

		void SocketHttpServerConnection::DetachServer(SocketHttpServerLifecycle* server)
		{
			CS_LOCK(lifecycle->lockState)
			{
				if (lifecycle->server.Obj() == server) lifecycle->server = nullptr;
			}
		}

		bool SocketHttpServerConnection::MarkAccepted()
		{
			CS_LOCK(lifecycle->lockState)
			{
				if (!lifecycle->stopStarted)
				{
					lifecycle->accepted = true;
					return true;
				}
			}
			return false;
		}

		bool SocketHttpServerConnection::IsAccepted()
		{
			CS_LOCK(lifecycle->lockState) { return lifecycle->accepted; }
			return false;
		}

		bool SocketHttpServerConnection::HasCurrentCallback()
		{
			return CurrentCallbackDepth(lifecycle) > 0;
		}

		WString SocketHttpServerConnection::GetToken()
		{
			return lifecycle->token;
		}

		WaitForClientResult SocketHttpServerConnection::InvokeClientConnected(SocketHttpServer* server)
		{
			auto state = lifecycle;
			bool invoke = false;
			CS_LOCK(state->lockState)
			{
				if (!state->stopStarted)
				{
					state->activeCallbacks++;
					invoke = true;
				}
			}
			if (!invoke) return WaitForClientResult::Reject;

			WaitForClientResult result;
			{
				CallbackFrame frame(state);
				result = server->OnClientConnected(this);
			}
			if (result != WaitForClientResult::Accept) return WaitForClientResult::Reject;

			Ptr<SocketHttpServerLifecycle> retainedServer;
			CS_LOCK(state->lockState) { retainedServer = state->server; }
			return retainedServer && retainedServer->TryAccept(state->token, this)
				? WaitForClientResult::Accept
				: WaitForClientResult::Reject;
		}

		bool SocketHttpServerConnection::RegisterPoll(Ptr<SocketHttpRequestContext> context)
		{
			auto state = lifecycle;
			bool process = false;
			CS_LOCK(state->lockState)
			{
				if (state->stopStarted || !state->accepted) return false;
				state->queuedPollRegistrations.Add(context);
				if (!state->pollRegistrationProcessing)
				{
					state->pollRegistrationProcessing = true;
					process = true;
				}
			}
			if (process) ProcessPollRegistrations(state);
			return true;
		}

		bool SocketHttpServerConnection::DispatchInbound(const WString& message, WString& response)
		{
			auto state = lifecycle;
			INetworkProtocolCallback* installed = nullptr;
			PollWork work;
			CS_LOCK(state->lockState)
			{
				if (state->stopStarted || !state->accepted) return false;
				if (state->callback && !state->callbackInstalling)
				{
					installed = state->callback;
					state->activeCallbacks++;
				}
				else
				{
					state->queuedInbound.Add(message);
					if (state->queuedOutbound.Count() > 0)
					{
						response = state->queuedOutbound[0];
						state->queuedOutbound.RemoveAt(0);
					}
					ClaimPollUnsafe(state, work);
				}
			}

			if (!installed)
			{
				StartPollResponse(state, work);
				return true;
			}

			List<WString> generated;
			{
				CallbackFrame callbackFrame(state);
				InboundFrame inboundFrame(state);
				installed->OnReadString(message);
				generated = std::move(inboundFrame.generated);
			}

			CS_LOCK(state->lockState)
			{
				if (state->stopStarted) return false;
				if (generated.Count() > 0)
				{
					response = generated[0];
					for (vint i = 1; i < generated.Count(); i++) state->queuedOutbound.Add(generated[i]);
				}
				else if (state->queuedOutbound.Count() > 0)
				{
					response = state->queuedOutbound[0];
					state->queuedOutbound.RemoveAt(0);
				}
				ClaimPollUnsafe(state, work);
			}
			StartPollResponse(state, work);
			return true;
		}

		void SocketHttpServerConnection::StopCore(bool removeFromServer, bool waitForPoll)
		{
			auto state = lifecycle;
			if (removeFromServer)
			{
				Ptr<SocketHttpServerLifecycle> server;
				CS_LOCK(state->lockState) { server = state->server; }
				if (server) server->RemoveConnection(state->token, this);
			}

			auto callbackDepth = CurrentCallbackDepth(state);
			List<Ptr<SocketHttpRequestContext>> cancelling;
			bool first = false;
			bool ownsAssist = false;
			state->lockState.Enter();
			if (!state->stopStarted)
			{
				first = true;
				state->stopStarted = true;
				if (callbackDepth > 0)
				{
					state->stopAssistProcessing = true;
					ownsAssist = true;
				}
				if (state->pendingPoll) cancelling.Add(state->pendingPoll);
				state->pendingPoll = nullptr;
				for (auto context : state->queuedPollRegistrations) cancelling.Add(context);
				state->queuedPollRegistrations.Clear();
				state->queuedInbound.Clear();
				state->queuedOutbound.Clear();
			}
			else if (callbackDepth > 0)
			{
				if (
					state->stopFinished ||
					state->disconnectDelivering ||
					state->disconnectFinished ||
					state->stopAssistProcessing
					)
				{
					state->lockState.Leave();
					return;
				}
				state->stopAssistProcessing = true;
				ownsAssist = true;
				state->lockState.Leave();
			}
			else
			{
				while (!state->stopFinished) state->cvState.SleepWith(state->lockState);
				while (state->activeCallbacks > 0 || (waitForPoll && (state->inFlightPoll || state->pollRegistrationProcessing)))
				{
					state->cvState.SleepWith(state->lockState);
				}
				state->lockState.Leave();
				return;
			}
			if (first) state->lockState.Leave();

			if (first)
			{
				for (auto context : cancelling)
				{
					try { context->Cancel(); } catch (...) {}
				}
				CS_LOCK(state->lockState)
				{
					state->stopCancellationFinished = true;
					state->cvState.WakeAllPendings();
				}
			}

			INetworkProtocolCallback* disconnected = nullptr;
			state->lockState.Enter();
			while (!state->stopCancellationFinished || state->pollRegistrationProcessing)
			{
				state->cvState.SleepWith(state->lockState);
			}
			if (first && !ownsAssist)
			{
				while (state->stopAssistProcessing) state->cvState.SleepWith(state->lockState);
			}
			while (state->activeCallbacks > callbackDepth)
			{
				state->cvState.SleepWith(state->lockState);
			}
			if (state->accepted && state->callback && !state->disconnectDelivering && !state->disconnectFinished)
			{
				disconnected = state->callback;
				state->disconnectDelivering = true;
				state->activeCallbacks++;
			}
			state->lockState.Leave();

			if (disconnected)
			{
				try
				{
					CallbackFrame frame(state);
					disconnected->OnDisconnected();
				}
				catch (...)
				{
				}
			}

			Ptr<SocketHttpServerLifecycle> releasingServer;
			CS_LOCK(state->lockState)
			{
				state->callback = nullptr;
				state->callbackInstalling = false;
				state->disconnectDelivering = false;
				state->disconnectFinished = true;
				if (ownsAssist) state->stopAssistProcessing = false;
				if (first)
				{
					state->stopFinished = true;
					if (state->activeCallbacks == 0) releasingServer = state->server;
				}
				state->cvState.WakeAllPendings();
			}
			Ptr<SocketHttpServerConnection> releasing;
			if (releasingServer) releasing = releasingServer->ReleaseStoppedConnection(this);
			if (first && waitForPoll)
			{
				CS_LOCK(state->lockState)
				{
					while (state->inFlightPoll) state->cvState.SleepWith(state->lockState);
				}
			}
		}

		void SocketHttpServerConnection::StopFromServer()
		{
			StopCore(false, false);
		}

		void SocketHttpServerConnection::WaitForPollCompletion()
		{
			CS_LOCK(lifecycle->lockState)
			{
				while (lifecycle->inFlightPoll || lifecycle->pollRegistrationProcessing)
				{
					lifecycle->cvState.SleepWith(lifecycle->lockState);
				}
			}
		}

		void SocketHttpServerConnection::InstallCallback(INetworkProtocolCallback* callback)
		{
			auto state = lifecycle;
			if (!callback)
			{
				auto callbackDepth = CurrentCallbackDepth(state);
				CS_LOCK(state->lockState)
				{
					state->callback = nullptr;
					while (state->activeCallbacks > callbackDepth)
					{
						state->cvState.SleepWith(state->lockState);
					}
				}
				return;
			}

			bool canInstall = false;
			CS_LOCK(state->lockState)
			{
				if (!state->callback && !state->callbackInstalling && !state->stopStarted)
				{
					state->callback = callback;
					state->callbackInstalling = true;
					state->activeCallbacks++;
					canInstall = true;
				}
			}
			CHECK_ERROR(canInstall, L"SocketHttpServerConnection::InstallCallback cannot replace a callback or install one on a stopped connection.");

			try
			{
				CallbackFrame frame(state);
				callback->OnInstalled(this);
				while (true)
				{
					WString message;
					bool replay = false;
					CS_LOCK(state->lockState)
					{
						if (!state->stopStarted && state->callback == callback && state->queuedInbound.Count() > 0)
						{
							message = state->queuedInbound[0];
							state->queuedInbound.RemoveAt(0);
							replay = true;
						}
						else
						{
							state->callbackInstalling = false;
							state->cvState.WakeAllPendings();
						}
					}
					if (!replay) break;
					callback->OnReadString(message);
				}
			}
			catch (...)
			{
				CS_LOCK(state->lockState)
				{
					if (state->callback == callback) state->callback = nullptr;
					state->callbackInstalling = false;
					state->cvState.WakeAllPendings();
				}
				throw;
			}
		}

		void SocketHttpServerConnection::BeginReadingLoopUnsafe()
		{
		}

		void SocketHttpServerConnection::SendString(const WString& str)
		{
			Array<vuint8_t> validated;
			EncodeMessage(str, validated);
			auto state = lifecycle;
			PollWork work;
			CS_LOCK(state->lockState)
			{
				CHECK_ERROR(!state->stopStarted, L"SocketHttpServerConnection::SendString cannot send on a stopped connection.");
				if (currentInboundFrame && currentInboundFrame->state == state)
				{
					currentInboundFrame->generated.Add(str);
					return;
				}
				state->queuedOutbound.Add(str);
				ClaimPollUnsafe(state, work);
			}
			StartPollResponse(state, work);
		}

		void SocketHttpServerConnection::Stop()
		{
			StopCore(true, true);
		}
	}

	class SocketHttpServer::Impl : public Object
	{
	public:
		enum class BeginStopResult
		{
			Continue,
			ReturnFollower,
		};

		Ptr<SocketHttpServerLifecycle> lifecycle;

		Impl(SocketHttpServer* owner)
			: lifecycle(Ptr(new SocketHttpServerLifecycle(owner)))
		{
		}

		static bool HasCurrentCallback(List<Ptr<SocketHttpServerConnection>>& stopping)
		{
			for (auto connection : stopping)
			{
				if (connection->HasCurrentCallback()) return true;
			}
			return false;
		}

		static void DrainConnections(List<Ptr<SocketHttpServerConnection>>& stopping)
		{
			for (auto connection : stopping)
			{
				if (!connection->HasCurrentCallback()) connection->StopFromServer();
			}
			for (auto connection : stopping)
			{
				if (connection->HasCurrentCallback()) connection->StopFromServer();
			}
		}

		void ExecuteAssistant(List<Ptr<SocketHttpServerConnection>>& stopping)
		{
			try
			{
				SocketHttpServerStopScope scope(lifecycle.Obj(), true);
				DrainConnections(stopping);
			}
			catch (...)
			{
				lifecycle->FinishStopAssist();
				throw;
			}
			lifecycle->FinishStopAssist();
		}

		BeginStopResult BeginStop(List<Ptr<SocketHttpServerConnection>>& stopping)
		{
			lifecycle->PrepareStop(stopping);
			auto existingFrame = FindSocketHttpServerStopFrame(lifecycle.Obj());
			auto callbackNested = HasCurrentCallback(stopping);
			bool execute = false;
			bool assist = false;
			lifecycle->PrepareStopProcessing(
				callbackNested,
				existingFrame && existingFrame->ownsAssist,
				execute,
				assist
				);

			if (execute)
			{
				try
				{
					SocketHttpServerStopScope scope(lifecycle.Obj(), assist);
					DrainConnections(stopping);
				}
				catch (...)
				{
					lifecycle->FinishStop(assist);
					throw;
				}
				lifecycle->FinishStop(assist);
				return BeginStopResult::Continue;
			}

			if (existingFrame)
			{
				if (existingFrame->ownsAssist)
				{
					DrainConnections(stopping);
				}
				else if (assist)
				{
					ExecuteAssistant(stopping);
				}
				return BeginStopResult::ReturnFollower;
			}

			if (callbackNested)
			{
				if (assist) ExecuteAssistant(stopping);
				return BeginStopResult::ReturnFollower;
			}

			lifecycle->WaitForStop();
			DrainConnections(stopping);
			return BeginStopResult::Continue;
		}

		void OnRequest(SocketHttpServer* owner, Ptr<SocketHttpRequestContext> context)
		{
			auto request = context->GetRequest();
			auto path = context->GetRelativePath();
			if (!request || context->GetQuery() != WString::Empty)
			{
				context->Respond(CreateErrorResponse(L"Route not found"));
				return;
			}

			if (request->method == L"GET" && path == HttpServerUrl_Connect && HasEmptyBody(request))
			{
				auto connection = lifecycle->CreateConnection(lifecycle);
				if (!connection)
				{
					context->Respond(CreateErrorResponse(L"Connection rejected"));
					return;
				}

				WaitForClientResult result = WaitForClientResult::Reject;
				try { result = connection->InvokeClientConnected(owner); }
				catch (...) { result = WaitForClientResult::Reject; }
				if (result != WaitForClientResult::Accept)
				{
					connection->Stop();
					context->Respond(CreateErrorResponse(L"Connection rejected"));
					return;
				}

				auto token = connection->GetToken();
				auto body = WString::Unmanaged(HttpServerUrl_Request) + L"/" + token + L";" + HttpServerUrl_Response + L"/" + token;
				context->Respond(CreateSuccessResponse(body));
				return;
			}

			WString token;
			if (request->method == L"POST" && ExtractToken(path, HttpServerUrl_Request, token) && HasEmptyBody(request))
			{
				auto connection = lifecycle->FindConnection(token);
				if (connection && connection->RegisterPoll(context)) return;
				context->Respond(CreateErrorResponse(L"Connection not found"));
				return;
			}

			WString message;
			if (request->method == L"POST" && ExtractToken(path, HttpServerUrl_Response, token) && DecodeSubmittedMessage(request, message))
			{
				auto connection = lifecycle->FindConnection(token);
				WString response;
				if (connection && connection->DispatchInbound(message, response))
				{
					context->Respond(CreateSuccessResponse(response));
					return;
				}
			}

			context->Respond(CreateErrorResponse(L"Route not found"));
		}
	};

	void SetSocketHttpServerPollCallbacksForTesting(
		const Func<void(const WString&)>& claimed,
		const Func<void(const WString&, bool)>& completed,
		const Func<void(const WString&)>& registered
		)
	{
		auto& hooks = GetSocketHttpServerTestHooks();
		SPIN_LOCK(hooks.lock)
		{
			hooks.claimed = claimed;
			hooks.completed = completed;
			hooks.registered = registered;
		}
	}

	void ResetSocketHttpServerPollCallbacksForTesting()
	{
		SetSocketHttpServerPollCallbacksForTesting({}, {}, {});
	}

	SocketHttpServer::SocketHttpServer(const WString& baseUrl, vint port)
		: SocketHttpServerApi(CreateServerUrlPrefix(baseUrl, port), true)
		, impl(Ptr(new Impl(this)))
	{
	}

	SocketHttpServer::~SocketHttpServer()
	{
		try { Stop(); } catch (...) {}
		CS_LOCK(impl->lifecycle->lockState) { impl->lifecycle->owner = nullptr; }
	}

	WaitForClientResult SocketHttpServer::OnClientConnected(INetworkProtocolConnection*)
	{
		return WaitForClientResult::Accept;
	}

	void SocketHttpServer::OnHttpRequestReceived(Ptr<SocketHttpRequestContext> context)
	{
		impl->OnRequest(this, context);
	}

	void SocketHttpServer::OnHttpServerStopping()
	{
		List<Ptr<SocketHttpServerConnection>> stopping;
		impl->BeginStop(stopping);
	}

	void SocketHttpServer::Start()
	{
		impl->lifecycle->PrepareStart();
		try
		{
			SocketHttpServerApi::Start();
		}
		catch (...)
		{
			List<Ptr<SocketHttpServerConnection>> stopping;
			impl->BeginStop(stopping);
			throw;
		}
	}

	void SocketHttpServer::Stop()
	{
		List<Ptr<SocketHttpServerConnection>> stopping;
		if (impl->BeginStop(stopping) == Impl::BeginStopResult::ReturnFollower) return;
		SocketHttpServerApi::Stop();
		for (auto connection : stopping) connection->WaitForPollCompletion();
	}

	bool SocketHttpServer::IsStopped()
	{
		return impl->lifecycle->IsStopped() || SocketHttpServerApi::IsStopped();
	}
}
