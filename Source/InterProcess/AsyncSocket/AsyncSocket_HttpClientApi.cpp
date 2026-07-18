/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	SocketHttpClientApi

***********************************************************************/

#include "AsyncSocket_HttpClientApi.h"

namespace vl::inter_process::async_tcp_socket
{
	using namespace vl::collections;

	namespace
	{
		enum class SocketHttpClientErrorCode : vuint32_t
		{
			InvalidRequest = 1,
			Stopped = 2,
			Transport = 3,
			UnsupportedCoding = 4,
		};

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

		WString FoldAsciiFieldName(const WString& name)
		{
			Array<wchar_t> characters(name.Length());
			for (vint i = 0; i < name.Length(); i++)
			{
				characters[i] = FoldAscii(name[i]);
			}
			return characters.Count() == 0 ? WString() : WString::CopyFrom(&characters[0], characters.Count());
		}

		WString TrimHttpWhitespace(const WString& value)
		{
			vint begin = 0;
			vint end = value.Length();
			while (begin < end && (value[begin] == L' ' || value[begin] == L'\t')) begin++;
			while (begin < end && (value[end - 1] == L' ' || value[end - 1] == L'\t')) end--;
			return value.Sub(begin, end - begin);
		}

		bool ContainsOnlyIdentityCoding(const WString& value)
		{
			vint begin = 0;
			bool found = false;
			while (begin <= value.Length())
			{
				vint end = begin;
				while (end < value.Length() && value[end] != L',') end++;
				auto coding = TrimHttpWhitespace(value.Sub(begin, end - begin));
				if (!AsciiEqualsIgnoreCase(coding, L"identity")) return false;
				found = true;
				if (end == value.Length()) break;
				begin = end + 1;
			}
			return found;
		}

		bool ValidateAuthority(const WString& authority)
		{
			if (authority.Length() == 0) return false;
			for (vint i = 0; i < authority.Length(); i++)
			{
				auto c = authority[i];
				if (c <= 0x20 || c > 0x7E || c == L'/' || c == L'?' || c == L'#' || c == L'@') return false;
			}

			WString host;
			vint portStart = -1;
			if (authority[0] == L'[') return false;
			vint colon = -1;
			for (vint i = 0; i < authority.Length(); i++)
			{
				if (authority[i] == L':')
				{
					if (colon != -1) return false;
					colon = i;
				}
			}
			if (colon <= 0 || colon + 1 >= authority.Length()) return false;
			host = authority.Sub(0, colon);
			portStart = colon + 1;

			if (
				!AsciiEqualsIgnoreCase(host, L"localhost") &&
				host != L"127.0.0.1"
				)
			{
				return false;
			}

			vint port = 0;
			for (vint i = portStart; i < authority.Length(); i++)
			{
				if (authority[i] < L'0' || authority[i] > L'9') return false;
				port = port * 10 + authority[i] - L'0';
				if (port > 65535) return false;
			}
			return 1 <= port && port <= 65535;
		}

		HttpField CreateField(const WString& name, const WString& value)
		{
			HttpField field;
			field.name = FoldAsciiFieldName(name);
			auto utf8 = wtou8(value);
			field.value.Resize(utf8.Length());
			if (utf8.Length() > 0)
			{
				memcpy(&field.value[0], utf8.Buffer(), utf8.Length());
			}
			return field;
		}

		WString DecodeFieldValue(const Array<vuint8_t>& value)
		{
			if (value.Count() == 0) return WString::Empty;
			Array<char8_t> utf8(value.Count());
			for (vint i = 0; i < value.Count(); i++)
			{
				utf8[i] = (char8_t)value[i];
			}
			return u8tow(U8String::CopyFrom(&utf8[0], utf8.Count()));
		}

		windows_http::HttpError MakeError(const WString& operation, const WString& message, SocketHttpClientErrorCode code)
		{
			windows_http::HttpError error;
			error.operation = operation;
			error.errorCode = (vuint32_t)code;
			error.message = message;
			return error;
		}
	}

/***********************************************************************
SocketHttpClientApi::Impl
***********************************************************************/

	class SocketHttpClientApi::Impl : public Object, public virtual IHttpRequestCallback
	{
		using QueryResult = Variant<windows_http::HttpResponse, windows_http::HttpError>;
		using QueryCallback = Func<void(QueryResult)>;

		class Query : public Object
		{
		public:
			Ptr<HttpRequest>				request;
			vint						responseTimeout = 0;
			QueryCallback				callback;
			bool						completed = false;
		};

		struct CallbackFrame
		{
			Impl*						owner = nullptr;
			Ptr<Impl>					self;
			CallbackFrame*				previous = nullptr;

			CallbackFrame(Impl* _owner, Ptr<Impl> _self)
				: owner(_owner)
				, self(_self)
				, previous(currentCallbackFrame)
			{
				currentCallbackFrame = this;
			}

			~CallbackFrame()
			{
				currentCallbackFrame = previous;
				CS_LOCK(owner->lockState)
				{
					owner->activeCallbacks--;
					owner->cvState.WakeAllPendings();
				}
			}
		};

		struct ResponseFrame
		{
			Impl*						owner = nullptr;
			Ptr<Impl>					self;
			ResponseFrame*				previous = nullptr;

			ResponseFrame(Impl* _owner, Ptr<Impl> _self)
				: owner(_owner)
				, self(_self)
				, previous(currentResponseFrame)
			{
				currentResponseFrame = this;
			}

			~ResponseFrame()
			{
				currentResponseFrame = previous;
			}
		};

		static thread_local CallbackFrame*	currentCallbackFrame;
		static thread_local ResponseFrame*	currentResponseFrame;

		Ptr<HttpRequestClient>			client;
		IHttpRequestConnection*			connection = nullptr;
		WString						authority;
		Ptr<Impl>					selfReference;

		CriticalSection					lockState;
		ConditionVariable				cvState;
		Ptr<Query>					activeQuery;
		List<Ptr<Query>>				queuedQueries;
		List<Ptr<Query>>				pendingQueries;
		vint						activeCallbacks = 0;
		bool						waitStarted = false;
		bool						readingStarted = false;
		bool						responseDispatching = false;
		bool						terminal = false;
		bool						stopStarted = false;
		bool						stopFinished = false;

		Ptr<Impl> RetainSelf()
		{
			Ptr<Impl> self;
			CS_LOCK(lockState)
			{
				self = selfReference;
			}
			return self;
		}

		vint CurrentCallbackDepth()
		{
			vint depth = 0;
			for (auto frame = currentCallbackFrame; frame; frame = frame->previous)
			{
				if (frame->owner == this) depth++;
			}
			return depth;
		}

		bool IsInsideResponseCallback()
		{
			for (auto frame = currentResponseFrame; frame; frame = frame->previous)
			{
				if (frame->owner == this) return true;
			}
			return false;
		}

		Ptr<Query> TakeFirstQueuedUnsafe()
		{
			auto query = queuedQueries[0];
			queuedQueries.RemoveAt(0);
			return query;
		}

		void TakeAllQueriesUnsafe(List<Ptr<Query>>& queries)
		{
			if (activeQuery)
			{
				queries.Add(activeQuery);
				activeQuery = nullptr;
			}
			for (auto query : queuedQueries)
			{
				queries.Add(query);
			}
			queuedQueries.Clear();
			for (auto query : pendingQueries)
			{
				queries.Add(query);
			}
			pendingQueries.Clear();
		}

		void MoveAllQueriesToPendingUnsafe()
		{
			if (activeQuery)
			{
				pendingQueries.Add(activeQuery);
				activeQuery = nullptr;
			}
			for (auto query : queuedQueries)
			{
				pendingQueries.Add(query);
			}
			queuedQueries.Clear();
		}

		bool ReserveCallbackUnsafe(Ptr<Query> query, QueryCallback& callback)
		{
			if (!query || query->completed) return false;
			query->completed = true;
			callback = query->callback;
			if (callback) activeCallbacks++;
			return true;
		}

		void InvokeReserved(QueryCallback callback, QueryResult result)
		{
			if (!callback) return;
			CallbackFrame frame(this, RetainSelf());
			try
			{
				callback(std::move(result));
			}
			catch (...)
			{
			}
		}

		void CompleteWithError(Ptr<Query> query, const windows_http::HttpError& error)
		{
			QueryCallback callback;
			bool reserved = false;
			CS_LOCK(lockState)
			{
				reserved = ReserveCallbackUnsafe(query, callback);
			}
			if (reserved)
			{
				InvokeReserved(callback, QueryResult(error));
			}
		}

		void CompleteAllWithError(List<Ptr<Query>>& queries, const windows_http::HttpError& error)
		{
			for (auto query : queries)
			{
				CompleteWithError(query, error);
			}
		}

		void CompletePendingWithError(const windows_http::HttpError& error)
		{
			while (true)
			{
				QueryCallback callback;
				bool reserved = false;
				CS_LOCK(lockState)
				{
					if (!stopStarted && pendingQueries.Count() > 0)
					{
						auto query = pendingQueries[0];
						pendingQueries.RemoveAt(0);
						reserved = ReserveCallbackUnsafe(query, callback);
					}
				}
				if (!reserved) return;
				InvokeReserved(callback, QueryResult(error));
			}
		}

		void InvokeDirect(QueryCallback callback, const windows_http::HttpError& error)
		{
			if (!callback) return;
			CS_LOCK(lockState)
			{
				activeCallbacks++;
			}
			InvokeReserved(callback, QueryResult(error));
		}

		Ptr<Query> CreateQuery(const windows_http::HttpRequest& request, windows_http::HttpError& error)
		{
			if (request.secure)
			{
				error = MakeError(L"SocketHttpClientApi::HttpQuery", L"TLS is not supported by SocketHttpClientApi.", SocketHttpClientErrorCode::InvalidRequest);
				return nullptr;
			}
			if (request.username != WString::Empty || request.password != WString::Empty)
			{
				error = MakeError(L"SocketHttpClientApi::HttpQuery", L"Credentials are not supported by SocketHttpClientApi.", SocketHttpClientErrorCode::InvalidRequest);
				return nullptr;
			}
			if (request.keepAliveOnStop)
			{
				error = MakeError(L"SocketHttpClientApi::HttpQuery", L"keepAliveOnStop is not supported by SocketHttpClientApi.", SocketHttpClientErrorCode::InvalidRequest);
				return nullptr;
			}

			auto query = Ptr(new Query);
			query->request = Ptr(new HttpRequest);
			query->responseTimeout = request.receiveTimeout;
			query->callback = {};
			query->request->method = request.method == WString::Empty ? WString::Unmanaged(L"GET") : request.method;
			query->request->requestTarget = request.query == WString::Empty ? WString::Unmanaged(L"/") : request.query;

			query->request->headers.Add(CreateAsciiHttpField(L"Host", authority));
			query->request->headers.Add(CreateAsciiHttpField(L"Accept-Encoding", L"identity"));
			for (vint i = 0; i < request.acceptTypes.Count(); i++)
			{
				query->request->headers.Add(CreateField(L"Accept", request.acceptTypes.Get(i)));
			}
			if (request.contentType != WString::Empty)
			{
				query->request->headers.Add(CreateField(L"Content-Type", request.contentType));
			}
			if (request.cookie != WString::Empty)
			{
				query->request->headers.Add(CreateField(L"Cookie", request.cookie));
			}

			for (vint i = 0; i < request.extraHeaders.Count(); i++)
			{
				auto name = request.extraHeaders.Keys()[i];
				auto value = request.extraHeaders.Values()[i];
				if (AsciiEqualsIgnoreCase(name, L"Host"))
				{
					if (!AsciiEqualsIgnoreCase(TrimHttpWhitespace(value), authority))
					{
						error = MakeError(L"SocketHttpClientApi::HttpQuery", L"A caller-supplied Host field conflicts with the constructor authority.", SocketHttpClientErrorCode::InvalidRequest);
						return nullptr;
					}
					continue;
				}
				if (AsciiEqualsIgnoreCase(name, L"Accept-Encoding"))
				{
					if (!ContainsOnlyIdentityCoding(value))
					{
						error = MakeError(L"SocketHttpClientApi::HttpQuery", L"SocketHttpClientApi only supports Accept-Encoding: identity.", SocketHttpClientErrorCode::InvalidRequest);
						return nullptr;
					}
					continue;
				}
				query->request->headers.Add(CreateField(name, value));
			}

			if (request.body.Count() > 0)
			{
				HttpBodyChunk chunk;
				chunk.data.Resize(request.body.Count());
				memcpy(&chunk.data[0], &request.body.Get(0), request.body.Count());
				query->request->body.chunks.Add(std::move(chunk));
			}
			return query;
		}

		bool ConvertResponse(Ptr<HttpResponse> response, windows_http::HttpResponse& output, windows_http::HttpError& error)
		{
			if (!response)
			{
				error = MakeError(L"SocketHttpClientApi::OnReadResponse", L"The HTTP request layer returned an empty response.", SocketHttpClientErrorCode::Transport);
				return false;
			}

			output.statusCode = response->statusCode;
			bool contentTypeAssigned = false;
			bool cookieAssigned = false;
			auto processField = [&](const HttpField& field)
			{
				if (AsciiEqualsIgnoreCase(field.name, L"Content-Encoding"))
				{
					if (!ContainsOnlyIdentityCoding(DecodeFieldValue(field.value)))
					{
						error = MakeError(L"SocketHttpClientApi::OnReadResponse", L"The server returned an unsupported Content-Encoding.", SocketHttpClientErrorCode::UnsupportedCoding);
						return false;
					}
				}
				else if (!contentTypeAssigned && AsciiEqualsIgnoreCase(field.name, L"Content-Type"))
				{
					output.contentType = DecodeFieldValue(field.value);
					contentTypeAssigned = true;
				}
				else if (!cookieAssigned && AsciiEqualsIgnoreCase(field.name, L"Set-Cookie"))
				{
					output.cookie = DecodeFieldValue(field.value);
					cookieAssigned = true;
				}
				return true;
			};
			for (auto&& field : response->headers)
			{
				if (!processField(field)) return false;
			}
			for (auto&& field : response->body.trailers)
			{
				if (!processField(field)) return false;
			}

			Array<vuint8_t> body;
			if (!FlattenHttpBody(response->body, body))
			{
				error = MakeError(L"SocketHttpClientApi::OnReadResponse", L"The response body is too large to flatten.", SocketHttpClientErrorCode::Transport);
				return false;
			}
			output.body.Resize(body.Count());
			for (vint i = 0; i < body.Count(); i++)
			{
				output.body[i] = (char)body[i];
			}
			return true;
		}

		bool TrySend(Ptr<Query> query, windows_http::HttpError& error)
		{
			try
			{
				connection->SendRequest(query->request, query->responseTimeout);
				return true;
			}
			catch (...)
			{
				error = MakeError(L"SocketHttpClientApi::HttpQuery", L"The HTTP request layer rejected the exchange.", SocketHttpClientErrorCode::Transport);
				return false;
			}
		}

		void StopConnectionNoThrow()
		{
			try
			{
				connection->Stop();
			}
			catch (...)
			{
			}
		}

		void HandleSendFailure(Ptr<Query> query, const windows_http::HttpError& error)
		{
			bool stopConnection = false;
			CS_LOCK(lockState)
			{
				if (!query->completed && !stopStarted && !terminal)
				{
					terminal = true;
					responseDispatching = false;
					MoveAllQueriesToPendingUnsafe();
					stopConnection = true;
				}
			}
			if (stopConnection)
			{
				StopConnectionNoThrow();
				CompletePendingWithError(error);
			}
		}

		void HandleTerminalError(const windows_http::HttpError& error, bool stopConnectionImmediately)
		{
			bool shouldStop = false;
			CS_LOCK(lockState)
			{
				if (!stopStarted && !terminal)
				{
					terminal = true;
					responseDispatching = false;
					MoveAllQueriesToPendingUnsafe();
					shouldStop = stopConnectionImmediately;
				}
			}
			if (shouldStop)
			{
				StopConnectionNoThrow();
			}
			CompletePendingWithError(error);
		}

	public:
		Impl(Ptr<IAsyncSocketClient> socketClient, const WString& _authority)
			: client(new HttpRequestClient(socketClient))
			, authority(_authority)
		{
			connection = client->GetConnection();
		}

		void Initialize(Ptr<Impl> self)
		{
			CS_LOCK(lockState)
			{
				selfReference = self;
			}
			try
			{
				connection->InstallCallback(this);
			}
			catch (...)
			{
				CS_LOCK(lockState)
				{
					selfReference = nullptr;
				}
				throw;
			}
		}

		void ReleaseSelf()
		{
			CS_LOCK(lockState)
			{
				selfReference = nullptr;
			}
		}

		void WaitForServer()
		{
			CS_LOCK(lockState)
			{
				CHECK_ERROR(!waitStarted && !stopStarted && !terminal, L"SocketHttpClientApi::WaitForServer can only be called once on an active client.");
				waitStarted = true;
			}

			try
			{
				client->WaitForServer();
			}
			catch (...)
			{
				CS_LOCK(lockState)
				{
					terminal = true;
				}
				throw;
			}

			bool beginReading = false;
			CS_LOCK(lockState)
			{
				beginReading = !stopStarted && !terminal;
			}
			if (!beginReading) return;

			try
			{
				connection->BeginReadingLoopUnsafe();
			}
			catch (...)
			{
				CS_LOCK(lockState)
				{
					terminal = true;
				}
				throw;
			}
			CS_LOCK(lockState)
			{
				readingStarted = !stopStarted && !terminal;
			}
		}

		ClientStatus GetStatus()
		{
			return client->GetStatus();
		}

		void HttpQuery(const windows_http::HttpRequest& request, QueryCallback callback)
		{
			auto self = RetainSelf();
			windows_http::HttpError error;
			Ptr<Query> query;
			try
			{
				query = CreateQuery(request, error);
			}
			catch (...)
			{
				error = MakeError(L"SocketHttpClientApi::HttpQuery", L"The request could not be translated to the socket HTTP representation.", SocketHttpClientErrorCode::InvalidRequest);
			}
			if (!query)
			{
				InvokeDirect(callback, error);
				return;
			}
			query->callback = callback;

			bool start = false;
			bool reject = false;
			bool notReady = false;
			CS_LOCK(lockState)
			{
				if (stopStarted || terminal)
				{
					reject = true;
				}
				else if (!readingStarted)
				{
					notReady = true;
				}
				else if (!activeQuery && queuedQueries.Count() == 0 && (!responseDispatching || IsInsideResponseCallback()))
				{
					activeQuery = query;
					start = true;
				}
				else
				{
					queuedQueries.Add(query);
				}
			}

			if (reject)
			{
				CompleteWithError(query, MakeError(L"SocketHttpClientApi::HttpQuery", L"SocketHttpClientApi has stopped accepting work.", SocketHttpClientErrorCode::Stopped));
			}
			else if (notReady)
			{
				CompleteWithError(query, MakeError(L"SocketHttpClientApi::HttpQuery", L"WaitForServer must complete before sending an HTTP query.", SocketHttpClientErrorCode::InvalidRequest));
			}
			else if (start)
			{
				if (!TrySend(query, error))
				{
					HandleSendFailure(query, error);
				}
			}
		}

		void Stop()
		{
			auto self = RetainSelf();
			List<Ptr<Query>> cancelledQueries;
			auto callbackDepth = CurrentCallbackDepth();
			bool executeStop = false;
			lockState.Enter();
			if (stopFinished)
			{
				while (activeCallbacks > callbackDepth) cvState.SleepWith(lockState);
				lockState.Leave();
				return;
			}
			if (!stopStarted)
			{
				stopStarted = true;
				terminal = true;
				responseDispatching = false;
				TakeAllQueriesUnsafe(cancelledQueries);
				executeStop = true;
			}
			else if (callbackDepth > 0)
			{
				lockState.Leave();
				return;
			}
			else
			{
				while (!stopFinished) cvState.SleepWith(lockState);
				while (activeCallbacks > 0) cvState.SleepWith(lockState);
				lockState.Leave();
				return;
			}
			lockState.Leave();

			if (executeStop)
			{
				StopConnectionNoThrow();
				CompleteAllWithError(
					cancelledQueries,
					MakeError(L"SocketHttpClientApi::Stop", L"The HTTP query was cancelled because the client stopped.", SocketHttpClientErrorCode::Stopped)
					);
			}

			CS_LOCK(lockState)
			{
				while (activeCallbacks > callbackDepth) cvState.SleepWith(lockState);
				stopFinished = true;
				cvState.WakeAllPendings();
			}
		}

		void OnReadRequest(Ptr<HttpRequest>) override
		{
			auto self = RetainSelf();
			if (!self) return;
			HandleTerminalError(
				MakeError(L"SocketHttpClientApi::OnReadRequest", L"The client connection received a request instead of a response.", SocketHttpClientErrorCode::Transport),
				true
				);
		}

		void OnReadRequestFailure(HttpRequestFailure) override
		{
			auto self = RetainSelf();
			if (!self) return;
			HandleTerminalError(
				MakeError(L"SocketHttpClientApi::OnReadRequestFailure", L"The client connection reported a request parsing failure.", SocketHttpClientErrorCode::Transport),
				true
				);
		}

		void OnReadResponse(Ptr<HttpResponse> response) override
		{
			auto self = RetainSelf();
			if (!self) return;
			ResponseFrame responseFrame(this, self);

			windows_http::HttpResponse convertedResponse;
			windows_http::HttpError responseError;
			auto converted = ConvertResponse(response, convertedResponse, responseError);
			Ptr<Query> completedQuery;
			Ptr<Query> nextQuery;
			QueryCallback completedCallback;
			bool reserved = false;
			bool stopForResponse = false;
			windows_http::HttpError terminalError;
			CS_LOCK(lockState)
			{
				if (activeQuery && !activeQuery->completed)
				{
					responseDispatching = true;
					completedQuery = activeQuery;
					activeQuery = nullptr;
					reserved = ReserveCallbackUnsafe(completedQuery, completedCallback);
					if (!converted)
					{
						terminal = true;
						MoveAllQueriesToPendingUnsafe();
						stopForResponse = true;
						terminalError = responseError;
					}
					else if (!stopStarted && !terminal && queuedQueries.Count() > 0)
					{
						nextQuery = TakeFirstQueuedUnsafe();
						activeQuery = nextQuery;
					}
				}
			}
			if (!reserved) return;

			windows_http::HttpError sendError;
			bool sendFailed = nextQuery && !TrySend(nextQuery, sendError);
			if (sendFailed)
			{
				CS_LOCK(lockState)
				{
					if (!nextQuery->completed && !stopStarted && !terminal)
					{
						terminal = true;
						MoveAllQueriesToPendingUnsafe();
						stopForResponse = true;
						terminalError = sendError;
					}
				}
			}

			if (converted)
			{
				InvokeReserved(completedCallback, QueryResult(std::move(convertedResponse)));
			}
			else
			{
				InvokeReserved(completedCallback, QueryResult(responseError));
			}

			Ptr<Query> lateQuery;
			if (!stopForResponse)
			{
				CS_LOCK(lockState)
				{
					if (!stopStarted && !terminal && !activeQuery && queuedQueries.Count() > 0)
					{
						lateQuery = TakeFirstQueuedUnsafe();
						activeQuery = lateQuery;
					}
					responseDispatching = false;
				}
				if (lateQuery && !TrySend(lateQuery, sendError))
				{
					CS_LOCK(lockState)
					{
						if (!lateQuery->completed && !stopStarted && !terminal)
						{
							terminal = true;
							MoveAllQueriesToPendingUnsafe();
							stopForResponse = true;
							terminalError = sendError;
						}
					}
				}
			}
			else
			{
				CS_LOCK(lockState)
				{
					responseDispatching = false;
				}
			}

			if (stopForResponse)
			{
				StopConnectionNoThrow();
				CompletePendingWithError(terminalError);
			}
		}

		void OnWriteCompleted() override
		{
		}

		void OnError(const WString& error, bool fatal) override
		{
			auto self = RetainSelf();
			if (!self) return;
			// The raw layer stops after delivering a fatal error. A nonfatal error
			// needs an explicit stop after this wrapper terminalizes its queue.
			auto stopConnectionImmediately = !fatal;
			HandleTerminalError(
				MakeError(L"SocketHttpClientApi::OnError", error, SocketHttpClientErrorCode::Transport),
				stopConnectionImmediately
				);
		}

		void OnConnected() override
		{
		}

		void OnDisconnected() override
		{
			auto self = RetainSelf();
			if (!self) return;
			HandleTerminalError(
				MakeError(L"SocketHttpClientApi::OnDisconnected", L"The HTTP connection was disconnected.", SocketHttpClientErrorCode::Transport),
				false
				);
		}

		void OnInstalled(IHttpRequestConnection* installedConnection) override
		{
			CHECK_ERROR(installedConnection == connection, L"SocketHttpClientApi was installed on an unexpected HTTP connection.");
		}
	};

	thread_local SocketHttpClientApi::Impl::CallbackFrame* SocketHttpClientApi::Impl::currentCallbackFrame = nullptr;
	thread_local SocketHttpClientApi::Impl::ResponseFrame* SocketHttpClientApi::Impl::currentResponseFrame = nullptr;

/***********************************************************************
SocketHttpClientApi
***********************************************************************/

	SocketHttpClientApi::SocketHttpClientApi(Ptr<IAsyncSocketClient> client, const WString& authority)
	{
		CHECK_ERROR(client, L"SocketHttpClientApi requires an asynchronous socket client.");
		CHECK_ERROR(ValidateAuthority(authority), L"SocketHttpClientApi requires an explicit loopback authority and port.");
		auto created = Ptr(new Impl(client, authority));
		created->Initialize(created);
		impl = created;
	}

	SocketHttpClientApi::~SocketHttpClientApi()
	{
		impl->Stop();
		impl->ReleaseSelf();
	}

	void SocketHttpClientApi::WaitForServer()
	{
		impl->WaitForServer();
	}

	ClientStatus SocketHttpClientApi::GetStatus()
	{
		return impl->GetStatus();
	}

	void SocketHttpClientApi::HttpQuery(
		const windows_http::HttpRequest& request,
		Func<void(Variant<windows_http::HttpResponse, windows_http::HttpError>)> callback
		)
	{
		impl->HttpQuery(request, callback);
	}

	void SocketHttpClientApi::Stop()
	{
		impl->Stop();
	}

	WString SocketHttpClientApi::UrlEncodeQuery(const WString& query)
	{
		return HttpUrlEncodeQuery(query);
	}

	WString SocketHttpClientApi::UrlDecodeQuery(const WString& query)
	{
		return HttpUrlDecodeQuery(query);
	}
}
