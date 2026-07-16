#include "../../Source/InterProcess/AsyncSocket/AsyncSocket_HttpServerApi.h"
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket_HttpClientApi.h"

#if defined VCZH_MSVC
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.Windows.h"
#include "../../Source/InterProcess/Windows/HttpClientApi.Windows.h"
#include "../../Source/InterProcess/Windows/HttpServerApi.Windows.h"
#elif defined VCZH_GCC && defined VCZH_APPLE
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.macOS.h"
#elif defined VCZH_GCC
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.Linux.h"
#endif

using namespace vl;
using namespace vl::collections;
using namespace vl::inter_process;
using namespace vl::inter_process::async_tcp_socket;

namespace vl::inter_process::async_tcp_socket
{
	extern void SetSocketHttpServerListenerFactoryForTesting(const Func<Ptr<IAsyncSocketServer>(vint)>& factory);
	extern void ResetSocketHttpServerListenerFactoryForTesting();
	extern void SetSocketHttpServerTimeoutControllerFactoryForTesting(const Func<Ptr<IHttpRequestTimeoutController>()>& factory);
	extern void ResetSocketHttpServerTimeoutControllerFactoryForTesting();
}

namespace mini_http_api_test
{
	constexpr vint ConnectTimeout = 10000;
	constexpr vint TransferTimeout = 30000;

	class SignalEventOnExit
	{
	private:
		EventObject* eventObject = nullptr;

	public:
		SignalEventOnExit(EventObject& value)
			: eventObject(&value)
		{
		}

		~SignalEventOnExit()
		{
			eventObject->Signal();
		}
	};

	struct QueryRecord
	{
		bool isError = false;
		vint statusCode = 0;
		WString body;
		WString contentType;
		WString cookie;
		vuint32_t errorCode = 0;
		WString operation;
		WString message;
	};

	class TestState : public Object
	{
	public:
		EventObject eventConnected;
		EventObject eventDone;
		EventObject eventContext;
		EventObject eventCompletion;

		// covers failure
		SpinLock lockFailure;
		WString failure;

		// covers records and targets
		CriticalSection lockRecords;
		List<QueryRecord> records;
		List<WString> targets;
		vint expectedRecords = 0;

		atomic_vint serverRequests = 0;
		atomic_vint successfulCompletions = 0;
		atomic_vint failedCompletions = 0;
		atomic_vint stoppingCallbacks = 0;
		atomic_vint callbacksAfterStop = 0;
		atomic_vint stopReturned = 0;
		atomic_vint listenerFactoryCalls = 0;

		TestState(vint _expectedRecords = 0)
			: expectedRecords(_expectedRecords)
		{
			CHECK_ERROR(eventConnected.CreateManualUnsignal(false), L"MiniHttp test failed to create eventConnected.");
			CHECK_ERROR(eventDone.CreateManualUnsignal(false), L"MiniHttp test failed to create eventDone.");
			CHECK_ERROR(eventContext.CreateManualUnsignal(false), L"MiniHttp test failed to create eventContext.");
			CHECK_ERROR(eventCompletion.CreateManualUnsignal(false), L"MiniHttp test failed to create eventCompletion.");
		}

		void Fail(const WString& message)
		{
			SPIN_LOCK(lockFailure)
			{
				if (failure == WString::Empty)
				{
					failure = message;
				}
			}
		}

		void Expect(bool condition, const WString& message)
		{
			if (!condition)
			{
				Fail(message);
			}
		}

		WString GetFailure()
		{
			WString result;
			SPIN_LOCK(lockFailure)
			{
				result = failure;
			}
			return result;
		}

		void AddTarget(const WString& target)
		{
			CS_LOCK(lockRecords)
			{
				targets.Add(target);
			}
		}

		void AddResult(Variant<windows_http::HttpResponse, windows_http::HttpError> result)
		{
			QueryRecord record;
			if (auto response = result.TryGet<windows_http::HttpResponse>())
			{
				record.statusCode = response->statusCode;
				record.body = response->GetBodyUtf8();
				record.contentType = response->contentType;
				record.cookie = response->cookie;
			}
			else
			{
				auto&& error = result.Get<windows_http::HttpError>();
				record.isError = true;
				record.errorCode = error.errorCode;
				record.operation = error.operation;
				record.message = error.message;
			}

			bool done = false;
			CS_LOCK(lockRecords)
			{
				records.Add(std::move(record));
				done = expectedRecords > 0 && records.Count() >= expectedRecords;
			}
			if (stopReturned)
			{
				callbacksAfterStop++;
			}
			if (done)
			{
				eventDone.Signal();
			}
		}

		List<QueryRecord> CopyRecords()
		{
			List<QueryRecord> result;
			CS_LOCK(lockRecords)
			{
				for (auto&& record : records)
				{
					result.Add(record);
				}
			}
			return result;
		}

		List<WString> CopyTargets()
		{
			List<WString> result;
			CS_LOCK(lockRecords)
			{
				for (auto&& target : targets)
				{
					result.Add(target);
				}
			}
			return result;
		}
	};

	void RecordCurrentException(TestState& state, const wchar_t* operation)
	{
		try
		{
			throw;
		}
		catch (const Error& error)
		{
			state.Fail(WString(operation) + L": " + error.Description());
		}
		catch (const Exception& exception)
		{
			state.Fail(WString(operation) + L": " + exception.Message());
		}
		catch (...)
		{
			state.Fail(WString(operation) + L": unknown exception.");
		}
	}

	void AssertState(TestState& state)
	{
		auto failure = state.GetFailure();
		if (failure != WString::Empty)
		{
			TEST_PRINT(failure);
		}
		TEST_ASSERT(failure == WString::Empty);
	}

	class ManualServerTimeoutController : public Object, public virtual IHttpRequestTimeoutController
	{
	private:
		static thread_local ManualServerTimeoutController*
										currentFiring;
		CriticalSection					lockState;
		ConditionVariable				cvState;
		Func<void()>						callback;
		bool							firing = false;
		EventObject						eventArmed;

		void FinishFiring()
		{
			CS_LOCK(lockState)
			{
				firing = false;
				cvState.WakeAllPendings();
			}
		}

	public:
		atomic_vint						armCount = 0;
		atomic_vint						cancelCount = 0;
		atomic_vint						lastArmDuration = 0;

		ManualServerTimeoutController()
		{
			CHECK_ERROR(eventArmed.CreateManualUnsignal(false), L"MiniHttp test failed to create the timeout armed event.");
		}

		~ManualServerTimeoutController()
		{
			CancelAndWait();
		}

		void Arm(vint milliseconds, const Func<void()>& value) override
		{
			CS_LOCK(lockState)
			{
				CHECK_ERROR(!callback && !firing, L"The manual MiniHttp timeout controller is already armed.");
				callback = value;
				armCount++;
				lastArmDuration = milliseconds;
			}
			eventArmed.Signal();
		}

		void Refresh() override
		{
		}

		void CancelAndWait() override
		{
			cancelCount++;
			auto nested = currentFiring == this;
			lockState.Enter();
			callback = {};
			while (firing && !nested)
			{
				cvState.SleepWith(lockState);
			}
			lockState.Leave();
		}

		bool WaitUntilArmed(vint milliseconds)
		{
			return eventArmed.WaitForTime(milliseconds);
		}

		bool Fire()
		{
			Func<void()> firingCallback;
			CS_LOCK(lockState)
			{
				if (!callback) return false;
				firingCallback = callback;
				callback = {};
				firing = true;
			}

			auto previous = currentFiring;
			currentFiring = this;
			try
			{
				firingCallback();
			}
			catch (...)
			{
				currentFiring = previous;
				FinishFiring();
				throw;
			}
			currentFiring = previous;
			FinishFiring();
			return true;
		}
	};

	thread_local ManualServerTimeoutController* ManualServerTimeoutController::currentFiring = nullptr;

	class SelectiveTimeoutControllerFactory : public Object
	{
	private:
		CriticalSection					lockState;
		Ptr<IHttpRequestTimeoutController>	nextController;

	public:
		void SetNext(Ptr<IHttpRequestTimeoutController> controller)
		{
			CHECK_ERROR(controller, L"The selective MiniHttp timeout factory requires a controller.");
			CS_LOCK(lockState)
			{
				CHECK_ERROR(!nextController, L"The selective MiniHttp timeout factory already has a pending controller.");
				nextController = controller;
			}
		}

		Ptr<IHttpRequestTimeoutController> Create()
		{
			Ptr<IHttpRequestTimeoutController> controller;
			CS_LOCK(lockState)
			{
				controller = nextController;
				nextController = nullptr;
			}
			return controller ? controller : CreateHttpRequestTimeoutController();
		}
	};

	HttpField CreateField(const WString& name, const WString& value)
	{
		HttpField field;
		field.name = name;
		auto utf8 = wtou8(value);
		field.value.Resize(utf8.Length());
		for (vint i = 0; i < utf8.Length(); i++)
		{
			field.value[i] = (vuint8_t)utf8[i];
		}
		return field;
	}

	void AddBodyChunk(HttpBody& body, const WString& value)
	{
		auto utf8 = wtou8(value);
		HttpBodyChunk chunk;
		chunk.data.Resize(utf8.Length());
		for (vint i = 0; i < utf8.Length(); i++)
		{
			chunk.data[i] = (vuint8_t)utf8[i];
		}
		body.chunks.Add(std::move(chunk));
	}

	Ptr<async_tcp_socket::HttpResponse> CreateResponse(vint statusCode, const WString& body, const WString& contentType = L"text/plain; charset=utf-8")
	{
		auto response = Ptr(new async_tcp_socket::HttpResponse);
		response->statusCode = statusCode;
		response->reason = L"MiniHttp Test";
		response->headers.Add(CreateField(L"Content-Type", contentType));
		response->headers.Add(CreateField(L"Set-Cookie", L"mini=http"));
		if (body.Length() > 1)
		{
			AddBodyChunk(response->body, body.Left(1));
			AddBodyChunk(response->body, body.Right(body.Length() - 1));
		}
		else if (body.Length() == 1)
		{
			AddBodyChunk(response->body, body);
		}
		return response;
	}

	WString FieldText(const HttpField& field)
	{
		Array<char8_t> buffer(field.value.Count());
		for (vint i = 0; i < field.value.Count(); i++)
		{
			buffer[i] = (char8_t)field.value[i];
		}
		return buffer.Count() == 0 ? WString() : u8tow(U8String::CopyFrom(&buffer[0], buffer.Count()));
	}

	wchar_t FoldAscii(wchar_t c)
	{
		return L'A' <= c && c <= L'Z' ? c - L'A' + L'a' : c;
	}

	bool SameFieldName(const WString& a, const WString& b)
	{
		if (a.Length() != b.Length()) return false;
		for (vint i = 0; i < a.Length(); i++)
		{
			if (FoldAscii(a[i]) != FoldAscii(b[i])) return false;
		}
		return true;
	}

	vint CountField(const List<HttpField>& fields, const WString& name)
	{
		vint count = 0;
		for (auto&& field : fields)
		{
			if (SameFieldName(field.name, name)) count++;
		}
		return count;
	}

	WString FindField(const List<HttpField>& fields, const WString& name)
	{
		for (auto&& field : fields)
		{
			if (SameFieldName(field.name, name)) return FieldText(field);
		}
		return {};
	}

	WString RawBodyUtf8(const HttpBody& body)
	{
		vint size = 0;
		for (auto&& chunk : body.chunks) size += chunk.data.Count();
		Array<char8_t> buffer(size);
		vint offset = 0;
		for (auto&& chunk : body.chunks)
		{
			for (auto value : chunk.data) buffer[offset++] = (char8_t)value;
		}
		return size == 0 ? WString() : u8tow(U8String::CopyFrom(&buffer[0], size));
	}

	bool SameRawBody(const HttpBody& body, const vuint8_t* expected, vint expectedSize)
	{
		vint actualSize = 0;
		for (auto&& chunk : body.chunks) actualSize += chunk.data.Count();
		if (actualSize != expectedSize) return false;
		vint offset = 0;
		for (auto&& chunk : body.chunks)
		{
			for (auto value : chunk.data)
			{
				if (value != expected[offset++]) return false;
			}
		}
		return true;
	}

	class TestServerApi : public SocketHttpServerApi
	{
	private:
		Ptr<TestState> state;
		Func<void(Ptr<SocketHttpRequestContext>)> handler;

	protected:
		void OnHttpRequestReceived(Ptr<SocketHttpRequestContext> context) override
		{
			state->serverRequests++;
			if (!handler)
			{
				context->Cancel();
				state->Fail(L"A MiniHttp server received a request without a handler.");
				return;
			}
			try
			{
				handler(context);
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"MiniHttp server request handler");
				context->Cancel();
			}
		}

		void OnHttpServerStopping() override
		{
			state->stoppingCallbacks++;
		}

	public:
		TestServerApi(const WString& urlPrefix, bool respondToOptions, Ptr<TestState> _state)
			: SocketHttpServerApi(urlPrefix, respondToOptions)
			, state(_state)
		{
		}

		~TestServerApi()
		{
			Stop();
		}

		void SetHandler(const Func<void(Ptr<SocketHttpRequestContext>)>& value)
		{
			handler = value;
		}
	};

	class PolicyServerApi : public SocketHttpServerApi
	{
	private:
		Ptr<TestState> state;

	protected:
		void OnHttpRequestReceived(Ptr<SocketHttpRequestContext> context) override
		{
			state->serverRequests++;
			auto relative = context->GetRelativePath();
			if (relative == L"/respond-then-throw")
			{
				state->Expect(context->Respond(CreateResponse(216, L"already-completed")), L"The respond-then-throw response was rejected.");
				throw Exception(L"Expected exception after a completed MiniHttp response.");
			}
			if (relative == L"/throw")
			{
				throw Exception(L"Expected MiniHttp application exception.");
			}
			auto status = relative == L"/method" ? 405 : 415;
			auto response = CreateResponse(status, WString::Empty);
			if (status == 405)
			{
				response->headers.Add(CreateField(L"Allow", L"GET, HEAD, POST, OPTIONS"));
			}
			state->Expect(context->Respond(response), L"The MiniHttp policy server could not respond.");
		}

		void OnHttpServerStopping() override
		{
			state->stoppingCallbacks++;
		}

	public:
		PolicyServerApi(Ptr<TestState> _state)
			: SocketHttpServerApi(L"http://localhost:38910/errors", false)
			, state(_state)
		{
		}

		~PolicyServerApi()
		{
			Stop();
		}
	};

	Ptr<TestServerApi> CreateValidationServer(const WString& urlPrefix)
	{
		return Ptr(new TestServerApi(urlPrefix, false, Ptr(new TestState)));
	}

	template<typename TNativeClient>
	Ptr<SocketHttpClientApi> CreateConnectedClient(vint port, Ptr<TestState> state)
	{
		auto nativeClient = Ptr<IAsyncSocketClient>(new TNativeClient(port));
		auto client = Ptr(new SocketHttpClientApi(nativeClient, L"localhost:" + itow(port)));
		auto queued = ThreadPoolLite::Queue(Func<void()>([client, state]()
		{
			SignalEventOnExit signal(state->eventConnected);
			try
			{
				client->WaitForServer();
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"SocketHttpClientApi::WaitForServer");
			}
		}));
		if (!queued)
		{
			state->Fail(L"ThreadPoolLite failed to queue SocketHttpClientApi::WaitForServer.");
			state->eventConnected.Signal();
		}
		auto connected = state->eventConnected.WaitForTime(ConnectTimeout);
		state->Expect(connected, L"SocketHttpClientApi::WaitForServer timed out.");
		if (!connected)
		{
			client->Stop();
			state->eventConnected.WaitForTime(ConnectTimeout);
		}
		else
		{
			state->Expect(client->GetStatus() == ClientStatus::Connected, L"SocketHttpClientApi did not report Connected after WaitForServer.");
		}
		return client;
	}

	void SubmitQuery(Ptr<SocketHttpClientApi> client, Ptr<TestState> state, const windows_http::HttpRequest& request)
	{
		client->HttpQuery(request, Func<void(Variant<windows_http::HttpResponse, windows_http::HttpError>)>([state](auto result)
		{
			try
			{
				state->AddResult(std::move(result));
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"SocketHttpClientApi response callback");
				state->eventDone.Signal();
			}
		}));
	}

	void InstallEchoHandler(Ptr<TestServerApi> server, Ptr<TestState> state, const WString& tag, vint statusCode)
	{
		server->SetHandler(Func<void(Ptr<SocketHttpRequestContext>)>([state, tag, statusCode](Ptr<SocketHttpRequestContext> context)
		{
			auto request = context->GetRequest();
			state->AddTarget(request->requestTarget);
			state->Expect(CountField(request->headers, L"host") == 1, L"SocketHttpClientApi did not send exactly one Host field.");
			state->Expect(FindField(request->headers, L"host") != WString::Empty, L"SocketHttpClientApi sent an empty Host field.");
			state->Expect(CountField(request->headers, L"accept-encoding") == 1, L"SocketHttpClientApi did not send exactly one Accept-Encoding field.");
			state->Expect(FindField(request->headers, L"accept-encoding") == L"identity", L"SocketHttpClientApi did not force identity response coding.");
			auto responseBody = tag + L":" + context->GetRelativePath() + L"?" + context->GetQuery();
			auto completion = Func<void(bool)>([state](bool succeeded)
			{
				if (succeeded) state->successfulCompletions++;
				else state->failedCompletions++;
				state->eventCompletion.Signal();
			});
			state->Expect(context->Respond(CreateResponse(statusCode, responseBody), completion), L"A fresh MiniHttp response was rejected.");
		}));
	}

	template<typename TNativeClient>
	Ptr<SocketHttpClientApi> CreateClientWithAuthority(vint port, const WString& authority)
	{
		return Ptr(new SocketHttpClientApi(Ptr<IAsyncSocketClient>(new TNativeClient(port)), authority));
	}

	template<typename TNativeServer>
	class NativeListenerFactoryScope
	{
	public:
		NativeListenerFactoryScope()
		{
			SetSocketHttpServerListenerFactoryForTesting(Func<Ptr<IAsyncSocketServer>(vint)>([](vint port)
			{
				return Ptr<IAsyncSocketServer>(new TNativeServer(port));
			}));
		}

		~NativeListenerFactoryScope()
		{
			try
			{
				ResetSocketHttpServerListenerFactoryForTesting();
			}
			catch (...)
			{
			}
		}
	};

	class ListenerFactoryScope
	{
	public:
		ListenerFactoryScope(const Func<Ptr<IAsyncSocketServer>(vint)>& factory)
		{
			SetSocketHttpServerListenerFactoryForTesting(factory);
		}

		~ListenerFactoryScope()
		{
			try
			{
				ResetSocketHttpServerListenerFactoryForTesting();
			}
			catch (...)
			{
			}
		}
	};

	class TimeoutControllerFactoryScope
	{
	public:
		TimeoutControllerFactoryScope(const Func<Ptr<IHttpRequestTimeoutController>()>& factory)
		{
			SetSocketHttpServerTimeoutControllerFactoryForTesting(factory);
		}

		~TimeoutControllerFactoryScope()
		{
			try
			{
				ResetSocketHttpServerTimeoutControllerFactoryForTesting();
			}
			catch (...)
			{
			}
		}
	};

	class BarrierListenerState : public Object
	{
	public:
		EventObject eventStartEntered;
		EventObject eventReleaseStart;
		EventObject eventFirstReturned;
		EventObject eventSecondCalling;
		EventObject eventSecondReturned;
		atomic_vint factoryCalls = 0;
		atomic_vint startCalls = 0;
		atomic_vint stopCalls = 0;
		Ptr<TestState> testState = Ptr(new TestState);

		BarrierListenerState()
		{
			CHECK_ERROR(eventStartEntered.CreateManualUnsignal(false), L"Failed to create the barrier listener entered event.");
			CHECK_ERROR(eventReleaseStart.CreateManualUnsignal(false), L"Failed to create the barrier listener release event.");
			CHECK_ERROR(eventFirstReturned.CreateManualUnsignal(false), L"Failed to create the first Start return event.");
			CHECK_ERROR(eventSecondCalling.CreateManualUnsignal(false), L"Failed to create the second Start calling event.");
			CHECK_ERROR(eventSecondReturned.CreateManualUnsignal(false), L"Failed to create the second Start return event.");
		}
	};

	class BarrierAsyncSocketServer : public Object, public virtual IAsyncSocketServer
	{
	private:
		Ptr<BarrierListenerState> state;
		atomic_vint stopped = 0;

	public:
		BarrierAsyncSocketServer(Ptr<BarrierListenerState> _state)
			: state(_state)
		{
		}

		void Start(IAsyncSocketServerCallback*) override
		{
			state->startCalls++;
			state->eventStartEntered.Signal();
			CHECK_ERROR(state->eventReleaseStart.Wait(), L"The barrier listener failed to wait for release.");
		}

		void Stop() override
		{
			if (stopped.exchange(1) == 0) state->stopCalls++;
		}

		bool IsStopped() override
		{
			return stopped != 0;
		}
	};

	class RejectingServerCallback : public Object, public virtual IAsyncSocketServerCallback
	{
	public:
		WaitForClientResult OnClientConnected(IAsyncSocketConnection*) override
		{
			return WaitForClientResult::Reject;
		}
	};

	class WireProbeState : public TestState
	{
	private:
		CriticalSection lockBytes;
		List<vuint8_t> bytes;

	public:
		atomic_vint writes = 0;

		void Append(const vuint8_t* buffer, vint size)
		{
			CS_LOCK(lockBytes)
			{
				for (vint i = 0; i < size; i++) bytes.Add(buffer[i]);
			}
		}

		vint GetStatusCode()
		{
			vint status = 0;
			CS_LOCK(lockBytes)
			{
				if (
					bytes.Count() >= 12 &&
					bytes[0] == 'H' && bytes[1] == 'T' && bytes[2] == 'T' && bytes[3] == 'P' &&
					bytes[4] == '/' && bytes[5] == '1' && bytes[6] == '.' && bytes[7] == '1' && bytes[8] == ' ' &&
					bytes[9] >= '0' && bytes[9] <= '9' &&
					bytes[10] >= '0' && bytes[10] <= '9' &&
					bytes[11] >= '0' && bytes[11] <= '9'
					)
				{
					status = (bytes[9] - '0') * 100 + (bytes[10] - '0') * 10 + (bytes[11] - '0');
				}
			}
			return status;
		}
	};

	class WireProbeCallback : public Object, public virtual IAsyncSocketCallback
	{
	private:
		Ptr<WireProbeState> state;

	public:
		WireProbeCallback(Ptr<WireProbeState> _state)
			: state(_state)
		{
		}

		void OnRead(const vuint8_t* buffer, vint size) override
		{
			state->Append(buffer, size);
		}

		void OnWriteCompleted(Ptr<AsyncSocketBuffer>) override
		{
			state->writes++;
		}

		void OnError(const WString& error, bool fatal) override
		{
			state->Fail(WString(fatal ? L"Fatal" : L"Nonfatal") + L" MiniHttp wire-probe error: " + error);
			state->eventDone.Signal();
		}

		void OnDisconnected() override
		{
			state->eventDone.Signal();
		}

		void OnInstalled(IAsyncSocketConnection*) override
		{
		}
	};

	WString RepeatCharacter(wchar_t character, vint count)
	{
		if (count == 0) return {};
		auto buffer = new wchar_t[count + 1];
		for (vint i = 0; i < count; i++) buffer[i] = character;
		buffer[count] = 0;
		return WString::TakeOver(buffer, count);
	}

	Ptr<AsyncSocketBuffer> CreateWireBytes(const WString& text)
	{
		auto utf8 = wtou8(text);
		auto buffer = Ptr(new AsyncSocketBuffer);
		buffer->data.Resize(utf8.Length());
		for (vint i = 0; i < utf8.Length(); i++) buffer->data[i] = (vuint8_t)utf8[i];
		return buffer;
	}

	template<typename TNativeClient>
	vint ProbeWireStatus(
		vint port,
		const WString& request,
		vint timeout = TransferTimeout,
		const Func<void()>& afterWrite = {}
		)
	{
		auto state = Ptr(new WireProbeState);
		auto client = Ptr<IAsyncSocketClient>(new TNativeClient(port));
		auto callback = Ptr(new WireProbeCallback(state));
		client->GetConnection()->InstallCallback(callback.Obj());
		auto queued = ThreadPoolLite::Queue(Func<void()>([client, state]()
		{
			SignalEventOnExit signal(state->eventConnected);
			try
			{
				client->WaitForServer();
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"MiniHttp wire-probe WaitForServer");
				state->eventDone.Signal();
			}
		}));
		if (!queued)
		{
			state->Fail(L"Failed to queue MiniHttp wire-probe WaitForServer.");
			state->eventConnected.Signal();
		}
		auto connected = state->eventConnected.WaitForTime(ConnectTimeout);
		state->Expect(connected, L"MiniHttp wire-probe WaitForServer timed out.");
		if (connected && client->GetStatus() == ClientStatus::Connected)
		{
			try
			{
				client->GetConnection()->BeginReadingLoopUnsafe();
				client->GetConnection()->WriteAsync(CreateWireBytes(request));
				if (afterWrite) afterWrite();
				state->Expect(state->eventDone.WaitForTime(timeout), L"MiniHttp wire-probe response timed out.");
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"Running MiniHttp wire probe");
			}
		}
		try
		{
			client->GetConnection()->Stop();
			client->GetConnection()->InstallCallback(nullptr);
		}
		catch (...)
		{
			RecordCurrentException(*state.Obj(), L"Stopping MiniHttp wire probe");
		}
		AssertState(*state.Obj());
		TEST_ASSERT(state->writes == 1);
		return state->GetStatusCode();
	}

	class RawSequenceState : public TestState
	{
	public:
		List<Ptr<async_tcp_socket::HttpRequest>> requests;
		List<Ptr<async_tcp_socket::HttpResponse>> responses;
		atomic_vint writes = 0;

		RawSequenceState(vint expected)
			: TestState(expected)
		{
		}
	};

	class DeferredState : public TestState
	{
	private:
		CriticalSection lockContext;
		Ptr<SocketHttpRequestContext> context;

	public:
		DeferredState(vint expected = 0)
			: TestState(expected)
		{
		}

		void SetContext(Ptr<SocketHttpRequestContext> value)
		{
			CS_LOCK(lockContext)
			{
				if (context)
				{
					Fail(L"A deferred MiniHttp context slot was assigned twice.");
				}
				else
				{
					context = value;
				}
			}
			eventContext.Signal();
		}

		Ptr<SocketHttpRequestContext> GetContext()
		{
			Ptr<SocketHttpRequestContext> result;
			CS_LOCK(lockContext)
			{
				result = context;
			}
			return result;
		}
	};

	class RawSequenceCallback : public Object, public virtual IHttpRequestCallback
	{
	private:
		Ptr<RawSequenceState> state;
		IHttpRequestConnection* connection = nullptr;
		vint responseIndex = 0;

	public:
		RawSequenceCallback(Ptr<RawSequenceState> _state)
			: state(_state)
		{
		}

		void Start()
		{
			if (state->requests.Count() == 0)
			{
				state->eventDone.Signal();
				return;
			}
			connection->SendRequest(state->requests[0]);
		}

		void OnReadRequest(Ptr<async_tcp_socket::HttpRequest>) override
		{
			state->Fail(L"A raw MiniHttp test client received a request instead of a response.");
			state->eventDone.Signal();
		}

		void OnReadResponse(Ptr<async_tcp_socket::HttpResponse> response) override
		{
			try
			{
				state->responses.Add(response);
				responseIndex++;
				if (responseIndex < state->requests.Count())
				{
					connection->SendRequest(state->requests[responseIndex]);
				}
				else
				{
					state->eventDone.Signal();
				}
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"Raw MiniHttp response callback");
				state->eventDone.Signal();
			}
		}

		void OnWriteCompleted() override
		{
			state->writes++;
		}

		void OnError(const WString& error, bool fatal) override
		{
			state->Fail(WString(fatal ? L"Fatal" : L"Nonfatal") + L" raw MiniHttp client error: " + error);
			state->eventDone.Signal();
		}

		void OnInstalled(IHttpRequestConnection* value) override
		{
			connection = value;
		}
	};

	Ptr<async_tcp_socket::HttpRequest> CreateRawRequest(const WString& method, const WString& target, vint port, bool addHost = true)
	{
		auto request = Ptr(new async_tcp_socket::HttpRequest);
		request->method = method;
		request->requestTarget = target;
		if (addHost)
		{
			request->headers.Add(CreateField(L"host", L"localhost:" + itow(port)));
		}
		return request;
	}

	template<typename TNativeClient>
	void RunRawSequence(vint port, Ptr<RawSequenceState> state)
	{
		auto nativeClient = Ptr<IAsyncSocketClient>(new TNativeClient(port));
		auto connection = Ptr(new HttpRequestConnection(
			nativeClient->GetConnection(),
			HttpRequestConnectionDirection::Client
			));
		auto callback = Ptr(new RawSequenceCallback(state));
		connection->InstallCallback(callback.Obj());
		auto queued = ThreadPoolLite::Queue(Func<void()>([nativeClient, state]()
		{
			SignalEventOnExit signal(state->eventConnected);
			try
			{
				nativeClient->WaitForServer();
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"Raw MiniHttp native client WaitForServer");
				state->eventDone.Signal();
			}
		}));
		if (!queued)
		{
			state->Fail(L"ThreadPoolLite failed to queue raw MiniHttp WaitForServer.");
			state->eventConnected.Signal();
		}

		auto connected = state->eventConnected.WaitForTime(ConnectTimeout);
		state->Expect(connected, L"The raw MiniHttp client WaitForServer timed out.");
		if (connected && nativeClient->GetStatus() == ClientStatus::Connected)
		{
			try
			{
				connection->BeginReadingLoopUnsafe();
				callback->Start();
				state->Expect(state->eventDone.WaitForTime(TransferTimeout), L"The raw MiniHttp request sequence timed out.");
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"Running a raw MiniHttp request sequence");
			}
		}
		else
		{
			state->Fail(L"The raw MiniHttp client did not connect.");
		}

		try
		{
			connection->Stop();
			connection->InstallCallback(nullptr);
		}
		catch (...)
		{
			RecordCurrentException(*state.Obj(), L"Stopping a raw MiniHttp request sequence");
		}
		if (queued && !connected)
		{
			state->eventConnected.WaitForTime(ConnectTimeout);
		}
	}

	template<typename TNativeServer, typename TNativeClient>
	void RunCrossPlatformMiniHttpApiTestCases()
	{
		TEST_CASE(L"Common HTTP values, UTF-8 helpers, query helpers, and route constants are platform-neutral")
		{
			windows_http::HttpRequest request;
			request.SetBodyUtf8(L"Common HTTP \x4F60\x597D");
			windows_http::HttpResponse response;
			response.body.Resize(request.body.Count());
			for (vint i = 0; i < request.body.Count(); i++) response.body[i] = request.body[i];
			TEST_ASSERT(response.GetBodyUtf8() == L"Common HTTP \x4F60\x597D");

			windows_http::HttpError error;
			error.errorCode = 0xFEDCBA98u;
			error.operation = L"portable";
			error.message = L"portable error";
			TEST_ASSERT(error.errorCode == 0xFEDCBA98u);

			auto encoded = SocketHttpClientApi::UrlEncodeQuery(L"a b/\x4F60+%");
			TEST_ASSERT(encoded == L"a%20b%2F%E4%BD%A0%2B%25");
			TEST_ASSERT(SocketHttpClientApi::UrlDecodeQuery(encoded) == L"a b/\x4F60+%");
			TEST_ASSERT(HttpUrlEncodeQuery(L"a b/\x4F60+%") == encoded);
			TEST_ASSERT(HttpUrlDecodeQuery(encoded) == SocketHttpClientApi::UrlDecodeQuery(encoded));
			TEST_ASSERT(WString(HttpServerUrl_Connect) == L"/VlppInterProcess/Connect");
			TEST_ASSERT(WString(HttpServerUrl_Request) == L"/VlppInterProcess/Request");
			TEST_ASSERT(WString(HttpServerUrl_Response) == L"/VlppInterProcess/Response");
		});

		TEST_CASE(L"SocketHttpServerApi rejects duplicate normalized prefixes")
		{
			NativeListenerFactoryScope<TNativeServer> listenerFactory;
			auto state = Ptr(new TestState);
			auto first = Ptr(new TestServerApi(L"http://localhost:38903/duplicate///", false, state));
			auto second = Ptr(new TestServerApi(L"http://LOCALHOST:38903/duplicate", false, state));
			first->Start();
			TEST_ERROR(second->Start());
			second->Stop();
			first->Stop();
			TEST_ASSERT(first->IsStopped());
			TEST_ASSERT(second->IsStopped());
			AssertState(*state.Obj());
		});

		TEST_CASE(L"SocketHttpServerApi concurrent Start joins one barrier-controlled in-progress listener")
		{
			auto barrier = Ptr(new BarrierListenerState);
			ListenerFactoryScope listenerFactory(Func<Ptr<IAsyncSocketServer>(vint)>([barrier](vint port)
			{
				barrier->testState->Expect(port == 38908, L"The barrier listener factory received the wrong port.");
				barrier->factoryCalls++;
				return Ptr<IAsyncSocketServer>(new BarrierAsyncSocketServer(barrier));
			}));
			auto first = Ptr(new TestServerApi(L"http://localhost:38908/first", false, barrier->testState));
			auto second = Ptr(new TestServerApi(L"http://localhost:38908/second", false, barrier->testState));

			auto firstQueued = ThreadPoolLite::Queue(Func<void()>([first, barrier]()
			{
				SignalEventOnExit signal(barrier->eventFirstReturned);
				try
				{
					first->Start();
				}
				catch (...)
				{
					RecordCurrentException(*barrier->testState.Obj(), L"First concurrent SocketHttpServerApi::Start");
				}
			}));
			barrier->testState->Expect(firstQueued, L"Failed to queue the first concurrent SocketHttpServerApi::Start.");
			barrier->testState->Expect(barrier->eventStartEntered.WaitForTime(ConnectTimeout), L"The barrier listener Start was not entered.");

			auto secondQueued = ThreadPoolLite::Queue(Func<void()>([second, barrier]()
			{
				SignalEventOnExit returned(barrier->eventSecondReturned);
				barrier->eventSecondCalling.Signal();
				try
				{
					second->Start();
				}
				catch (...)
				{
					RecordCurrentException(*barrier->testState.Obj(), L"Second concurrent SocketHttpServerApi::Start");
				}
			}));
			barrier->testState->Expect(secondQueued, L"Failed to queue the second concurrent SocketHttpServerApi::Start.");
			barrier->testState->Expect(barrier->eventSecondCalling.WaitForTime(ConnectTimeout), L"The second concurrent Start task did not reach its call boundary.");
			barrier->testState->Expect(!barrier->eventSecondReturned.WaitForTime(100), L"The second concurrent Start returned before the in-progress listener was published.");
			barrier->eventReleaseStart.Signal();
			barrier->testState->Expect(barrier->eventFirstReturned.WaitForTime(ConnectTimeout), L"The first concurrent Start did not return after barrier release.");
			barrier->testState->Expect(barrier->eventSecondReturned.WaitForTime(ConnectTimeout), L"The second concurrent Start did not join the published listener.");

			first->Stop();
			second->Stop();
			TEST_ASSERT(barrier->factoryCalls == 1);
			TEST_ASSERT(barrier->startCalls == 1);
			TEST_ASSERT(barrier->stopCalls == 1);
			TEST_ASSERT(first->IsStopped());
			TEST_ASSERT(second->IsStopped());
			AssertState(*barrier->testState.Obj());
		});

		TEST_CASE(L"SocketHttpServerApi retries a genuinely occupied native port exactly five times")
		{
			RejectingServerCallback occupiedCallback;
			auto occupied = Ptr<IAsyncSocketServer>(new TNativeServer(38909));
			occupied->Start(&occupiedCallback);
			auto state = Ptr(new TestState);
			ListenerFactoryScope listenerFactory(Func<Ptr<IAsyncSocketServer>(vint)>([state](vint port)
			{
				state->listenerFactoryCalls++;
				return Ptr<IAsyncSocketServer>(new TNativeServer(port));
			}));
			auto blocked = Ptr(new TestServerApi(L"http://localhost:38909/blocked", false, state));
			TEST_EXCEPTION(blocked->Start(), AsyncSocketServerStartException, [state](const AsyncSocketServerStartException& exception)
			{
				state->Expect(exception.GetFailure() == AsyncSocketServerStartFailure::AddressInUse, L"An occupied MiniHttp port was not classified as AddressInUse.");
			});
			blocked->Stop();
			occupied->Stop();
			TEST_ASSERT(state->listenerFactoryCalls == 5);
			TEST_ASSERT(blocked->IsStopped());
			AssertState(*state.Obj());
		});

		TEST_CASE(L"SocketHttpServerApi shares one port and dispatches every persistent request to the longest prefix")
		{
			NativeListenerFactoryScope<TNativeServer> listenerFactory;
			auto serverState = Ptr(new TestState);
			auto root = Ptr(new TestServerApi(L"http://localhost:38900////", false, serverState));
			auto api = Ptr(new TestServerApi(L"http://localhost:38900/api///", false, serverState));
			auto nested = Ptr(new TestServerApi(L"http://localhost:38900/api/nested/", false, serverState));
			InstallEchoHandler(root, serverState, L"R", 209);
			InstallEchoHandler(api, serverState, L"A", 210);
			nested->SetHandler(Func<void(Ptr<SocketHttpRequestContext>)>([serverState](Ptr<SocketHttpRequestContext> context)
			{
				auto request = context->GetRequest();
				const vuint8_t expected[] = { 'A', 0, 'B', 0xFF, 'Z' };
				serverState->AddTarget(request->requestTarget);
				serverState->Expect(request->method == L"POST", L"The nested MiniHttp API did not receive POST.");
				serverState->Expect(context->GetRelativePath() == L"/two", L"The nested MiniHttp API received the wrong relative path.");
				serverState->Expect(context->GetQuery() == L"binary=%2F", L"The nested MiniHttp API changed the raw query.");
				serverState->Expect(FindField(request->headers, L"host") == L"localhost:38900", L"SocketHttpClientApi sent the wrong automatic Host.");
				serverState->Expect(FindField(request->headers, L"accept-encoding") == L"identity", L"SocketHttpClientApi did not request identity coding.");
				serverState->Expect(FindField(request->headers, L"content-type") == L"application/octet-stream", L"SocketHttpClientApi lost the binary POST content type.");
				serverState->Expect(FindField(request->headers, L"content-length") == L"5", L"SocketHttpClientApi did not use fixed binary POST framing.");
				serverState->Expect(FindField(request->headers, L"x-binary-post") == L"exact", L"SocketHttpClientApi lost the binary POST extension field.");
				serverState->Expect(SameRawBody(request->body, expected, 5), L"SocketHttpClientApi changed the binary POST body.");
				serverState->Expect(context->Respond(CreateResponse(211, L"B:/two?binary=%2F"), Func<void(bool)>([serverState](bool succeeded)
				{
					if (succeeded) serverState->successfulCompletions++;
					else serverState->failedCompletions++;
				})), L"The nested MiniHttp API could not respond.");
			}));
			root->Start();
			api->Start();
			nested->Start();

			auto phase1 = Ptr(new TestState(5));
			auto client = CreateConnectedClient<TNativeClient>(38900, phase1);
			windows_http::HttpRequest request1;
			request1.method = L"GET";
			request1.query = L"/root-only?raw=a%2Fb";
			request1.acceptTypes.Add(L"text/plain");
			request1.cookie = L"request=cookie";
			request1.extraHeaders.Add(L"X-Mini-Header", L"mini-value");

			windows_http::HttpRequest request2;
			request2.method = L"GET";
			request2.query = L"/api";

			windows_http::HttpRequest request3;
			request3.method = L"POST";
			request3.query = L"/api/nested/two?binary=%2F";
			request3.contentType = L"application/octet-stream";
			request3.extraHeaders.Add(L"X-Binary-Post", L"exact");
			request3.body.Resize(5);
			const vuint8_t binaryPost[] = { 'A', 0, 'B', 0xFF, 'Z' };
			for (vint i = 0; i < 5; i++) request3.body[i] = (char)binaryPost[i];

			windows_http::HttpRequest request4;
			request4.method = L"GET";
			request4.query = L"/api2";

			windows_http::HttpRequest request5;
			request5.method = L"HEAD";
			request5.query = L"/api/head";

			SubmitQuery(client, phase1, request1);
			SubmitQuery(client, phase1, request2);
			SubmitQuery(client, phase1, request3);
			SubmitQuery(client, phase1, request4);
			SubmitQuery(client, phase1, request5);
			phase1->Expect(phase1->eventDone.WaitForTime(TransferTimeout), L"The first persistent MiniHttp routing phase timed out.");

			root->Stop();
			auto phase2 = Ptr(new TestState(1));
			windows_http::HttpRequest unmatched;
			unmatched.method = L"GET";
			unmatched.query = L"/unmatched";
			SubmitQuery(client, phase2, unmatched);
			phase2->Expect(phase2->eventDone.WaitForTime(TransferTimeout), L"The MiniHttp unmatched-prefix phase timed out.");

			nested->Stop();
			auto phase3 = Ptr(new TestState(2));
			windows_http::HttpRequest fallback;
			fallback.method = L"GET";
			fallback.query = L"/api/nested/after-stop";
			windows_http::HttpRequest unknownMethod;
			unknownMethod.method = L"PUT";
			unknownMethod.query = L"/api/put";
			SubmitQuery(client, phase3, fallback);
			SubmitQuery(client, phase3, unknownMethod);
			phase3->Expect(phase3->eventDone.WaitForTime(TransferTimeout), L"The MiniHttp stopped-prefix fallback phase timed out.");

			client->Stop();
			phase3->stopReturned = 1;
			api->Stop();

			auto records = phase1->CopyRecords();
			TEST_ASSERT(records.Count() == 5);
			if (records.Count() == 5)
			{
				TEST_ASSERT(!records[0].isError && records[0].statusCode == 209);
				TEST_ASSERT(records[0].body == L"R:/root-only?raw=a%2Fb");
				TEST_ASSERT(records[0].contentType == L"text/plain; charset=utf-8");
				TEST_ASSERT(records[0].cookie == L"mini=http");
				TEST_ASSERT(!records[1].isError && records[1].statusCode == 210);
				TEST_ASSERT(records[1].body == L"A:/?");
				TEST_ASSERT(!records[2].isError && records[2].statusCode == 211);
				TEST_ASSERT(records[2].body == L"B:/two?binary=%2F");
				TEST_ASSERT(!records[3].isError && records[3].statusCode == 209);
				TEST_ASSERT(records[3].body == L"R:/api2?");
				TEST_ASSERT(!records[4].isError && records[4].statusCode == 210);
				TEST_ASSERT(records[4].body == WString::Empty);
			}
			auto phase2Records = phase2->CopyRecords();
			TEST_ASSERT(phase2Records.Count() == 1 && !phase2Records[0].isError && phase2Records[0].statusCode == 404);
			auto phase3Records = phase3->CopyRecords();
			TEST_ASSERT(phase3Records.Count() == 2);
			if (phase3Records.Count() == 2)
			{
				TEST_ASSERT(!phase3Records[0].isError && phase3Records[0].statusCode == 210);
				TEST_ASSERT(phase3Records[0].body == L"A:/nested/after-stop?");
				TEST_ASSERT(!phase3Records[1].isError && phase3Records[1].statusCode == 501);
			}
			TEST_ASSERT(serverState->serverRequests == 6);
			TEST_ASSERT(serverState->successfulCompletions == 6);
			TEST_ASSERT(serverState->failedCompletions == 0);
			TEST_ASSERT(phase3->callbacksAfterStop == 0);
			TEST_ASSERT(root->IsStopped());
			TEST_ASSERT(api->IsStopped());
			TEST_ASSERT(nested->IsStopped());

			auto replacement = Ptr(new TestServerApi(L"http://localhost:38900/replacement", false, serverState));
			replacement->Start();
			replacement->Stop();
			TEST_ASSERT(replacement->IsStopped());
			AssertState(*phase1.Obj());
			AssertState(*phase2.Obj());
			AssertState(*phase3.Obj());
			AssertState(*serverState.Obj());
		});

		TEST_CASE(L"SocketHttpClientApi validates authority and maps unsupported WinHTTP-shaped options to HttpError")
		{
			NativeListenerFactoryScope<TNativeServer> listenerFactory;
			TEST_ERROR(CreateClientWithAuthority<TNativeClient>(38904, L"localhost"));
			TEST_ERROR(CreateClientWithAuthority<TNativeClient>(38904, L"example.com:38904"));
			TEST_ERROR(CreateClientWithAuthority<TNativeClient>(38904, L"user@localhost:38904"));
			TEST_ERROR(CreateClientWithAuthority<TNativeClient>(38904, L"localhost:0"));
			TEST_ERROR(CreateClientWithAuthority<TNativeClient>(38904, L"localhost:65536"));

			auto state = Ptr(new TestState(6));
			auto server = Ptr(new TestServerApi(L"http://localhost:38904/client", false, state));
			InstallEchoHandler(server, state, L"V", 212);
			server->Start();
			auto client = CreateConnectedClient<TNativeClient>(38904, state);

			windows_http::HttpRequest secure;
			secure.method = L"GET";
			secure.query = L"/client/secure";
			secure.secure = true;
			SubmitQuery(client, state, secure);

			windows_http::HttpRequest credentials;
			credentials.method = L"GET";
			credentials.query = L"/client/credentials";
			credentials.username = L"name";
			credentials.password = L"password";
			SubmitQuery(client, state, credentials);

			windows_http::HttpRequest keepAlive;
			keepAlive.method = L"GET";
			keepAlive.query = L"/client/keep-alive";
			keepAlive.keepAliveOnStop = true;
			SubmitQuery(client, state, keepAlive);

			windows_http::HttpRequest host;
			host.method = L"GET";
			host.query = L"/client/host";
			host.extraHeaders.Add(L"Host", L"localhost:1");
			SubmitQuery(client, state, host);

			windows_http::HttpRequest encoding;
			encoding.method = L"GET";
			encoding.query = L"/client/encoding";
			encoding.extraHeaders.Add(L"Accept-Encoding", L"gzip");
			SubmitQuery(client, state, encoding);

			windows_http::HttpRequest valid;
			valid.method = L"GET";
			valid.query = L"/client/valid";
			valid.extraHeaders.Add(L"Host", L" LOCALHOST:38904 ");
			valid.extraHeaders.Add(L"Accept-Encoding", L" identity ");
			SubmitQuery(client, state, valid);

			state->Expect(state->eventDone.WaitForTime(TransferTimeout), L"The MiniHttp client validation sequence timed out.");
			client->Stop();
			state->stopReturned = 1;
			server->Stop();

			auto records = state->CopyRecords();
			TEST_ASSERT(records.Count() == 6);
			if (records.Count() == 6)
			{
				for (vint i = 0; i < 5; i++)
				{
					TEST_ASSERT(records[i].isError);
					TEST_ASSERT(records[i].errorCode != 0);
					TEST_ASSERT(records[i].operation != WString::Empty);
					TEST_ASSERT(records[i].message != WString::Empty);
				}
				TEST_ASSERT(!records[5].isError && records[5].statusCode == 212);
				TEST_ASSERT(records[5].body == L"V:/valid?");
			}
			TEST_ASSERT(state->serverRequests == 1);
			TEST_ASSERT(state->callbacksAfterStop == 0);
			AssertState(*state.Obj());
		});

		TEST_CASE(L"SocketHttpServerApi delivers application 405 and 415 and converts a pending application exception to 500")
		{
			NativeListenerFactoryScope<TNativeServer> listenerFactory;
			auto serverState = Ptr(new TestState);
			auto server = Ptr(new PolicyServerApi(serverState));
			server->Start();
			auto state = Ptr(new TestState(4));
			auto client = CreateConnectedClient<TNativeClient>(38910, state);
			windows_http::HttpRequest method;
			method.method = L"POST";
			method.query = L"/errors/method";
			windows_http::HttpRequest media;
			media.method = L"POST";
			media.query = L"/errors/media";
			windows_http::HttpRequest throwing;
			throwing.method = L"GET";
			throwing.query = L"/errors/throw";
			windows_http::HttpRequest completedThrow;
			completedThrow.method = L"GET";
			completedThrow.query = L"/errors/respond-then-throw";
			SubmitQuery(client, state, method);
			SubmitQuery(client, state, media);
			SubmitQuery(client, state, throwing);
			SubmitQuery(client, state, completedThrow);
			state->Expect(state->eventDone.WaitForTime(TransferTimeout), L"The MiniHttp application status sequence timed out.");
			client->Stop();
			server->Stop();

			auto records = state->CopyRecords();
			TEST_ASSERT(records.Count() == 4);
			if (records.Count() == 4)
			{
				TEST_ASSERT(!records[0].isError && records[0].statusCode == 405);
				TEST_ASSERT(!records[1].isError && records[1].statusCode == 415);
				TEST_ASSERT(!records[2].isError && records[2].statusCode == 500);
				TEST_ASSERT(!records[3].isError && records[3].statusCode == 216 && records[3].body == L"already-completed");
			}
			TEST_ASSERT(serverState->serverRequests == 4);
			TEST_ASSERT(serverState->stoppingCallbacks == 1);
			AssertState(*state.Obj());
			AssertState(*serverState.Obj());
		});

		TEST_CASE(L"SocketHttpServerApi maps malformed wire requests to structured 400 408 413 414 417 431 501 and 505 responses")
		{
			NativeListenerFactoryScope<TNativeServer> listenerFactory;
			auto timeoutFactory = Ptr(new SelectiveTimeoutControllerFactory);
			TimeoutControllerFactoryScope timeoutFactoryScope(Func<Ptr<IHttpRequestTimeoutController>()>([timeoutFactory]()
			{
				return timeoutFactory->Create();
			}));
			auto serverState = Ptr(new TestState);
			auto server = Ptr(new TestServerApi(L"http://localhost:38911/errors", false, serverState));
			server->SetHandler(Func<void(Ptr<SocketHttpRequestContext>)>([serverState](Ptr<SocketHttpRequestContext> context)
			{
				serverState->Fail(L"A malformed MiniHttp wire probe reached application dispatch.");
				context->Cancel();
			}));
			server->Start();

			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"GET /errors HTTP/1.1\r\n\r\n"
				) == 400);
			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"GET /errors HTTP/1.1\r\nHost: localhost:38911\r\nHost: localhost:38911\r\n\r\n"
				) == 400);
			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"GET /errors/%2Fchild HTTP/1.1\r\nHost: localhost:38911\r\n\r\n"
				) == 400);
			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"GET /errors/%C0%AF HTTP/1.1\r\nHost: localhost:38911\r\n\r\n"
				) == 400);
			auto manualTimeout = Ptr(new ManualServerTimeoutController);
			timeoutFactory->SetNext(manualTimeout);
			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"GET /errors HTTP/1.1\r\nHost: localhost:38911\r\n",
				ConnectTimeout,
				Func<void()>([manualTimeout]()
				{
					CHECK_ERROR(manualTimeout->WaitUntilArmed(ConnectTimeout), L"The MiniHttp 408 timeout controller was not armed.");
					CHECK_ERROR(manualTimeout->Fire(), L"The MiniHttp 408 timeout controller could not fire.");
				})
				) == 408);
			TEST_ASSERT(manualTimeout->armCount == 1);
			TEST_ASSERT(manualTimeout->lastArmDuration == HttpIncompleteMessageTimeout);
			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"POST /errors HTTP/1.1\r\nHost: localhost:38911\r\nContent-Length: 16777217\r\n\r\n"
				) == 413);
			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"GET /" + RepeatCharacter(L'a', HttpRequestLineSizeLimit) + L" HTTP/1.1\r\nHost: localhost:38911\r\n\r\n"
				) == 414);
			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"POST /errors HTTP/1.1\r\nHost: localhost:38911\r\nContent-Length: 10\r\nExpect: unsupported\r\n\r\n"
				) == 417);
			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"GET /errors HTTP/1.1\r\nHost: localhost:38911\r\nX-Large: " + RepeatCharacter(L'x', HttpHeaderBlockSizeLimit) + L"\r\n\r\n"
				) == 431);
			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"POST /errors HTTP/1.1\r\nHost: localhost:38911\r\nTransfer-Encoding: gzip\r\n\r\n"
				) == 501);
			TEST_ASSERT(ProbeWireStatus<TNativeClient>(
				38911,
				L"GET /errors HTTP/1.0\r\nHost: localhost:38911\r\n\r\n"
				) == 505);

			server->Stop();
			TEST_ASSERT(serverState->serverRequests == 0);
			AssertState(*serverState.Obj());
		});

		TEST_CASE(L"SocketHttpServerApi provides CORS preflight, ordinary OPTIONS, and bodyless response semantics")
		{
			NativeListenerFactoryScope<TNativeServer> listenerFactory;
			auto serverState = Ptr(new TestState);
			auto server = Ptr(new TestServerApi(L"http://localhost:38905/cors", true, serverState));
			server->SetHandler(Func<void(Ptr<SocketHttpRequestContext>)>([serverState](Ptr<SocketHttpRequestContext> context)
			{
				auto request = context->GetRequest();
				serverState->AddTarget(request->requestTarget);
				vint status = 213;
				WString body = L"ordinary-options";
				if (context->GetRelativePath() == L"/head")
				{
					status = 214;
					body = L"head-body";
				}
				else if (context->GetRelativePath() == L"/no-content")
				{
					status = 204;
					body = L"forbidden-204-body";
				}
				else if (context->GetRelativePath() == L"/not-modified")
				{
					status = 304;
					body = L"forbidden-304-body";
				}
				serverState->Expect(context->Respond(CreateResponse(status, body)), L"The CORS MiniHttp handler could not respond.");
			}));
			server->Start();

			auto state = Ptr(new RawSequenceState(9));
			auto supported = CreateRawRequest(L"OPTIONS", L"/cors/resource", 38905);
			supported->headers.Add(CreateField(L"access-control-request-method", L"POST"));
			supported->headers.Add(CreateField(L"access-control-request-headers", L"Accept, Content-Type"));
			state->requests.Add(supported);

			auto unsupportedMethod = CreateRawRequest(L"OPTIONS", L"/cors/resource", 38905);
			unsupportedMethod->headers.Add(CreateField(L"access-control-request-method", L"DELETE"));
			state->requests.Add(unsupportedMethod);

			auto unsupportedHeader = CreateRawRequest(L"OPTIONS", L"/cors/resource", 38905);
			unsupportedHeader->headers.Add(CreateField(L"access-control-request-method", L"POST"));
			unsupportedHeader->headers.Add(CreateField(L"access-control-request-headers", L"X-Secret"));
			state->requests.Add(unsupportedHeader);
			state->requests.Add(CreateRawRequest(L"OPTIONS", L"*", 38905));
			auto unsupportedStar = CreateRawRequest(L"OPTIONS", L"*", 38905);
			unsupportedStar->headers.Add(CreateField(L"access-control-request-method", L"DELETE"));
			state->requests.Add(unsupportedStar);
			state->requests.Add(CreateRawRequest(L"OPTIONS", L"/cors/ordinary", 38905));
			state->requests.Add(CreateRawRequest(L"HEAD", L"/cors/head", 38905));
			state->requests.Add(CreateRawRequest(L"GET", L"/cors/no-content", 38905));
			state->requests.Add(CreateRawRequest(L"GET", L"/cors/not-modified", 38905));

			RunRawSequence<TNativeClient>(38905, state);
			AssertState(*state.Obj());
			server->Stop();

			TEST_ASSERT(state->responses.Count() == 9);
			if (state->responses.Count() == 9)
			{
				auto response = state->responses[0];
				TEST_ASSERT(response->statusCode == 200);
				TEST_ASSERT(FindField(response->headers, L"content-length") == L"0");
				TEST_ASSERT(FindField(response->headers, L"access-control-allow-origin") == L"*");
				TEST_ASSERT(FindField(response->headers, L"access-control-allow-methods") == L"GET, HEAD, POST, OPTIONS");
				TEST_ASSERT(FindField(response->headers, L"access-control-allow-headers") == L"Accept, Content-Type");
				TEST_ASSERT(FindField(response->headers, L"allow") == L"GET, HEAD, POST, OPTIONS");
				TEST_ASSERT(FindField(response->headers, L"date") != WString::Empty);
				TEST_ASSERT(FindField(response->headers, L"cache-control") == L"no-store");
				TEST_ASSERT(CountField(response->headers, L"access-control-allow-credentials") == 0);
				TEST_ASSERT(state->responses[1]->statusCode == 405);
				TEST_ASSERT(state->responses[2]->statusCode == 400);
				TEST_ASSERT(state->responses[3]->statusCode == 200);
				TEST_ASSERT(state->responses[4]->statusCode == 405);
				TEST_ASSERT(state->responses[5]->statusCode == 213);
				TEST_ASSERT(RawBodyUtf8(state->responses[5]->body) == L"ordinary-options");
				TEST_ASSERT(FindField(state->responses[5]->headers, L"content-length") == L"16");
				TEST_ASSERT(FindField(state->responses[5]->headers, L"access-control-allow-origin") == L"*");
				TEST_ASSERT(FindField(state->responses[5]->headers, L"date") != WString::Empty);
				TEST_ASSERT(FindField(state->responses[5]->headers, L"cache-control") == L"no-store");
				TEST_ASSERT(state->responses[6]->statusCode == 214);
				TEST_ASSERT(RawBodyUtf8(state->responses[6]->body) == WString::Empty);
				TEST_ASSERT(FindField(state->responses[6]->headers, L"content-length") == L"9");
				TEST_ASSERT(state->responses[7]->statusCode == 204);
				TEST_ASSERT(RawBodyUtf8(state->responses[7]->body) == WString::Empty);
				TEST_ASSERT(CountField(state->responses[7]->headers, L"content-length") == 0);
				TEST_ASSERT(state->responses[8]->statusCode == 304);
				TEST_ASSERT(RawBodyUtf8(state->responses[8]->body) == WString::Empty);
				TEST_ASSERT(FindField(state->responses[8]->headers, L"content-length") == L"0");
			}
			TEST_ASSERT(state->writes == 9);
			TEST_ASSERT(serverState->serverRequests == 4);
			TEST_ASSERT(serverState->stoppingCallbacks == 1);
			AssertState(*serverState.Obj());
		});

		TEST_CASE(L"SocketHttpRequestContext supports deferred one-shot responses and callback-reentrant FIFO submission")
		{
			NativeListenerFactoryScope<TNativeServer> listenerFactory;
			auto state = Ptr(new DeferredState(3));
			auto server = Ptr(new TestServerApi(L"http://localhost:38906/deferred", false, state));
			server->SetHandler(Func<void(Ptr<SocketHttpRequestContext>)>([state](Ptr<SocketHttpRequestContext> context)
			{
				state->AddTarget(context->GetRequest()->requestTarget);
				if (context->GetRelativePath() == L"/first")
				{
					state->SetContext(context);
					return;
				}
				vint status = context->GetRelativePath() == L"/second" ? 222 : 223;
				state->Expect(context->Respond(CreateResponse(status, context->GetRelativePath()), Func<void(bool)>([state](bool succeeded)
				{
					if (succeeded) state->successfulCompletions++;
					else state->failedCompletions++;
				})), L"A queued deferred/FIFO response was rejected.");
			}));
			server->Start();
			auto client = CreateConnectedClient<TNativeClient>(38906, state);

			windows_http::HttpRequest first;
			first.method = L"GET";
			first.query = L"/deferred/first";
			client->HttpQuery(first, Func<void(Variant<windows_http::HttpResponse, windows_http::HttpError>)>([client, state](auto result)
			{
				try
				{
					state->AddResult(std::move(result));
					windows_http::HttpRequest third;
					third.method = L"GET";
					third.query = L"/deferred/third";
					SubmitQuery(client, state, third);
				}
				catch (...)
				{
					RecordCurrentException(*state.Obj(), L"Reentrant MiniHttp response callback");
					state->eventDone.Signal();
				}
			}));
			windows_http::HttpRequest second;
			second.method = L"GET";
			second.query = L"/deferred/second";
			SubmitQuery(client, state, second);

			state->Expect(state->eventContext.WaitForTime(TransferTimeout), L"The deferred MiniHttp handler did not expose its context.");
			auto context = state->GetContext();
			TEST_ASSERT(context);
			if (context)
			{
				auto transferEncoding = CreateResponse(221, L"invalid-transfer-encoding");
				transferEncoding->headers.Add(CreateField(L"Transfer-Encoding", L"chunked"));
				TEST_ERROR(context->Respond(transferEncoding));

				auto contradictoryLength = CreateResponse(221, L"invalid-length");
				contradictoryLength->headers.Add(CreateField(L"Content-Length", L"999"));
				TEST_ERROR(context->Respond(contradictoryLength));

				auto trailers = CreateResponse(221, L"invalid-trailer");
				trailers->body.trailers.Add(CreateField(L"x-trailer", L"value"));
				TEST_ERROR(context->Respond(trailers));

				auto accepted = context->Respond(CreateResponse(221, L"deferred-first"), Func<void(bool)>([state](bool succeeded)
				{
					if (succeeded) state->successfulCompletions++;
					else state->failedCompletions++;
					state->eventCompletion.Signal();
				}));
				TEST_ASSERT(accepted);
				TEST_ASSERT(!context->Respond(CreateResponse(299, L"second-response")));
				TEST_ASSERT(!context->Cancel());
			}

			state->Expect(state->eventDone.WaitForTime(TransferTimeout), L"The deferred/reentrant MiniHttp sequence timed out.");
			state->Expect(state->eventCompletion.WaitForTime(TransferTimeout), L"The deferred MiniHttp write completion did not run.");
			client->Stop();
			state->stopReturned = 1;
			server->Stop();

			auto records = state->CopyRecords();
			TEST_ASSERT(records.Count() == 3);
			if (records.Count() == 3)
			{
				TEST_ASSERT(!records[0].isError && records[0].statusCode == 221 && records[0].body == L"deferred-first");
				TEST_ASSERT(!records[1].isError && records[1].statusCode == 222 && records[1].body == L"/second");
				TEST_ASSERT(!records[2].isError && records[2].statusCode == 223 && records[2].body == L"/third");
			}
			auto targets = state->CopyTargets();
			TEST_ASSERT(targets.Count() == 3);
			if (targets.Count() == 3)
			{
				TEST_ASSERT(targets[0] == L"/deferred/first");
				TEST_ASSERT(targets[1] == L"/deferred/second");
				TEST_ASSERT(targets[2] == L"/deferred/third");
			}
			TEST_ASSERT(state->successfulCompletions == 3);
			TEST_ASSERT(state->failedCompletions == 0);
			TEST_ASSERT(state->callbacksAfterStop == 0);
			AssertState(*state.Obj());
		});

		TEST_CASE(L"SocketHttpRequestContext cancellation and client/server Stop hard-drain accepted callbacks")
		{
			NativeListenerFactoryScope<TNativeServer> listenerFactory;
			auto serverState = Ptr(new TestState);
			auto cancelState = Ptr(new DeferredState(1));
			auto clientStopState = Ptr(new DeferredState(2));
			auto serverStopState = Ptr(new DeferredState(1));
			auto server = Ptr(new TestServerApi(L"http://localhost:38907/lifecycle", false, serverState));
			server->SetHandler(Func<void(Ptr<SocketHttpRequestContext>)>([serverState, cancelState, clientStopState, serverStopState](Ptr<SocketHttpRequestContext> context)
			{
				auto relative = context->GetRelativePath();
				serverState->AddTarget(relative);
				if (relative == L"/cancel") cancelState->SetContext(context);
				else if (relative == L"/client-stop") clientStopState->SetContext(context);
				else if (relative == L"/server-stop") serverStopState->SetContext(context);
				else
				{
					serverState->Fail(L"A queued request reached the server after a hard client stop.");
					context->Cancel();
				}
			}));
			server->Start();

			auto cancelClient = CreateConnectedClient<TNativeClient>(38907, cancelState);
			windows_http::HttpRequest cancelRequest;
			cancelRequest.method = L"GET";
			cancelRequest.query = L"/lifecycle/cancel";
			SubmitQuery(cancelClient, cancelState, cancelRequest);
			cancelState->Expect(cancelState->eventContext.WaitForTime(TransferTimeout), L"The explicit-cancel context was not delivered.");
			auto cancelContext = cancelState->GetContext();
			TEST_ASSERT(cancelContext);
			if (cancelContext)
			{
				TEST_ASSERT(cancelContext->Cancel());
				TEST_ASSERT(!cancelContext->Cancel());
				TEST_ASSERT(!cancelContext->Respond(CreateResponse(200, L"too-late")));
			}
			cancelState->Expect(cancelState->eventDone.WaitForTime(TransferTimeout), L"Explicit context cancellation did not fail the client query.");
			cancelClient->Stop();

			auto clientStopClient = CreateConnectedClient<TNativeClient>(38907, clientStopState);
			windows_http::HttpRequest heldRequest;
			heldRequest.method = L"GET";
			heldRequest.query = L"/lifecycle/client-stop";
			SubmitQuery(clientStopClient, clientStopState, heldRequest);
			windows_http::HttpRequest queuedRequest;
			queuedRequest.method = L"GET";
			queuedRequest.query = L"/lifecycle/must-not-arrive";
			SubmitQuery(clientStopClient, clientStopState, queuedRequest);
			clientStopState->Expect(clientStopState->eventContext.WaitForTime(TransferTimeout), L"The client-stop context was not delivered.");
			clientStopClient->Stop();
			clientStopState->stopReturned = 1;
			clientStopState->Expect(clientStopState->eventDone.WaitForTime(TransferTimeout), L"SocketHttpClientApi::Stop did not complete all accepted callbacks.");

			auto serverStopClient = CreateConnectedClient<TNativeClient>(38907, serverStopState);
			windows_http::HttpRequest serverStopRequest;
			serverStopRequest.method = L"GET";
			serverStopRequest.query = L"/lifecycle/server-stop";
			SubmitQuery(serverStopClient, serverStopState, serverStopRequest);
			serverStopState->Expect(serverStopState->eventContext.WaitForTime(TransferTimeout), L"The server-stop context was not delivered.");
			server->Stop();
			serverStopState->Expect(serverStopState->eventDone.WaitForTime(TransferTimeout), L"SocketHttpServerApi::Stop did not disconnect the retained request.");
			serverStopClient->Stop();
			serverStopState->stopReturned = 1;

			auto cancelRecords = cancelState->CopyRecords();
			auto clientStopRecords = clientStopState->CopyRecords();
			auto serverStopRecords = serverStopState->CopyRecords();
			TEST_ASSERT(cancelRecords.Count() == 1 && cancelRecords[0].isError);
			TEST_ASSERT(clientStopRecords.Count() == 2);
			if (clientStopRecords.Count() == 2)
			{
				TEST_ASSERT(clientStopRecords[0].isError);
				TEST_ASSERT(clientStopRecords[1].isError);
			}
			TEST_ASSERT(serverStopRecords.Count() == 1 && serverStopRecords[0].isError);
			TEST_ASSERT(clientStopState->callbacksAfterStop == 0);
			TEST_ASSERT(serverStopState->callbacksAfterStop == 0);
			TEST_ASSERT(serverState->serverRequests == 3);
			TEST_ASSERT(serverState->stoppingCallbacks == 1);
			TEST_ASSERT(server->IsStopped());
			AssertState(*cancelState.Obj());
			AssertState(*clientStopState.Obj());
			AssertState(*serverStopState.Obj());
			AssertState(*serverState.Obj());
		});

		TEST_CASE(L"SocketHttpClientApi maps receiveTimeout to a per-exchange response deadline")
		{
			NativeListenerFactoryScope<TNativeServer> listenerFactory;
			auto state = Ptr(new DeferredState(1));
			auto server = Ptr(new TestServerApi(L"http://localhost:38912/timeout", false, state));
			server->SetHandler(Func<void(Ptr<SocketHttpRequestContext>)>([state](Ptr<SocketHttpRequestContext> context)
			{
				state->SetContext(context);
			}));
			server->Start();
			auto client = CreateConnectedClient<TNativeClient>(38912, state);
			windows_http::HttpRequest request;
			request.method = L"GET";
			request.query = L"/timeout/retained";
			request.receiveTimeout = 100;
			SubmitQuery(client, state, request);
			state->Expect(state->eventContext.WaitForTime(TransferTimeout), L"The response-deadline request did not reach the server.");
			state->Expect(state->eventDone.WaitForTime(ConnectTimeout), L"SocketHttpClientApi did not report its response deadline.");
			client->Stop();
			server->Stop();

			auto records = state->CopyRecords();
			TEST_ASSERT(records.Count() == 1);
			if (records.Count() == 1)
			{
				TEST_ASSERT(records[0].isError);
				TEST_ASSERT(records[0].errorCode != 0);
				TEST_ASSERT(records[0].operation != WString::Empty);
			}
			auto context = state->GetContext();
			TEST_ASSERT(context);
			if (context) TEST_ASSERT(!context->Cancel());
			AssertState(*state.Obj());
		});
	}

#if defined VCZH_MSVC

	bool SameBytes(const HttpBody& body, const vuint8_t* expected, vint expectedSize)
	{
		vint actualSize = 0;
		for (auto&& chunk : body.chunks) actualSize += chunk.data.Count();
		if (actualSize != expectedSize) return false;
		vint offset = 0;
		for (auto&& chunk : body.chunks)
		{
			for (auto value : chunk.data)
			{
				if (value != expected[offset++]) return false;
			}
		}
		return true;
	}

	bool SameChars(const Array<char>& actual, const vuint8_t* expected, vint expectedSize)
	{
		if (actual.Count() != expectedSize) return false;
		for (vint i = 0; i < expectedSize; i++)
		{
			if ((vuint8_t)actual[i] != expected[i]) return false;
		}
		return true;
	}

	bool SameAscii(const char* actual, vint actualLength, const char* expected)
	{
		auto expectedLength = (vint)strlen(expected);
		if (!actual || actualLength != expectedLength) return false;
		for (vint i = 0; i < actualLength; i++)
		{
			if (actual[i] != expected[i]) return false;
		}
		return true;
	}

	bool HasUnknownHeader(PHTTP_REQUEST request, const char* name, const char* value)
	{
		for (USHORT i = 0; i < request->Headers.UnknownHeaderCount; i++)
		{
			auto&& header = request->Headers.pUnknownHeaders[i];
			if (
				SameAscii(header.pName, header.NameLength, name) &&
				SameAscii(header.pRawValue, header.RawValueLength, value)
				)
			{
				return true;
			}
		}
		return false;
	}

	class WindowsToSocketServerApi : public TestServerApi
	{
	public:
		WindowsToSocketServerApi(Ptr<TestState> state)
			: TestServerApi(L"http://localhost:38901/vlppos-mini-http/", false, state)
		{
			SetHandler(Func<void(Ptr<SocketHttpRequestContext>)>([state](Ptr<SocketHttpRequestContext> context)
			{
				try
				{
					auto request = context->GetRequest();
					const vuint8_t expectedRequest[] = { 'A', 0, 'B', 0xFF, '\r', '\n' };
					state->Expect(request->version.major == 1 && request->version.minor == 1, L"WinHTTP did not send HTTP/1.1 to SocketHttpServerApi.");
					state->Expect(request->method == L"POST", L"WinHTTP changed the MiniHttp POST method.");
					state->Expect(request->requestTarget == L"/vlppos-mini-http/submit?raw=%2F", L"WinHTTP changed the MiniHttp raw target.");
					state->Expect(context->GetRelativePath() == L"/submit", L"SocketHttpServerApi computed the wrong WinHTTP relative path.");
					state->Expect(context->GetQuery() == L"raw=%2F", L"SocketHttpServerApi changed the WinHTTP raw query.");
					state->Expect(FindField(request->headers, L"x-mixed-field") == L"mixed-value", L"SocketHttpServerApi lost a mixed-case WinHTTP field.");
					state->Expect(FindField(request->headers, L"content-type") == L"application/octet-stream", L"SocketHttpServerApi lost the WinHTTP content type.");
					state->Expect(FindField(request->headers, L"content-length") == L"6", L"SocketHttpServerApi did not receive fixed WinHTTP framing.");
					state->Expect(SameBytes(request->body, expectedRequest, 6), L"SocketHttpServerApi changed the binary WinHTTP request body.");

					auto response = Ptr(new async_tcp_socket::HttpResponse);
					response->statusCode = 298;
					response->reason = L"MiniHttp Interop";
					response->headers.Add(CreateField(L"Content-Type", L"application/octet-stream"));
					const vuint8_t responsePart1[] = { 'R', 0 };
					const vuint8_t responsePart2[] = { 'S', 0xFE };
					HttpBodyChunk chunk1;
					chunk1.data.Resize(2);
					HttpBodyChunk chunk2;
					chunk2.data.Resize(2);
					for (vint i = 0; i < 2; i++)
					{
						chunk1.data[i] = responsePart1[i];
						chunk2.data[i] = responsePart2[i];
					}
					response->body.chunks.Add(std::move(chunk1));
					response->body.chunks.Add(std::move(chunk2));
					state->Expect(context->Respond(response, Func<void(bool)>([state](bool succeeded)
					{
						state->Expect(succeeded, L"The WinHTTP interop response did not reach write completion.");
						state->eventCompletion.Signal();
					})), L"SocketHttpServerApi rejected the WinHTTP interop response.");
				}
				catch (...)
				{
					RecordCurrentException(*state.Obj(), L"WinHTTP-to-SocketHttpServerApi handler");
					context->Cancel();
				}
			}));
		}
	};

	void RunWindowsHttpClientToSocketHttpServerApi()
	{
		auto state = Ptr(new TestState);
		auto server = Ptr(new WindowsToSocketServerApi(state));
		TEST_ASSERT(server->GetUrlPrefix() == L"http://localhost:38901/vlppos-mini-http");
		server->Start();

		windows_http::HttpClientApi client(L"localhost", 38901);
		windows_http::HttpRequest request;
		request.method = L"POST";
		request.query = L"/vlppos-mini-http/submit?raw=%2F";
		request.contentType = L"application/octet-stream";
		request.extraHeaders.Add(L"X-MiXeD-Field", L"mixed-value");
		request.body.Resize(6);
		const vuint8_t requestBody[] = { 'A', 0, 'B', 0xFF, '\r', '\n' };
		for (vint i = 0; i < 6; i++) request.body[i] = (char)requestBody[i];
		client.HttpQuery(request, Func<void(Variant<windows_http::HttpResponse, windows_http::HttpError>)>([state](auto result)
		{
			SignalEventOnExit signal(state->eventDone);
			try
			{
				if (auto error = result.TryGet<windows_http::HttpError>())
				{
					state->Fail(L"WinHTTP-to-SocketHttpServerApi query failed: " + error->operation + L": " + error->message);
					return;
				}
				auto&& response = result.Get<windows_http::HttpResponse>();
				const vuint8_t expected[] = { 'R', 0, 'S', 0xFE };
				state->Expect(response.statusCode == 298, L"WinHTTP lost the non-default MiniHttp response status.");
				state->Expect(response.contentType == L"application/octet-stream", L"WinHTTP lost the MiniHttp response content type.");
				state->Expect(SameChars(response.body, expected, 4), L"WinHTTP changed the binary MiniHttp response body.");
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"Validating WinHTTP-to-SocketHttpServerApi response");
			}
		}));

		state->Expect(state->eventDone.WaitForTime(TransferTimeout), L"WinHTTP-to-SocketHttpServerApi interop timed out.");
		state->Expect(state->eventCompletion.WaitForTime(TransferTimeout), L"WinHTTP-to-SocketHttpServerApi completion timed out.");
		client.Stop();
		server->Stop();
		TEST_ASSERT(server->IsStopped());
		TEST_ASSERT(state->serverRequests == 1);
		AssertState(*state.Obj());
	}

	class SocketToWindowsServerApi : public windows_http::HttpServerApi
	{
	private:
		Ptr<TestState> state;

	protected:
		void OnHttpRequestReceived(PHTTP_REQUEST request) override
		{
			SignalEventOnExit signal(state->eventContext);
			try
			{
				state->serverRequests++;
				state->Expect(request->Version.MajorVersion == 1 && request->Version.MinorVersion == 1, L"SocketHttpClientApi did not send HTTP/1.1 to HTTP.sys.");
				state->Expect(request->Verb == HttpVerbPOST, L"SocketHttpClientApi did not preserve POST for HTTP.sys.");
				state->Expect(SameAscii(request->pRawUrl, request->RawUrlLength, "/vlppos-mini-http/submit?raw=%2F"), L"SocketHttpClientApi changed the HTTP.sys raw target.");
				auto&& host = request->Headers.KnownHeaders[HttpHeaderHost];
				state->Expect(SameAscii(host.pRawValue, host.RawValueLength, "localhost:38902"), L"SocketHttpClientApi sent the wrong HTTP.sys Host field.");
				auto&& encoding = request->Headers.KnownHeaders[HttpHeaderAcceptEncoding];
				state->Expect(SameAscii(encoding.pRawValue, encoding.RawValueLength, "identity"), L"SocketHttpClientApi did not request identity encoding from HTTP.sys.");
				auto&& contentType = request->Headers.KnownHeaders[HttpHeaderContentType];
				state->Expect(SameAscii(contentType.pRawValue, contentType.RawValueLength, "application/json; charset=utf8"), L"SocketHttpClientApi lost the HTTP.sys content type.");
				auto&& contentLength = request->Headers.KnownHeaders[HttpHeaderContentLength];
				state->Expect(SameAscii(contentLength.pRawValue, contentLength.RawValueLength, "17"), L"SocketHttpClientApi did not send fixed HTTP.sys framing.");
				state->Expect(HasUnknownHeader(request, "x-mini-interop", "socket-value"), L"SocketHttpClientApi lost a custom HTTP.sys field.");
				auto body = GetUtf8Body(request);
				state->Expect(body && body.Value() == L"socket-body-bytes", L"HTTP.sys received the wrong SocketHttpClientApi body.");
				auto result = SendResponse(
					GetHttpRequestQueue(),
					request->RequestId,
					{ 299, L"Interop Result", L"http-sys-body", L"text/plain; charset=utf-8" }
					);
				state->Expect(result == NO_ERROR, L"HTTP.sys failed to send the MiniHttp interop response.");
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"SocketHttpClientApi-to-HTTP.sys handler");
			}
		}

	public:
		SocketToWindowsServerApi(Ptr<TestState> _state)
			: windows_http::HttpServerApi(L"http://localhost:38902/vlppos-mini-http/", false)
			, state(_state)
		{
		}

		~SocketToWindowsServerApi()
		{
			Stop();
		}
	};

	void RunSocketHttpClientToWindowsHttpServerApi()
	{
		auto state = Ptr(new TestState(1));
		SocketToWindowsServerApi server(state);
		server.Start();
		auto client = CreateConnectedClient<windows_socket::AsyncSocketClient>(38902, state);

		windows_http::HttpRequest request;
		request.method = L"POST";
		request.query = L"/vlppos-mini-http/submit?raw=%2F";
		request.contentType = L"application/json; charset=utf8";
		request.extraHeaders.Add(L"X-Mini-Interop", L"socket-value");
		request.SetBodyUtf8(L"socket-body-bytes");
		request.receiveTimeout = 2000;
		SubmitQuery(client, state, request);
		state->Expect(state->eventContext.WaitForTime(TransferTimeout), L"HTTP.sys did not receive the SocketHttpClientApi request.");
		state->Expect(state->eventDone.WaitForTime(ConnectTimeout), L"SocketHttpClientApi did not receive the HTTP.sys response.");
		client->Stop();
		server.Stop();

		AssertState(*state.Obj());
		auto records = state->CopyRecords();
		TEST_ASSERT(records.Count() == 1);
		if (records.Count() == 1)
		{
			if (records[0].isError)
			{
				auto error = L"SocketHttpClientApi-to-HTTP.sys query failed: " + records[0].operation + L": " + records[0].message;
				state->Fail(error);
				TEST_PRINT(error);
			}
			TEST_ASSERT(!records[0].isError);
			TEST_ASSERT(records[0].statusCode == 299);
			TEST_ASSERT(records[0].body == L"http-sys-body");
			TEST_ASSERT(records[0].contentType == L"text/plain; charset=utf-8");
		}
		TEST_ASSERT(server.IsStopped());
		TEST_ASSERT(state->serverRequests == 1);
	}

#endif
}

using namespace mini_http_api_test;

TEST_FILE
{
	TEST_CASE(L"SocketHttpServerApi validates and normalizes loopback URL prefixes")
	{
		auto root = CreateValidationServer(L"http://LOCALHOST:38910////");
		TEST_ASSERT(root->GetUrlPrefix() == L"http://localhost:38910");
		root->Stop();

		auto unicode = CreateValidationServer(L"http://127.0.0.1:38910/%E4%BD%A0///");
		TEST_ASSERT(unicode->GetUrlPrefix() == L"http://127.0.0.1:38910/%E4%BD%A0");
		unicode->Stop();

		TEST_ERROR(CreateValidationServer(L"https://localhost:38910/api"));
		TEST_ERROR(CreateValidationServer(L"http://example.com:38910/api"));
		TEST_ERROR(CreateValidationServer(L"http://localhost/api"));
		TEST_ERROR(CreateValidationServer(L"http://localhost:0/api"));
		TEST_ERROR(CreateValidationServer(L"http://localhost:65536/api"));
		TEST_ERROR(CreateValidationServer(L"http://user@localhost:38910/api"));
		TEST_ERROR(CreateValidationServer(L"http://localhost:38910/api?query"));
		TEST_ERROR(CreateValidationServer(L"http://localhost:38910/api#fragment"));
		TEST_ERROR(CreateValidationServer(L"http://localhost:38910/api%2Fchild"));
		TEST_ERROR(CreateValidationServer(L"http://localhost:38910/api%5cchild"));
		TEST_ERROR(CreateValidationServer(L"http://localhost:38910/api%00child"));
		TEST_ERROR(CreateValidationServer(L"http://localhost:38910/api%ZZ"));
		TEST_ERROR(CreateValidationServer(L"http://localhost:38910/%C0%AF"));
	});

#if defined VCZH_MSVC
	using namespace vl::inter_process::async_tcp_socket::windows_socket;
	RunCrossPlatformMiniHttpApiTestCases<AsyncSocketServer, AsyncSocketClient>();
#elif defined VCZH_GCC && defined VCZH_APPLE
	using namespace vl::inter_process::async_tcp_socket::macos_socket;
	RunCrossPlatformMiniHttpApiTestCases<AsyncSocketServer, AsyncSocketClient>();
#elif defined VCZH_GCC
	using namespace vl::inter_process::async_tcp_socket::linux_socket;
	RunCrossPlatformMiniHttpApiTestCases<AsyncSocketServer, AsyncSocketClient>();
#endif

#if defined VCZH_MSVC
	TEST_CASE(L"Windows HttpClientApi interoperates with SocketHttpServerApi")
	{
		RunWindowsHttpClientToSocketHttpServerApi();
	});

	TEST_CASE(L"SocketHttpClientApi interoperates with Windows HttpServerApi")
	{
		RunSocketHttpClientToWindowsHttpServerApi();
	});
#endif
}
