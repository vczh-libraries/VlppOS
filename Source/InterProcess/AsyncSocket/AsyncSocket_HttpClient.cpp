#include "AsyncSocket_HttpClient.h"

#if defined VCZH_MSVC
#include "AsyncSocket.Windows.h"
#elif defined VCZH_GCC && defined VCZH_APPLE
#include "AsyncSocket.macOS.h"
#elif defined VCZH_GCC
#include "AsyncSocket.Linux.h"
#endif

#include <chrono>
#include <cstring>

namespace vl::inter_process::async_tcp_socket
{
	using namespace vl::collections;

	namespace
	{
		constexpr const wchar_t*		JsonContentType = L"application/json; charset=utf8";
		constexpr vint				HttpRequestMaxAttempts = 3;
		constexpr vint				SendDrainTimeout = 1000;

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

		bool ValidateUtf8(const char* buffer, vint size)
		{
			for (vint i = 0; i < size;)
			{
				auto c = (vuint8_t)buffer[i];
				if (c == 0) return false;
				if (c <= 0x7F)
				{
					i++;
					continue;
				}

				vint count = 0;
				vuint32_t code = 0;
				vuint32_t minimum = 0;
				if (0xC2 <= c && c <= 0xDF)
				{
					count = 2;
					code = c & 0x1F;
					minimum = 0x80;
				}
				else if (0xE0 <= c && c <= 0xEF)
				{
					count = 3;
					code = c & 0x0F;
					minimum = 0x800;
				}
				else if (0xF0 <= c && c <= 0xF4)
				{
					count = 4;
					code = c & 0x07;
					minimum = 0x10000;
				}
				else
				{
					return false;
				}

				if (i > size - count) return false;
				for (vint j = 1; j < count; j++)
				{
					auto continuation = (vuint8_t)buffer[i + j];
					if ((continuation & 0xC0) != 0x80) return false;
					code = (code << 6) | (continuation & 0x3F);
				}
				if (code < minimum || code > 0x10FFFF || (0xD800 <= code && code <= 0xDFFF)) return false;
				i += count;
			}
			return true;
		}

		bool ValidateWString(const WString& text)
		{
			for (vint i = 0; i < text.Length(); i++)
			{
				auto code = (vuint32_t)text[i];
				if (code == 0) return false;
				if constexpr (sizeof(wchar_t) == 2)
				{
					if (0xD800 <= code && code <= 0xDBFF)
					{
						if (i + 1 >= text.Length()) return false;
						auto low = (vuint32_t)text[++i];
						if (low < 0xDC00 || low > 0xDFFF) return false;
					}
					else if (0xDC00 <= code && code <= 0xDFFF)
					{
						return false;
					}
				}
				else
				{
					if (code > 0x10FFFF || (0xD800 <= code && code <= 0xDFFF)) return false;
				}
			}
			return true;
		}

		bool GetUtf8Size(const WString& text, vint& size)
		{
			if (!ValidateWString(text)) return false;
			auto utf8 = wtou8(text);
			size = utf8.Length();
			return true;
		}

		bool DecodeUtf8(const Array<char>& bytes, WString& text)
		{
			if (bytes.Count() == 0)
			{
				text = WString::Empty;
				return true;
			}
			if (!ValidateUtf8(&bytes[0], bytes.Count())) return false;

			Array<char8_t> utf8(bytes.Count());
			for (vint i = 0; i < bytes.Count(); i++)
			{
				utf8[i] = (char8_t)bytes[i];
			}
			text = u8tow(U8String::CopyFrom(&utf8[0], utf8.Count()));
			return ValidateWString(text);
		}

		bool ValidateOriginPath(const WString& path, bool rejectTrailingSlash)
		{
			if (path.Length() == 0 || path[0] != L'/') return false;
			if (rejectTrailingSlash && path[path.Length() - 1] == L'/') return false;

			List<char> decoded;
			for (vint i = 0; i < path.Length(); i++)
			{
				auto c = path[i];
				if (c == L'%')
				{
					if (i + 2 >= path.Length()) return false;
					auto high = HexValue(path[i + 1]);
					auto low = HexValue(path[i + 2]);
					if (high == -1 || low == -1) return false;
					auto value = (char)(high * 16 + low);
					if (value == 0 || value == '/' || value == '\\') return false;
					decoded.Add(value);
					i += 2;
				}
				else
				{
					if (!IsLegalOriginPathCharacter(c)) return false;
					decoded.Add((char)c);
				}
			}
			return decoded.Count() == 0 || ValidateUtf8(&decoded[0], decoded.Count());
		}

		bool ValidateRequestTarget(const WString& target, const wchar_t* method)
		{
			if (!ValidateOriginPath(target, false)) return false;
			vint methodLength = 0;
			while (method[methodLength]) methodLength++;
			return target.Length() <= HttpRequestLineSizeLimit - 10 - methodLength;
		}

		WString DescribeHttpError(const wchar_t* operation, const windows_http::HttpError& error)
		{
			return WString::Unmanaged(operation) + L" failed: " + error.message;
		}

		bool DecodeSuccessfulResponse(
			const windows_http::HttpResponse& response,
			const wchar_t* operation,
			WString& body,
			WString& error
			)
		{
			if (response.statusCode != 200)
			{
				error = WString::Unmanaged(operation) + L" returned status code " + itow(response.statusCode) + L".";
				return false;
			}
			if (response.contentType != JsonContentType)
			{
				error = WString::Unmanaged(operation) + L" did not return the required content type.";
				return false;
			}
			if (!DecodeUtf8(response.body, body))
			{
				error = WString::Unmanaged(operation) + L" returned malformed UTF-8 or an embedded NUL.";
				return false;
			}
			return true;
		}

		BEGIN_GLOBAL_STORAGE_CLASS(SocketHttpClientTestHooks)
			SpinLock						lock;
			Func<void()>					receiveSubmitted;
			Func<void()>					fatalReserved;
			Func<void()>					stopStarted;
		INITIALIZE_GLOBAL_STORAGE_CLASS
		FINALIZE_GLOBAL_STORAGE_CLASS
			SPIN_LOCK(lock)
			{
				receiveSubmitted = {};
				fatalReserved = {};
				stopStarted = {};
			}
		END_GLOBAL_STORAGE_CLASS(SocketHttpClientTestHooks)

		void InvokeReceiveSubmittedForTesting()
		{
			Func<void()> callback;
			auto& hooks = GetSocketHttpClientTestHooks();
			SPIN_LOCK(hooks.lock)
			{
				callback = hooks.receiveSubmitted;
			}
			if (callback)
			{
				try { callback(); }
				catch (...) {}
			}
		}

		void InvokeFatalReservedForTesting()
		{
			Func<void()> callback;
			auto& hooks = GetSocketHttpClientTestHooks();
			SPIN_LOCK(hooks.lock)
			{
				callback = hooks.fatalReserved;
			}
			if (callback)
			{
				try { callback(); }
				catch (...) {}
			}
		}

		void InvokeStopStartedForTesting()
		{
			Func<void()> callback;
			auto& hooks = GetSocketHttpClientTestHooks();
			SPIN_LOCK(hooks.lock)
			{
				callback = hooks.stopStarted;
			}
			if (callback)
			{
				try { callback(); }
				catch (...) {}
			}
		}

		SocketHttpClient::NativeClientFactory CreateDefaultClientFactory()
		{
#if defined VCZH_MSVC
			return [](vint port)->Ptr<IAsyncSocketClient>
			{
				return Ptr<IAsyncSocketClient>(new windows_socket::AsyncSocketClient(port));
			};
#elif defined VCZH_GCC && defined VCZH_APPLE
			return [](vint port)->Ptr<IAsyncSocketClient>
			{
				return Ptr<IAsyncSocketClient>(new macos_socket::AsyncSocketClient(port));
			};
#elif defined VCZH_GCC
			return [](vint port)->Ptr<IAsyncSocketClient>
			{
				return Ptr<IAsyncSocketClient>(new linux_socket::AsyncSocketClient(port));
			};
#else
			return {};
#endif
		}
	}

	void SetSocketHttpClientReceiveSubmittedCallbackForTesting(const Func<void()>& callback)
	{
		auto& hooks = GetSocketHttpClientTestHooks();
		SPIN_LOCK(hooks.lock)
		{
			hooks.receiveSubmitted = callback;
		}
	}

	void ResetSocketHttpClientReceiveSubmittedCallbackForTesting()
	{
		SetSocketHttpClientReceiveSubmittedCallbackForTesting({});
	}

	void SetSocketHttpClientFatalStopCallbacksForTesting(
		const Func<void()>& fatalReserved,
		const Func<void()>& stopStarted
		)
	{
		auto& hooks = GetSocketHttpClientTestHooks();
		SPIN_LOCK(hooks.lock)
		{
			hooks.fatalReserved = fatalReserved;
			hooks.stopStarted = stopStarted;
		}
	}

	void ResetSocketHttpClientFatalStopCallbacksForTesting()
	{
		SetSocketHttpClientFatalStopCallbacksForTesting({}, {});
	}

/***********************************************************************
SocketHttpClient::Impl
***********************************************************************/

	class SocketHttpClient::Impl : public Object
	{
		using QueryResult = Variant<windows_http::HttpResponse, windows_http::HttpError>;

		enum class State
		{
			Ready,
			WaitingForServer,
			Connected,
			Stopping,
		};

		class SendItem : public Object
		{
		public:
			WString						body;
			vint						attempt = 1;
		};

		class QueryWaiter : public Object
		{
		private:
			CriticalSection				lock;
			ConditionVariable			cv;
			Ptr<QueryResult>				result;

		public:
			void Complete(QueryResult value)
			{
				CS_LOCK(lock)
				{
					if (!result)
					{
						result = Ptr(new QueryResult(std::move(value)));
						cv.WakeAllPendings();
					}
				}
			}

			Ptr<QueryResult> Wait()
			{
				CS_LOCK(lock)
				{
					while (!result) cv.SleepWith(lock);
					return result;
				}
				return nullptr;
			}
		};

		struct CallbackFrame
		{
			Ptr<Impl>						state;
			CallbackFrame*				previous = nullptr;
			NetworkProtocolCallbackDomain::CallbackFrame
										domainFrame;

			CallbackFrame(Ptr<Impl> _state)
				: state(_state)
				, previous(currentCallbackFrame)
				, domainFrame(state->callbackDomain)
			{
				currentCallbackFrame = this;
			}

			~CallbackFrame()
			{
				currentCallbackFrame = previous;
				CS_LOCK(state->lockState)
				{
					state->activeCallbacks--;
					state->cvState.WakeAllPendings();
				}
			}
		};

		struct WorkerFrame
		{
			Ptr<Impl>						state;
			WorkerFrame*					previous = nullptr;

			WorkerFrame(Ptr<Impl> _state)
				: state(_state)
				, previous(currentWorkerFrame)
			{
				currentWorkerFrame = this;
			}

			~WorkerFrame()
			{
				currentWorkerFrame = previous;
				CS_LOCK(state->lockState)
				{
					state->activeWorkers--;
					if (state->hardStopping)
					{
						state->sendReconnecting = false;
						state->receiveReconnecting = false;
					}
					state->cvState.WakeAllPendings();
				}
			}
		};

		struct WaitFrame
		{
			Ptr<Impl>						state;
			WaitFrame*					previous = nullptr;

			WaitFrame(Ptr<Impl> _state)
				: state(_state)
				, previous(currentWaitFrame)
			{
				currentWaitFrame = this;
				CS_LOCK(state->lockState)
				{
					state->activeWaits++;
				}
			}

			~WaitFrame()
			{
				currentWaitFrame = previous;
				CS_LOCK(state->lockState)
				{
					state->activeWaits--;
					state->cvState.WakeAllPendings();
				}
			}
		};

		static thread_local CallbackFrame*	currentCallbackFrame;
		static thread_local WorkerFrame*	currentWorkerFrame;
		static thread_local WaitFrame*		currentWaitFrame;

		SocketHttpClient*				owner = nullptr;
		NativeClientFactory				clientFactory;
		vint							port = 0;
		WString							baseUrl;
		WString							authority;
		WString							urlConnect;
		WString							urlRequest;
		WString							urlResponse;
		Ptr<Impl>						selfReference;
		Ptr<NetworkProtocolCallbackDomain>	callbackDomain = Ptr(new NetworkProtocolCallbackDomain);

		CriticalSection					lockState;
		ConditionVariable				cvState;
		State							state = State::Ready;
		INetworkProtocolCallback*		callback = nullptr;
		bool							callbackInstalling = false;
		vint							activeCallbacks = 0;
		vint							activeWorkers = 0;
		vint							activeWaits = 0;
		bool							readingStarted = false;
		bool							receivePollActive = false;
		bool							receiveReconnecting = false;
		bool							sendActive = false;
		bool							sendReconnecting = false;
		bool							stopStarted = false;
		bool							drainSends = false;
		bool							hardStopping = false;
		bool							stopFinished = false;
		bool							fatalStarted = false;
		bool							disconnectedNotified = false;
		bool							disconnectDelivering = false;
		bool							disconnectFinished = false;
		Ptr<SocketHttpClientApi>			sendApi;
		Ptr<SocketHttpClientApi>			receiveApi;
		List<Ptr<SendItem>>				sendQueue;

		Ptr<Impl> RetainSelf()
		{
			CS_LOCK(lockState)
			{
				return selfReference;
			}
			return nullptr;
		}

		vint CurrentCallbackDepth()
		{
			vint depth = 0;
			for (auto frame = currentCallbackFrame; frame; frame = frame->previous)
			{
				if (frame->state.Obj() == this) depth++;
			}
			return depth;
		}

		vint CurrentWorkerDepth()
		{
			vint depth = 0;
			for (auto frame = currentWorkerFrame; frame; frame = frame->previous)
			{
				if (frame->state.Obj() == this) depth++;
			}
			return depth;
		}

		vint CurrentWaitDepth()
		{
			vint depth = 0;
			for (auto frame = currentWaitFrame; frame; frame = frame->previous)
			{
				if (frame->state.Obj() == this) depth++;
			}
			return depth;
		}

		template<typename TCallback>
		void InvokeProtocolCallback(bool allowStopping, TCallback&& invoke)
		{
			INetworkProtocolCallback* installed = nullptr;
			auto callbackDepth = CurrentCallbackDepth();
			lockState.Enter();
			while (callbackInstalling && callbackDepth == 0 && callback)
			{
				cvState.SleepWith(lockState);
			}
			if (callback && (allowStopping || !stopStarted))
			{
				installed = callback;
				activeCallbacks++;
			}
			lockState.Leave();

			if (installed)
			{
				CallbackFrame frame(RetainSelf());
				invoke(installed);
			}
		}

		bool IsStopped()
		{
			CS_LOCK(lockState)
			{
				return stopStarted;
			}
			return true;
		}

		bool CanReceiveUnsafe()
		{
			return state == State::Connected && readingStarted && !stopStarted && !hardStopping;
		}

		bool CanSendUnsafe()
		{
			return !hardStopping && (state == State::Connected || drainSends);
		}

		void StopApiNoThrow(Ptr<SocketHttpClientApi> api)
		{
			if (!api) return;
			try
			{
				api->Stop();
			}
			catch (...)
			{
			}
		}

		Ptr<SocketHttpClientApi> CreateApi()
		{
			auto nativeClient = clientFactory(port);
			CHECK_ERROR(nativeClient, L"SocketHttpClient::NativeClientFactory returned null.");
			return Ptr(new SocketHttpClientApi(nativeClient, authority));
		}

		bool WaitApiForServer(Ptr<SocketHttpClientApi> api)
		{
			api->WaitForServer();
			return api->GetStatus() == ClientStatus::Connected;
		}

		void ReportLocalError(const WString& error, bool fatal)
		{
			InvokeProtocolCallback(fatal, [&](INetworkProtocolCallback* installed)
			{
				installed->OnLocalError(error, fatal);
			});
		}

		void NotifyDisconnected()
		{
			auto callbackDepth = CurrentCallbackDepth();
			lockState.Enter();
			if (!disconnectedNotified)
			{
				disconnectedNotified = true;
				cvState.WakeAllPendings();
			}
			if (disconnectFinished)
			{
				lockState.Leave();
				return;
			}
			if (disconnectDelivering)
			{
				if (callbackDepth == 0)
				{
					while (!disconnectFinished) cvState.SleepWith(lockState);
				}
				lockState.Leave();
				return;
			}
			if (callbackDepth == 0)
			{
				while (activeCallbacks > 0 && !disconnectDelivering && !disconnectFinished)
				{
					cvState.SleepWith(lockState);
				}
				if (disconnectFinished)
				{
					lockState.Leave();
					return;
				}
			}
			disconnectDelivering = true;
			while (activeCallbacks > callbackDepth) cvState.SleepWith(lockState);
			lockState.Leave();

			try
			{
				InvokeProtocolCallback(true, [](INetworkProtocolCallback* installed)
				{
					installed->OnDisconnected();
				});
			}
			catch (...)
			{
				CS_LOCK(lockState)
				{
					callback = nullptr;
					disconnectFinished = true;
					cvState.WakeAllPendings();
				}
				throw;
			}

			CS_LOCK(lockState)
			{
				callback = nullptr;
				disconnectFinished = true;
				cvState.WakeAllPendings();
			}
		}

		void ReportFatalError(const WString& error)
		{
			INetworkProtocolCallback* installed = nullptr;
			auto callbackDepth = CurrentCallbackDepth();
			bool report = false;
			lockState.Enter();
			while (callbackInstalling && callbackDepth == 0 && callback && !stopStarted)
			{
				cvState.SleepWith(lockState);
			}
			if (!fatalStarted && !stopStarted)
			{
				fatalStarted = true;
				report = true;
				if (callback)
				{
					installed = callback;
					activeCallbacks++;
				}
			}
			lockState.Leave();
			if (!report) return;
			InvokeFatalReservedForTesting();

			try
			{
				if (installed)
				{
					CallbackFrame frame(RetainSelf());
					installed->OnLocalError(error, true);
				}
			}
			catch (...)
			{
				Stop(true);
				throw;
			}
			Stop(true);
		}

		bool HandleConnectFailure(WString error, vint& attempt)
		{
			if (IsStopped()) return false;
			if (attempt >= HttpRequestMaxAttempts)
			{
				ReportFatalError(error);
				return false;
			}
			ReportLocalError(error, false);
			attempt++;
			return !IsStopped();
		}

		Ptr<QueryResult> QueryConnect(Ptr<SocketHttpClientApi> api)
		{
			windows_http::HttpRequest request;
			request.method = L"GET";
			request.query = urlConnect;
			request.acceptTypes.Add(JsonContentType);

			auto waiter = Ptr(new QueryWaiter);
			api->HttpQuery(request, [waiter](QueryResult result)
			{
				waiter->Complete(std::move(result));
			});
			return waiter->Wait();
		}

		bool ValidateConnectResponse(
			const windows_http::HttpResponse& response,
			WString& requestUrl,
			WString& responseUrl,
			WString& error
			)
		{
			WString body;
			if (!DecodeSuccessfulResponse(response, L"/Connect", body, error)) return false;
			if (body.Length() == 0)
			{
				error = L"/Connect returned an empty body.";
				return false;
			}

			auto separator = body.IndexOf(L';');
			if (separator <= 0 || separator + 1 >= body.Length() || body.Right(body.Length() - separator - 1).IndexOf(L';') != -1)
			{
				error = L"/Connect did not return exactly two paths.";
				return false;
			}
			auto requestPath = body.Left(separator);
			auto responsePath = body.Right(body.Length() - separator - 1);
			if (!ValidateOriginPath(requestPath, false) || !ValidateOriginPath(responsePath, false))
			{
				error = L"/Connect returned an illegal path.";
				return false;
			}

			requestUrl = baseUrl + requestPath;
			responseUrl = baseUrl + responsePath;
			if (!ValidateRequestTarget(requestUrl, L"POST") || !ValidateRequestTarget(responseUrl, L"POST"))
			{
				error = L"/Connect returned a path exceeding the HTTP request-target contract.";
				return false;
			}
			return true;
		}

		bool PublishApi(Ptr<SocketHttpClientApi> api, bool receive)
		{
			CS_LOCK(lockState)
			{
				if (stopStarted || hardStopping) return false;
				if (receive)
				{
					receiveApi = api;
				}
				else
				{
					sendApi = api;
				}
				return true;
			}
			return false;
		}

		void ClearApi(Ptr<SocketHttpClientApi> api, bool receive)
		{
			CS_LOCK(lockState)
			{
				if (receive)
				{
					if (receiveApi == api) receiveApi = nullptr;
				}
				else
				{
					if (sendApi == api) sendApi = nullptr;
				}
				cvState.WakeAllPendings();
			}
		}

		void SubmitReceivePoll(Ptr<SocketHttpClientApi> api)
		{
			auto self = RetainSelf();
			if (!self) return;
			windows_http::HttpRequest request;
			request.method = L"POST";
			request.query = urlRequest;
			request.acceptTypes.Add(JsonContentType);
			request.extraHeaders.Add(L"Content-Length", L"0");
			request.receiveTimeout = 0;

			try
			{
				api->HttpQuery(request, [self, api](QueryResult result)
				{
					self->OnReceiveCompleted(api, std::move(result));
				});
				InvokeReceiveSubmittedForTesting();
			}
			catch (...)
			{
				HandleReceiveTransportFailure(api);
			}
		}

		bool ReserveReceivePoll(Ptr<SocketHttpClientApi> api)
		{
			CS_LOCK(lockState)
			{
				if (!CanReceiveUnsafe() || receiveReconnecting || receivePollActive || receiveApi != api) return false;
				receivePollActive = true;
				return true;
			}
			return false;
		}

		void HandleReceiveTransportFailure(Ptr<SocketHttpClientApi> api)
		{
			bool schedule = false;
			CS_LOCK(lockState)
			{
				if (receiveApi == api && CanReceiveUnsafe() && !receiveReconnecting)
				{
					receivePollActive = false;
					receiveReconnecting = true;
					activeWorkers++;
					schedule = true;
				}
			}
			if (!schedule) return;

			auto self = RetainSelf();
			bool queued = false;
			try
			{
				auto worker = Func<void()>([self, api]()
				{
					WorkerFrame frame(self);
					try
					{
						self->RunReceiveReplacement(api);
					}
					catch (...)
					{
						try
						{
							self->ReportFatalError(L"/Request replacement worker failed unexpectedly.");
						}
						catch (...)
						{
							self->Stop();
						}
					}
				});
				if (self)
				{
					try
					{
						queued = ThreadPoolLite::Queue(worker);
					}
					catch (...)
					{
					}
					if (!queued)
					{
						try
						{
							queued = Thread::CreateAndStart(worker) != nullptr;
						}
						catch (...)
						{
						}
					}
				}
			}
			catch (...)
			{
			}
			if (!queued)
			{
				CS_LOCK(lockState)
				{
					activeWorkers--;
					receiveReconnecting = false;
					cvState.WakeAllPendings();
				}
				ReportFatalError(L"/Request could not queue its replacement worker.");
			}
		}

		void RunReceiveReplacement(Ptr<SocketHttpClientApi> deadApi)
		{
			StopApiNoThrow(deadApi);
			ClearApi(deadApi, true);

			while (true)
			{
				CS_LOCK(lockState)
				{
					if (!CanReceiveUnsafe() || !receiveReconnecting) return;
				}

				Ptr<SocketHttpClientApi> api;
				try
				{
					api = CreateApi();
				}
				catch (...)
				{
					continue;
				}
				if (!PublishApi(api, true))
				{
					StopApiNoThrow(api);
					return;
				}

				bool connected = false;
				try
				{
					connected = WaitApiForServer(api);
				}
				catch (...)
				{
				}
				if (!connected)
				{
					StopApiNoThrow(api);
					ClearApi(api, true);
					continue;
				}

				bool submit = false;
				CS_LOCK(lockState)
				{
					if (receiveApi == api && CanReceiveUnsafe() && receiveReconnecting)
					{
						receiveReconnecting = false;
						receivePollActive = true;
						submit = true;
					}
				}
				if (!submit)
				{
					StopApiNoThrow(api);
					return;
				}
				SubmitReceivePoll(api);
				return;
			}
		}

		void OnReceiveCompleted(Ptr<SocketHttpClientApi> api, QueryResult result)
		{
			bool current = false;
			CS_LOCK(lockState)
			{
				if (receiveApi == api && receivePollActive)
				{
					receivePollActive = false;
					current = true;
				}
			}
			if (!current) return;

			if (result.TryGet<windows_http::HttpError>())
			{
				HandleReceiveTransportFailure(api);
				return;
			}

			WString body;
			WString error;
			auto valid = DecodeSuccessfulResponse(result.Get<windows_http::HttpResponse>(), L"/Request", body, error);
			if (ReserveReceivePoll(api))
			{
				// SocketHttpClientApi starts this replacement from inside its response
				// callback before any user callback below can create a receive gap.
				SubmitReceivePoll(api);
			}
			if (valid && body.Length() > 0)
			{
				InvokeProtocolCallback(false, [&](INetworkProtocolCallback* installed)
				{
					installed->OnReadString(body);
				});
			}
		}

		bool IsCurrentSendUnsafe(Ptr<SocketHttpClientApi> api, Ptr<SendItem> item)
		{
			return sendQueue.Count() > 0 && sendQueue[0] == item && sendApi == api;
		}

		void SubmitSend(Ptr<SocketHttpClientApi> api, Ptr<SendItem> item)
		{
			bool submit = false;
			CS_LOCK(lockState)
			{
				submit = CanSendUnsafe() && sendActive && IsCurrentSendUnsafe(api, item);
			}
			if (!submit) return;

			windows_http::HttpRequest request;
			request.method = L"POST";
			request.query = urlResponse;
			request.acceptTypes.Add(JsonContentType);
			request.contentType = JsonContentType;
			request.SetBodyUtf8(item->body);

			auto self = RetainSelf();
			try
			{
				api->HttpQuery(request, [self, api, item](QueryResult result)
				{
					self->OnSendCompleted(api, item, std::move(result));
				});
			}
			catch (...)
			{
				HandleSendFailure(api, item, L"/Response could not submit the HTTP exchange.", true);
			}
		}

		void QueueSendReplacement(Ptr<SocketHttpClientApi> deadApi, Ptr<SendItem> item)
		{
			bool schedule = false;
			CS_LOCK(lockState)
			{
				if (CanSendUnsafe() && sendReconnecting && IsCurrentSendUnsafe(deadApi, item))
				{
					activeWorkers++;
					schedule = true;
				}
				else
				{
					sendReconnecting = false;
				}
			}
			if (!schedule) return;

			auto self = RetainSelf();
			bool queued = false;
			try
			{
				auto worker = Func<void()>([self, deadApi, item]()
				{
					WorkerFrame frame(self);
					try
					{
						self->RunSendReplacement(deadApi, item);
					}
					catch (...)
					{
						try
						{
							self->ReportFatalError(L"/Response replacement worker failed unexpectedly.");
						}
						catch (...)
						{
							self->Stop();
						}
					}
				});
				if (self)
				{
					try
					{
						queued = ThreadPoolLite::Queue(worker);
					}
					catch (...)
					{
					}
					if (!queued)
					{
						try
						{
							queued = Thread::CreateAndStart(worker) != nullptr;
						}
						catch (...)
						{
						}
					}
				}
			}
			catch (...)
			{
			}
			if (!queued)
			{
				CS_LOCK(lockState)
				{
					activeWorkers--;
					sendReconnecting = false;
					cvState.WakeAllPendings();
				}
				ReportFatalError(L"/Response could not queue its replacement worker.");
			}
		}

		bool HandleSendPreparationFailure(Ptr<SendItem> item, const WString& error)
		{
			bool fatal = false;
			CS_LOCK(lockState)
			{
				if (!CanSendUnsafe() || !sendReconnecting || sendQueue.Count() == 0 || sendQueue[0] != item) return false;
				fatal = item->attempt >= HttpRequestMaxAttempts;
				if (!fatal) item->attempt++;
			}
			if (fatal)
			{
				ReportFatalError(error);
				return false;
			}
			ReportLocalError(error, false);
			CS_LOCK(lockState)
			{
				return CanSendUnsafe() && sendReconnecting && sendQueue.Count() > 0 && sendQueue[0] == item;
			}
			return false;
		}

		void RunSendReplacement(Ptr<SocketHttpClientApi> deadApi, Ptr<SendItem> item)
		{
			StopApiNoThrow(deadApi);
			ClearApi(deadApi, false);

			while (true)
			{
				CS_LOCK(lockState)
				{
					if (!CanSendUnsafe() || !sendReconnecting || sendQueue.Count() == 0 || sendQueue[0] != item) return;
				}

				Ptr<SocketHttpClientApi> api;
				try
				{
					api = CreateApi();
				}
				catch (...)
				{
					if (!HandleSendPreparationFailure(item, L"/Response could not create a replacement HTTP client.")) return;
					continue;
				}
				if (!PublishApi(api, false))
				{
					StopApiNoThrow(api);
					return;
				}

				bool connected = false;
				try
				{
					connected = WaitApiForServer(api);
				}
				catch (...)
				{
				}
				if (!connected)
				{
					StopApiNoThrow(api);
					ClearApi(api, false);
					if (!HandleSendPreparationFailure(item, L"/Response replacement failed to connect.")) return;
					continue;
				}

				bool submit = false;
				CS_LOCK(lockState)
				{
					if (sendApi == api && CanSendUnsafe() && sendReconnecting && sendQueue.Count() > 0 && sendQueue[0] == item)
					{
						sendReconnecting = false;
						sendActive = true;
						submit = true;
					}
				}
				if (!submit)
				{
					StopApiNoThrow(api);
					return;
				}
				SubmitSend(api, item);
				return;
			}
		}

		void HandleSendFailure(Ptr<SocketHttpClientApi> api, Ptr<SendItem> item, WString error, bool transportFailure)
		{
			bool fatal = false;
			bool retryHealthy = false;
			bool replace = false;
			CS_LOCK(lockState)
			{
				if (!sendActive || !CanSendUnsafe() || !IsCurrentSendUnsafe(api, item)) return;
				sendActive = false;
				fatal = item->attempt >= HttpRequestMaxAttempts;
				if (!fatal)
				{
					item->attempt++;
					if (transportFailure)
					{
						sendReconnecting = true;
						replace = true;
					}
					else
					{
						sendActive = true;
						retryHealthy = true;
					}
				}
				cvState.WakeAllPendings();
			}

			if (fatal)
			{
				ReportFatalError(error);
				return;
			}

			try
			{
				ReportLocalError(error, false);
			}
			catch (...)
			{
				if (replace) QueueSendReplacement(api, item);
				if (retryHealthy) SubmitSend(api, item);
				throw;
			}
			if (replace) QueueSendReplacement(api, item);
			if (retryHealthy) SubmitSend(api, item);
		}

		void OnSendCompleted(Ptr<SocketHttpClientApi> api, Ptr<SendItem> item, QueryResult result)
		{
			if (auto httpError = result.TryGet<windows_http::HttpError>())
			{
				HandleSendFailure(api, item, DescribeHttpError(L"/Response", *httpError), true);
				return;
			}

			WString body;
			WString error;
			if (!DecodeSuccessfulResponse(result.Get<windows_http::HttpResponse>(), L"/Response", body, error))
			{
				HandleSendFailure(api, item, error, false);
				return;
			}

			Ptr<SendItem> next;
			CS_LOCK(lockState)
			{
				if (!sendActive || !IsCurrentSendUnsafe(api, item)) return;
				sendActive = false;
				sendQueue.RemoveAt(0);
				if (CanSendUnsafe() && sendQueue.Count() > 0)
				{
					next = sendQueue[0];
					sendActive = true;
				}
				cvState.WakeAllPendings();
			}

			// Preserve FIFO ownership by submitting the next accepted send before
			// delivering a piggybacked message to callback-reentrant application code.
			if (next) SubmitSend(api, next);
			if (body.Length() > 0)
			{
				InvokeProtocolCallback(false, [&](INetworkProtocolCallback* installed)
				{
					installed->OnReadString(body);
				});
			}
		}

	public:
		Impl(SocketHttpClient* _owner, const WString& _baseUrl, vint _port, NativeClientFactory _clientFactory)
			: owner(_owner)
			, clientFactory(_clientFactory)
			, port(_port)
			, baseUrl(_baseUrl)
			, authority(WString::Unmanaged(L"localhost:") + itow(_port))
			, urlConnect(_baseUrl + HttpServerUrl_Connect)
		{
			CHECK_ERROR(owner, L"SocketHttpClient requires an owning adapter.");
			CHECK_ERROR(clientFactory, L"SocketHttpClient requires a native-client factory.");
			CHECK_ERROR(1 <= port && port <= 65535, L"SocketHttpClient requires a port in 1..65535.");
			CHECK_ERROR(baseUrl == WString::Empty || ValidateOriginPath(baseUrl, true), L"SocketHttpClient requires an empty or legal origin-form base URL without a trailing slash.");
			CHECK_ERROR(ValidateRequestTarget(urlConnect, L"GET"), L"SocketHttpClient base URL makes /Connect exceed the HTTP request-line limit.");
		}

		void Initialize(Ptr<Impl> self)
		{
			CS_LOCK(lockState)
			{
				selfReference = self;
			}
		}

		void ReleaseSelf()
		{
			CS_LOCK(lockState)
			{
				owner = nullptr;
				selfReference = nullptr;
			}
		}

		INetworkProtocolConnection* GetConnection()
		{
			return owner;
		}

		void WaitForServer()
		{
			auto self = RetainSelf();
			CS_LOCK(lockState)
			{
				CHECK_ERROR(state == State::Ready && !stopStarted, L"SocketHttpClient::WaitForServer can only be called once.");
				state = State::WaitingForServer;
			}
			WaitFrame waitFrame(self);

			vint attempt = 1;
			Ptr<SocketHttpClientApi> api;
			while (!IsStopped())
			{
				if (!api)
				{
					try
					{
						api = CreateApi();
					}
					catch (...)
					{
						if (!HandleConnectFailure(L"/Connect could not create its HTTP client.", attempt)) return;
						continue;
					}
					if (!PublishApi(api, false))
					{
						StopApiNoThrow(api);
						return;
					}
					bool connected = false;
					try
					{
						connected = WaitApiForServer(api);
					}
					catch (...)
					{
					}
					if (!connected)
					{
						StopApiNoThrow(api);
						ClearApi(api, false);
						api = nullptr;
						if (!HandleConnectFailure(L"/Connect native connection failed.", attempt)) return;
						continue;
					}
				}

				Ptr<QueryResult> result;
				try
				{
					result = QueryConnect(api);
				}
				catch (...)
				{
					StopApiNoThrow(api);
					ClearApi(api, false);
					api = nullptr;
					if (!HandleConnectFailure(L"/Connect could not submit its HTTP exchange.", attempt)) return;
					continue;
				}
				if (IsStopped()) return;

				if (auto httpError = result->TryGet<windows_http::HttpError>())
				{
					auto error = DescribeHttpError(L"/Connect", *httpError);
					StopApiNoThrow(api);
					ClearApi(api, false);
					api = nullptr;
					if (!HandleConnectFailure(error, attempt)) return;
					continue;
				}

				WString requestUrl;
				WString responseUrl;
				WString error;
				if (!ValidateConnectResponse(result->Get<windows_http::HttpResponse>(), requestUrl, responseUrl, error))
				{
					if (!HandleConnectFailure(error, attempt)) return;
					continue;
				}

				CS_LOCK(lockState)
				{
					if (stopStarted) return;
					urlRequest = requestUrl;
					urlResponse = responseUrl;
				}
				break;
			}
			if (IsStopped()) return;

			// The logical token is already fixed. Failures in this second physical
			// bootstrap are silent and never repeat /Connect.
			while (!IsStopped())
			{
				Ptr<SocketHttpClientApi> apiReceive;
				try
				{
					apiReceive = CreateApi();
				}
				catch (...)
				{
					continue;
				}
				if (!PublishApi(apiReceive, true))
				{
					StopApiNoThrow(apiReceive);
					return;
				}
				bool connected = false;
				try
				{
					connected = WaitApiForServer(apiReceive);
				}
				catch (...)
				{
				}
				if (!connected)
				{
					StopApiNoThrow(apiReceive);
					ClearApi(apiReceive, true);
					continue;
				}

				bool publishConnected = false;
				CS_LOCK(lockState)
				{
					if (!stopStarted && receiveApi == apiReceive)
					{
						state = State::Connected;
						publishConnected = true;
					}
				}
				if (!publishConnected)
				{
					StopApiNoThrow(apiReceive);
					return;
				}
				InvokeProtocolCallback(false, [](INetworkProtocolCallback* installed)
				{
					installed->OnConnected();
				});
				return;
			}
		}

		ClientStatus GetStatus()
		{
			CS_LOCK(lockState)
			{
				switch (state)
				{
				case State::Ready:
					return ClientStatus::Ready;
				case State::WaitingForServer:
					return ClientStatus::WaitingForServer;
				case State::Connected:
					return ClientStatus::Connected;
				default:
					return ClientStatus::Disconnected;
				}
			}
			return ClientStatus::Disconnected;
		}

		void InstallCallback(INetworkProtocolCallback* value)
		{
			if (!value)
			{
				auto callbackDepth = CurrentCallbackDepth();
				bool uninstallOwner = false;
				CS_LOCK(lockState)
				{
					uninstallOwner = callback != nullptr;
					callback = nullptr;
					while ((callbackDepth == 0 || uninstallOwner) && activeCallbacks > callbackDepth)
					{
						cvState.SleepWith(lockState);
					}
				}
				return;
			}

			bool canInstall = false;
			CS_LOCK(lockState)
			{
				if (!callback && !callbackInstalling && !stopStarted)
				{
					callback = value;
					callbackInstalling = true;
					activeCallbacks++;
					canInstall = true;
				}
			}
			CHECK_ERROR(canInstall, L"SocketHttpClient::InstallCallback cannot replace a callback or install one on a stopped client.");

			CallbackFrame frame(RetainSelf());
			try
			{
				value->OnInstalled(owner);
			}
			catch (...)
			{
				CS_LOCK(lockState)
				{
					if (callback == value) callback = nullptr;
					callbackInstalling = false;
					cvState.WakeAllPendings();
				}
				throw;
			}
			CS_LOCK(lockState)
			{
				callbackInstalling = false;
				cvState.WakeAllPendings();
			}
		}

		void BeginReadingLoopUnsafe()
		{
			Ptr<SocketHttpClientApi> api;
			CS_LOCK(lockState)
			{
				CHECK_ERROR(state == State::Connected && !stopStarted, L"SocketHttpClient::BeginReadingLoopUnsafe requires a connected client.");
				CHECK_ERROR(!readingStarted, L"SocketHttpClient::BeginReadingLoopUnsafe can only be called once.");
				readingStarted = true;
				api = receiveApi;
				CHECK_ERROR(api, L"SocketHttpClient has no receive API after connecting.");
				receivePollActive = true;
			}
			SubmitReceivePoll(api);
		}

		void SendString(const WString& str)
		{
			vint utf8Size = 0;
			CHECK_ERROR(str.Length() > 0, L"SocketHttpClient::SendString does not accept an empty string.");
			CHECK_ERROR(GetUtf8Size(str, utf8Size), L"SocketHttpClient::SendString requires valid Unicode without embedded NUL characters.");
			CHECK_ERROR(utf8Size <= HttpBodySizeLimit, L"SocketHttpClient::SendString exceeds the HTTP body size limit.");

			auto item = Ptr(new SendItem);
			item->body = str;
			Ptr<SocketHttpClientApi> api;
			bool submit = false;
			CS_LOCK(lockState)
			{
				CHECK_ERROR(state == State::Connected && !stopStarted, L"SocketHttpClient::SendString requires a connected client.");
				sendQueue.Add(item);
				if (!sendActive && !sendReconnecting)
				{
					api = sendApi;
					CHECK_ERROR(api, L"SocketHttpClient has no send API after connecting.");
					sendActive = true;
					submit = true;
				}
			}
			if (submit) SubmitSend(api, item);
		}

		void Stop(bool internalFollower = false)
		{
			auto self = RetainSelf();
			auto callbackDepth = CurrentCallbackDepth();
			auto workerDepth = CurrentWorkerDepth();
			auto waitDepth = CurrentWaitDepth();
			auto nested = internalFollower || callbackDepth > 0 || workerDepth > 0 || waitDepth > 0;
			bool executeStop = false;
			Ptr<SocketHttpClientApi> cancellingReceive;

			lockState.Enter();
			if (stopFinished)
			{
				while (activeCallbacks > callbackDepth || activeWorkers > workerDepth || activeWaits > waitDepth)
				{
					cvState.SleepWith(lockState);
				}
				lockState.Leave();
				return;
			}
			if (!stopStarted)
			{
				stopStarted = true;
				state = State::Stopping;
				drainSends = !fatalStarted && sendQueue.Count() > 0;
				cancellingReceive = receiveApi;
				executeStop = true;
			}
			else if (nested)
			{
				lockState.Leave();
				return;
			}
			else
			{
				while (!stopFinished) cvState.SleepWith(lockState);
				while (activeCallbacks > 0 || activeWorkers > 0 || activeWaits > 0) cvState.SleepWith(lockState);
				lockState.Leave();
				return;
			}
			lockState.Leave();

			if (!executeStop) return;
			InvokeStopStartedForTesting();
			// The infinite receive exchange is always cancelled before the bounded
			// opportunity given to already accepted send-lane messages.
			StopApiNoThrow(cancellingReceive);

			lockState.Enter();
			if (drainSends)
			{
				auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(SendDrainTimeout);
				while (sendQueue.Count() > 0)
				{
					auto now = std::chrono::steady_clock::now();
					if (now >= deadline) break;
					auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
					cvState.SleepWithForTime(lockState, (vint)remaining);
				}
			}
			drainSends = false;
			hardStopping = true;
			sendQueue.Clear();
			sendActive = false;
			receivePollActive = false;
			auto stoppingSend = sendApi;
			auto stoppingReceive = receiveApi;
			lockState.Leave();

			StopApiNoThrow(stoppingReceive);
			StopApiNoThrow(stoppingSend);

			lockState.Enter();
			while (activeWorkers > workerDepth || activeWaits > waitDepth)
			{
				cvState.SleepWith(lockState);
			}
			lockState.Leave();

			try
			{
				NotifyDisconnected();
			}
			catch (...)
			{
				CS_LOCK(lockState)
				{
					while (activeCallbacks > callbackDepth) cvState.SleepWith(lockState);
					stopFinished = true;
					cvState.WakeAllPendings();
				}
				throw;
			}
			CS_LOCK(lockState)
			{
				while (activeCallbacks > callbackDepth) cvState.SleepWith(lockState);
				stopFinished = true;
				cvState.WakeAllPendings();
			}
		}
	};

	thread_local SocketHttpClient::Impl::CallbackFrame* SocketHttpClient::Impl::currentCallbackFrame = nullptr;
	thread_local SocketHttpClient::Impl::WorkerFrame* SocketHttpClient::Impl::currentWorkerFrame = nullptr;
	thread_local SocketHttpClient::Impl::WaitFrame* SocketHttpClient::Impl::currentWaitFrame = nullptr;

/***********************************************************************
SocketHttpClient
***********************************************************************/

	SocketHttpClient::SocketHttpClient(const WString& baseUrl, vint port)
		: SocketHttpClient(baseUrl, port, CreateDefaultClientFactory())
	{
	}

	SocketHttpClient::SocketHttpClient(const WString& baseUrl, vint port, NativeClientFactory clientFactory)
	{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::SocketHttpClient::SocketHttpClient(const WString&, vint, NativeClientFactory)#"
		CHECK_ERROR(clientFactory, ERROR_MESSAGE_PREFIX L"A native-client factory is required on this platform.");
		auto created = Ptr(new Impl(this, baseUrl, port, clientFactory));
		created->Initialize(created);
		impl = created;
#undef ERROR_MESSAGE_PREFIX
	}

	SocketHttpClient::~SocketHttpClient()
	{
		try
		{
			impl->Stop();
		}
		catch (...)
		{
		}
		impl->ReleaseSelf();
	}

	INetworkProtocolConnection* SocketHttpClient::GetConnection()
	{
		return impl->GetConnection();
	}

	void SocketHttpClient::WaitForServer()
	{
		impl->WaitForServer();
	}

	ClientStatus SocketHttpClient::GetStatus()
	{
		return impl->GetStatus();
	}

	void SocketHttpClient::InstallCallback(INetworkProtocolCallback* callback)
	{
		impl->InstallCallback(callback);
	}

	void SocketHttpClient::BeginReadingLoopUnsafe()
	{
		impl->BeginReadingLoopUnsafe();
	}

	void SocketHttpClient::SendString(const WString& str)
	{
		impl->SendString(str);
	}

	void SocketHttpClient::Stop()
	{
		impl->Stop();
	}
}
