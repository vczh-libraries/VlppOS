#include "../../Source/InterProcess/AsyncSocket/AsyncSocket_HttpRequestClient.h"
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket_HttpRequestServer.h"
#include "../../Source/Threading.h"

#if defined VCZH_MSVC
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.Windows.h"
#include "../../Source/InterProcess/Windows/HttpClientApi.Windows.h"
#include "../../Source/InterProcess/Windows/HttpServerApi.Windows.h"
#elif defined VCZH_GCC && defined VCZH_APPLE
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.macOS.h"
#elif defined VCZH_GCC && !defined VCZH_APPLE
#include "../../Source/InterProcess/AsyncSocket/AsyncSocket.Linux.h"
#endif

using namespace vl;
using namespace vl::collections;
using namespace vl::inter_process;
using namespace vl::inter_process::async_tcp_socket;

namespace http_request_test
{
	constexpr vint ConnectTimeout = 10000;
	constexpr vint TransferTimeout = 30000;

	template<size_t N>
	Array<vuint8_t> ByteArray(const char(&text)[N])
	{
		Array<vuint8_t> result((vint)N - 1);
		for (vint i = 0; i < result.Count(); i++)
		{
			result[i] = (vuint8_t)text[i];
		}
		return result;
	}

	template<size_t N>
	void AppendBytes(List<vuint8_t>& output, const char(&text)[N])
	{
		for (vint i = 0; i < (vint)N - 1; i++)
		{
			output.Add((vuint8_t)text[i]);
		}
	}

	Array<vuint8_t> ToByteArray(const List<vuint8_t>& input)
	{
		Array<vuint8_t> result(input.Count());
		for (vint i = 0; i < input.Count(); i++)
		{
			result[i] = input[i];
		}
		return result;
	}

	bool SameBytes(const Array<vuint8_t>& first, const Array<vuint8_t>& second)
	{
		if (first.Count() != second.Count())
		{
			return false;
		}
		for (vint i = 0; i < first.Count(); i++)
		{
			if (first[i] != second[i])
			{
				return false;
			}
		}
		return true;
	}

	template<size_t N>
	bool SameBytes(const Array<vuint8_t>& actual, const char(&expected)[N])
	{
		auto expectedBytes = ByteArray(expected);
		return SameBytes(actual, expectedBytes);
	}

	template<size_t N>
	void AddField(List<HttpField>& fields, const WString& name, const char(&value)[N])
	{
		HttpField field;
		field.name = name;
		field.value = ByteArray(value);
		fields.Add(std::move(field));
	}

	void AddField(List<HttpField>& fields, const WString& name, Array<vuint8_t>&& value)
	{
		HttpField field;
		field.name = name;
		field.value = std::move(value);
		fields.Add(std::move(field));
	}

	template<size_t N>
	void AddChunk(HttpBody& body, const char(&value)[N])
	{
		HttpBodyChunk chunk;
		chunk.data = ByteArray(value);
		body.chunks.Add(std::move(chunk));
	}

	void AddChunk(HttpBody& body, Array<vuint8_t>&& value)
	{
		HttpBodyChunk chunk;
		chunk.data = std::move(value);
		body.chunks.Add(std::move(chunk));
	}

	bool IsLowercaseFieldName(const WString& name)
	{
		for (vint i = 0; i < name.Length(); i++)
		{
			if (L'A' <= name[i] && name[i] <= L'Z')
			{
				return false;
			}
		}
		return true;
	}

	template<size_t N>
	bool HasField(const List<HttpField>& fields, const WString& name, const char(&value)[N])
	{
		for (auto&& field : fields)
		{
			if (field.name == name && SameBytes(field.value, value))
			{
				return true;
			}
		}
		return false;
	}

	vint CountField(const List<HttpField>& fields, const WString& name)
	{
		vint count = 0;
		for (auto&& field : fields)
		{
			if (field.name == name)
			{
				count++;
			}
		}
		return count;
	}

	Array<vuint8_t> FlattenBody(const HttpBody& body)
	{
		vint count = 0;
		for (auto&& chunk : body.chunks)
		{
			count += chunk.data.Count();
		}

		Array<vuint8_t> result(count);
		vint offset = 0;
		for (auto&& chunk : body.chunks)
		{
			for (vint i = 0; i < chunk.data.Count(); i++)
			{
				result[offset++] = chunk.data[i];
			}
		}
		return result;
	}

	class FakeAsyncSocketConnection : public Object, public virtual IAsyncSocketConnection
	{
	private:
		IAsyncSocketCallback*			callback = nullptr;
		vint						completionIndex = 0;
		bool						disconnected = false;

	public:
		List<Ptr<AsyncSocketBuffer>>	writes;
		Func<void()>					beforeWrite;
		Func<void()>					stopAction;
		bool						completeWritesImmediately = false;
		bool						readingStarted = false;
		vint						stopCount = 0;
		vint						installedCallbackCount = 0;
		vint						uninstalledCallbackCount = 0;

		void InstallCallback(IAsyncSocketCallback* value) override
		{
			callback = value;
			if (callback)
			{
				installedCallbackCount++;
				callback->OnInstalled(this);
			}
			else
			{
				uninstalledCallbackCount++;
			}
		}

		void BeginReadingLoopUnsafe() override
		{
			readingStarted = true;
		}

		void WriteAsync(Ptr<AsyncSocketBuffer> buffer) override
		{
			if (beforeWrite)
			{
				beforeWrite();
			}
			writes.Add(buffer);
			if (completeWritesImmediately)
			{
				completionIndex++;
				callback->OnWriteCompleted(buffer);
			}
		}

		void Stop() override
		{
			stopCount++;
			if (stopAction)
			{
				stopAction();
			}
			else
			{
				DisconnectFromPeer();
			}
		}

		void DisconnectFromPeer()
		{
			if (!disconnected)
			{
				disconnected = true;
				if (callback)
				{
					callback->OnDisconnected();
				}
			}
		}

		void Feed(const Array<vuint8_t>& data)
		{
			CHECK_ERROR(callback != nullptr, L"The fake socket callback has not been installed.");
			CHECK_ERROR(data.Count() > 0, L"The fake socket only delivers positive read blocks.");
			callback->OnRead(&data[0], data.Count());
		}

		void CompleteNextWrite()
		{
			CHECK_ERROR(completionIndex < writes.Count(), L"The fake socket has no pending write.");
			auto buffer = writes[completionIndex++];
			callback->OnWriteCompleted(buffer);
		}

		void RaiseError(const WString& error, bool fatal)
		{
			CHECK_ERROR(callback != nullptr, L"The fake socket callback has not been installed.");
			callback->OnError(error, fatal);
		}

		bool HasCallback()
		{
			return callback != nullptr;
		}
	};

	class FakeAsyncSocketServer : public Object, public virtual IAsyncSocketServer
	{
	private:
		IAsyncSocketServerCallback*		callback = nullptr;
		bool						stopped = true;

	public:
		vint						startCount = 0;
		vint						stopCount = 0;
		bool						callbackPresentDuringFirstStop = false;

		void Start(IAsyncSocketServerCallback* value) override
		{
			CHECK_ERROR(value != nullptr, L"The fake async socket server requires a callback.");
			CHECK_ERROR(startCount == 0, L"The fake async socket server can only start once.");
			startCount++;
			callback = value;
			stopped = false;
		}

		void Stop() override
		{
			stopCount++;
			if (stopCount == 1)
			{
				callbackPresentDuringFirstStop = callback != nullptr;
			}
			stopped = true;
			callback = nullptr;
		}

		bool IsStopped() override
		{
			return stopped;
		}

		WaitForClientResult Accept(IAsyncSocketConnection* connection)
		{
			CHECK_ERROR(callback != nullptr && !stopped, L"The fake async socket server is not accepting clients.");
			return callback->OnClientConnected(connection);
		}

		void FailUnexpected()
		{
			CHECK_ERROR(callback != nullptr && !stopped, L"The fake async socket server is not running.");
			auto installed = callback;
			installed->OnServerStopped();
			if (!stopped)
			{
				Stop();
			}
		}

		bool HasCallback()
		{
			return callback != nullptr;
		}
	};

	class ManualTimeoutController : public Object, public virtual IHttpRequestTimeoutController
	{
	private:
		Func<void()>					callback;

	public:
		vint						armCount = 0;
		vint						refreshCount = 0;
		vint						cancelCount = 0;
		vint						lastArmDuration = 0;
		bool						failArm = false;
		bool						failRefresh = false;

		void Arm(vint milliseconds, const Func<void()>& value) override
		{
			armCount++;
			lastArmDuration = milliseconds;
			CHECK_ERROR(!failArm, L"The manual HTTP timeout was configured to fail while arming.");
			callback = value;
		}

		void Refresh() override
		{
			refreshCount++;
			CHECK_ERROR(!failRefresh, L"The manual HTTP timeout was configured to fail while refreshing.");
		}

		void CancelAndWait() override
		{
			cancelCount++;
			callback = Func<void()>();
		}

		void Fire()
		{
			auto firing = callback;
			callback = Func<void()>();
			CHECK_ERROR((bool)firing, L"The manual HTTP timeout is not armed.");
			firing();
		}

		bool TryFire()
		{
			auto firing = callback;
			callback = Func<void()>();
			if (!firing)
			{
				return false;
			}
			firing();
			return true;
		}

		bool HasCallback()
		{
			return (bool)callback;
		}
	};

	class BlockingFirstCancelTimeoutController : public Object, public virtual IHttpRequestTimeoutController
	{
	private:
		CriticalSection					lockState;
		Func<void()>					callback;

	public:
		EventObject						eventFirstCancelEntered;
		EventObject						eventReleaseFirstCancel;
		atomic_vint					cancelCount = 0;

		BlockingFirstCancelTimeoutController()
		{
			CHECK_ERROR(eventFirstCancelEntered.CreateManualUnsignal(false), L"Failed to create the first-cancel-entered event.");
			CHECK_ERROR(eventReleaseFirstCancel.CreateManualUnsignal(false), L"Failed to create the first-cancel-release event.");
		}

		void Arm(vint, const Func<void()>& value) override
		{
			CS_LOCK(lockState)
			{
				callback = value;
			}
		}

		void Refresh() override
		{
		}

		void CancelAndWait() override
		{
			auto index = ++cancelCount;
			CS_LOCK(lockState)
			{
				callback = Func<void()>();
			}
			if (index == 1)
			{
				eventFirstCancelEntered.Signal();
				CHECK_ERROR(eventReleaseFirstCancel.WaitForTime(ConnectTimeout), L"The coordinated HTTP timeout cancellation was not released before its deadline.");
			}
		}
	};

	class RacingTimeoutController : public Object, public virtual IHttpRequestTimeoutController
	{
	private:
		static thread_local RacingTimeoutController*
									currentFiring;
		CriticalSection					lockState;
		Func<void()>						callback;
		bool							firing = false;

		void FinishFiring()
		{
			CS_LOCK(lockState)
			{
				firing = false;
			}
			eventFiringFinished.Signal();
		}

	public:
		EventObject						eventFiringFinished;
		EventObject						eventExternalCancelEntered;
		atomic_vint					externalCancelWaits = 0;

		RacingTimeoutController()
		{
			CHECK_ERROR(eventFiringFinished.CreateManualUnsignal(false), L"Failed to create the timeout-firing-finished event.");
			CHECK_ERROR(eventExternalCancelEntered.CreateManualUnsignal(false), L"Failed to create the external-timeout-cancel event.");
		}

		void Arm(vint, const Func<void()>& value) override
		{
			CS_LOCK(lockState)
			{
				CHECK_ERROR(!callback && !firing, L"The racing timeout controller is already armed.");
				callback = value;
			}
		}

		void Refresh() override
		{
		}

		void CancelAndWait() override
		{
			bool wait = false;
			CS_LOCK(lockState)
			{
				callback = Func<void()>();
				wait = firing && currentFiring != this;
			}
			if (wait)
			{
				externalCancelWaits++;
				eventExternalCancelEntered.Signal();
				CHECK_ERROR(eventFiringFinished.WaitForTime(ConnectTimeout), L"The racing timeout callback did not finish before cancellation timed out.");
			}
		}

		void Fire()
		{
			Func<void()> firingCallback;
			CS_LOCK(lockState)
			{
				CHECK_ERROR(callback, L"The racing timeout controller is not armed.");
				firingCallback = callback;
				callback = Func<void()>();
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
		}
	};

	thread_local RacingTimeoutController* RacingTimeoutController::currentFiring = nullptr;

	class EventReleaseOnExit
	{
	private:
		EventObject* eventObject = nullptr;

	public:
		EventReleaseOnExit(EventObject& value)
			: eventObject(&value)
		{
		}

		~EventReleaseOnExit()
		{
			eventObject->Signal();
		}
	};

	class RecordingHttpCallback : public Object, public virtual IHttpRequestCallback
	{
	public:
		IHttpRequestConnection*			connection = nullptr;
		List<Ptr<HttpRequest>>			requests;
		List<HttpRequestFailure>		requestFailures;
		List<Ptr<HttpResponse>>			responses;
		List<WString>					events;
		WString						lastError;
		bool						lastErrorFatal = false;
		vint						writeCompletedCount = 0;
		vint						disconnectedCount = 0;

		void OnInstalled(IHttpRequestConnection* value) override
		{
			connection = value;
			events.Add(L"installed");
		}

		void OnReadRequest(Ptr<HttpRequest> request) override
		{
			requests.Add(request);
			events.Add(L"request");
		}

		void OnReadRequestFailure(HttpRequestFailure failure) override
		{
			requestFailures.Add(failure);
			events.Add(L"request-failure");
		}

		void OnReadResponse(Ptr<HttpResponse> response) override
		{
			responses.Add(response);
			events.Add(L"response");
		}

		void OnWriteCompleted() override
		{
			writeCompletedCount++;
			events.Add(L"write");
		}

		void OnError(const WString& error, bool fatal) override
		{
			lastError = error;
			lastErrorFatal = fatal;
			events.Add(fatal ? L"fatal" : L"nonfatal");
		}

		void OnConnected() override
		{
			events.Add(L"connected");
		}

		void OnDisconnected() override
		{
			disconnectedCount++;
			events.Add(L"disconnected");
		}
	};

	class CoordinatedRecordingHttpCallback : public RecordingHttpCallback
	{
	public:
		EventObject						eventRaceCanRelease;

		CoordinatedRecordingHttpCallback()
		{
			CHECK_ERROR(eventRaceCanRelease.CreateManualUnsignal(false), L"Failed to create the response-race coordination event.");
		}

		void OnError(const WString& error, bool fatal) override
		{
			if (fatal)
			{
				eventRaceCanRelease.Signal();
			}
			RecordingHttpCallback::OnError(error, fatal);
		}
	};

	class BlockingErrorHttpCallback : public RecordingHttpCallback
	{
	public:
		EventObject						eventErrorEntered;
		EventObject						eventReleaseError;

		BlockingErrorHttpCallback()
		{
			CHECK_ERROR(eventErrorEntered.CreateManualUnsignal(false), L"Failed to create the timeout-error-entered event.");
			CHECK_ERROR(eventReleaseError.CreateManualUnsignal(false), L"Failed to create the timeout-error-release event.");
		}

		void OnError(const WString& error, bool fatal) override
		{
			RecordingHttpCallback::OnError(error, fatal);
			eventErrorEntered.Signal();
			CHECK_ERROR(eventReleaseError.WaitForTime(ConnectTimeout), L"The coordinated timeout error callback was not released.");
		}
	};

	class StopInsideRequestCallback : public Object, public virtual IHttpRequestCallback
	{
	private:
		IHttpRequestConnection*			connection = nullptr;

	public:
		vint						requestCount = 0;
		vint						disconnectedCount = 0;
		vint						errorCount = 0;
		vint						callbacksAfterStop = 0;
		bool						stopReturned = false;
		bool						disconnectedBeforeStopReturned = false;

		void OnInstalled(IHttpRequestConnection* value) override
		{
			connection = value;
		}

		void OnReadRequest(Ptr<HttpRequest>) override
		{
			if (stopReturned) callbacksAfterStop++;
			requestCount++;
			connection->Stop();
			disconnectedBeforeStopReturned = disconnectedCount == 1;
			stopReturned = true;
		}

		void OnReadResponse(Ptr<HttpResponse>) override
		{
			if (stopReturned) callbacksAfterStop++;
		}

		void OnWriteCompleted() override
		{
			if (stopReturned) callbacksAfterStop++;
		}

		void OnError(const WString&, bool) override
		{
			if (stopReturned) callbacksAfterStop++;
			errorCount++;
		}

		void OnConnected() override
		{
			if (stopReturned) callbacksAfterStop++;
		}

		void OnDisconnected() override
		{
			if (stopReturned) callbacksAfterStop++;
			disconnectedCount++;
		}
	};

	class FakeHttpRequestServer : public HttpRequestServer
	{
	public:
		bool						acceptClients = false;
		vint						acceptCallbackCount = 0;
		vint						unexpectedStopCount = 0;
		IHttpRequestConnection*			acceptedConnection = nullptr;
		RecordingHttpCallback			acceptedCallback;

		FakeHttpRequestServer(Ptr<IAsyncSocketServer> server, bool accept)
			: HttpRequestServer(server)
			, acceptClients(accept)
		{
		}

		~FakeHttpRequestServer()
		{
			HttpRequestServer::Stop();
		}

		WaitForClientResult OnClientConnected(IHttpRequestConnection* connection) override
		{
			acceptCallbackCount++;
			acceptedConnection = connection;
			if (!acceptClients)
			{
				return WaitForClientResult::Reject;
			}
			connection->InstallCallback(&acceptedCallback);
			connection->BeginReadingLoopUnsafe();
			return WaitForClientResult::Accept;
		}

	protected:
		void OnServerStopped() override
		{
			unexpectedStopCount++;
			HttpRequestServer::OnServerStopped();
		}
	};

	Ptr<HttpResponse> CreateEmptyResponse(vint statusCode = 200, const WString& reason = L"OK")
	{
		auto response = Ptr(new HttpResponse);
		response->statusCode = statusCode;
		response->reason = reason;
		AddField(response->headers, L"content-length", "0");
		return response;
	}

	void AssertFatalInput(HttpRequestConnectionDirection direction, const Array<vuint8_t>& input)
	{
		auto socket = Ptr(new FakeAsyncSocketConnection);
		auto connection = Ptr(new HttpRequestConnection(socket.Obj(), direction));
		RecordingHttpCallback callback;
		connection->InstallCallback(&callback);
		connection->BeginReadingLoopUnsafe();
		if (direction == HttpRequestConnectionDirection::Client)
		{
			auto request = Ptr(new HttpRequest);
			request->method = L"GET";
			request->requestTarget = L"/fatal-input-probe";
			connection->SendRequest(request);
			socket->CompleteNextWrite();
		}
		socket->Feed(input);
		TEST_ASSERT(callback.lastError != L"");
		TEST_ASSERT(callback.lastErrorFatal);
		TEST_ASSERT(callback.disconnectedCount == 1);
		TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
		connection->Stop();
	}

	void AssertRequestFailureInput(const Array<vuint8_t>& input, HttpRequestFailure failure)
	{
		auto socket = Ptr(new FakeAsyncSocketConnection);
		auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
		RecordingHttpCallback callback;
		connection->InstallCallback(&callback);
		connection->BeginReadingLoopUnsafe();
		socket->Feed(input);
		TEST_ASSERT(callback.requests.Count() == 0);
		TEST_ASSERT(callback.requestFailures.Count() == 1);
		TEST_ASSERT(callback.requestFailures[0] == failure);
		TEST_ASSERT(callback.lastError == L"");
		TEST_ASSERT(callback.disconnectedCount == 1);
		TEST_ASSERT(callback.events.IndexOf(L"request-failure") < callback.events.IndexOf(L"disconnected"));
		connection->Stop();
	}

	void RunHttpRequestDataHelperTestCases()
	{
		TEST_CASE(L"HTTP framing analysis exposes authoritative framing and connection state")
		{
			List<HttpField> fields;
			HttpFraming framing;
			framing.kind = HttpFramingKind::Chunked;
			framing.contentLength = 99;
			framing.contentLengthFieldCount = 7;
			framing.contentLengthValueCount = 8;
			framing.contentLengthValuesPlainDecimal = false;
			framing.connectionClose = true;
			TEST_ASSERT(AnalyzeHttpFraming(fields, framing) == HttpFramingAnalysisResult::Succeeded);
			TEST_ASSERT(framing.kind == HttpFramingKind::None);
			TEST_ASSERT(framing.contentLength == 0);
			TEST_ASSERT(framing.contentLengthFieldCount == 0);
			TEST_ASSERT(framing.contentLengthValueCount == 0);
			TEST_ASSERT(framing.contentLengthValuesPlainDecimal);
			TEST_ASSERT(!framing.connectionClose);

			AddField(fields, L"transfer-encoding", "CHUNKED");
			AddField(fields, L"connection", "keep-alive, CLOSE");
			TEST_ASSERT(AnalyzeHttpFraming(fields, framing) == HttpFramingAnalysisResult::Succeeded);
			TEST_ASSERT(framing.kind == HttpFramingKind::Chunked);
			TEST_ASSERT(framing.connectionClose);
		});

		TEST_CASE(L"HTTP framing analysis distinguishes Content-Length fields, values, and spelling")
		{
			HttpFraming framing;
			{
				List<HttpField> fields;
				AddField(fields, L"content-length", "3");
				TEST_ASSERT(AnalyzeHttpFraming(fields, framing) == HttpFramingAnalysisResult::Succeeded);
				TEST_ASSERT(framing.kind == HttpFramingKind::ContentLength && framing.contentLength == 3);
				TEST_ASSERT(framing.contentLengthFieldCount == 1 && framing.contentLengthValueCount == 1);
				TEST_ASSERT(framing.contentLengthValuesPlainDecimal);
			}
			{
				List<HttpField> fields;
				AddField(fields, L"content-length", "003");
				TEST_ASSERT(AnalyzeHttpFraming(fields, framing) == HttpFramingAnalysisResult::Succeeded);
				TEST_ASSERT(framing.contentLength == 3 && framing.contentLengthValuesPlainDecimal);
			}
			{
				List<HttpField> fields;
				AddField(fields, L"content-length", "3, 3");
				TEST_ASSERT(AnalyzeHttpFraming(fields, framing) == HttpFramingAnalysisResult::Succeeded);
				TEST_ASSERT(framing.contentLength == 3);
				TEST_ASSERT(framing.contentLengthFieldCount == 1 && framing.contentLengthValueCount == 2);
				TEST_ASSERT(!framing.contentLengthValuesPlainDecimal);
			}
			{
				List<HttpField> fields;
				AddField(fields, L"content-length", "3");
				AddField(fields, L"content-length", "3");
				TEST_ASSERT(AnalyzeHttpFraming(fields, framing) == HttpFramingAnalysisResult::Succeeded);
				TEST_ASSERT(framing.contentLengthFieldCount == 2 && framing.contentLengthValueCount == 2);
				TEST_ASSERT(framing.contentLengthValuesPlainDecimal);
			}
			{
				List<HttpField> fields;
				AddField(fields, L"content-length", "\t3 ");
				TEST_ASSERT(AnalyzeHttpFraming(fields, framing) == HttpFramingAnalysisResult::Succeeded);
				TEST_ASSERT(framing.contentLength == 3 && !framing.contentLengthValuesPlainDecimal);
			}
			{
				List<HttpField> fields;
				AddField(fields, L"Content-Length", "3");
				TEST_ASSERT(AnalyzeHttpFraming(fields, framing) == HttpFramingAnalysisResult::Succeeded);
				TEST_ASSERT(framing.kind == HttpFramingKind::None);
			}
		});

		TEST_CASE(L"HTTP framing analysis preserves invalid and unsupported results")
		{
			auto analyze = [](const WString& firstName, auto&& firstValue, const WString& secondName, auto&& secondValue)
			{
				List<HttpField> fields;
				AddField(fields, firstName, firstValue);
				if (secondName != L"") AddField(fields, secondName, secondValue);
				HttpFraming framing;
				return AnalyzeHttpFraming(fields, framing);
			};
			TEST_ASSERT(analyze(L"content-length", "3, 4", L"", "") == HttpFramingAnalysisResult::Invalid);
			TEST_ASSERT(analyze(L"content-length", "3", L"content-length", "4") == HttpFramingAnalysisResult::Invalid);
			TEST_ASSERT(analyze(L"content-length", "3", L"transfer-encoding", "chunked") == HttpFramingAnalysisResult::Invalid);
			TEST_ASSERT(analyze(L"content-length", "3", L"transfer-encoding", "gzip") == HttpFramingAnalysisResult::Invalid);
			TEST_ASSERT(analyze(L"transfer-encoding", "chunked;foo", L"", "") == HttpFramingAnalysisResult::Invalid);
			TEST_ASSERT(analyze(L"transfer-encoding", "gzip", L"", "") == HttpFramingAnalysisResult::UnsupportedTransferCoding);
			TEST_ASSERT(analyze(L"transfer-encoding", "gzip, chunked", L"", "") == HttpFramingAnalysisResult::UnsupportedTransferCoding);
			TEST_ASSERT(analyze(L"transfer-encoding", "chunked;foo=bar", L"", "") == HttpFramingAnalysisResult::UnsupportedTransferCoding);
		});

		TEST_CASE(L"HTTP field helpers normalize construction and perform exact lookup")
		{
			List<HttpField> fields;
			fields.Add(CreateAsciiHttpField(L"X-RePeAt", L"first\tvalue"));
			fields.Add(CreateAsciiHttpField(L"x-repeat", L"second"));
			TEST_ASSERT(fields[0].name == L"x-repeat");
			TEST_ASSERT(HttpFieldValueEqualsAscii(fields[0].value, L"first\tvalue"));
			TEST_ASSERT(FindHttpField(fields, L"x-repeat") == &fields[0]);
			TEST_ASSERT(FindHttpField(fields, L"X-Repeat") == nullptr);
			TEST_ASSERT(CountHttpFields(fields, L"x-repeat") == 2);
			TEST_ASSERT(CountHttpFields(fields, L"X-Repeat") == 0);
			TEST_ERROR(CreateAsciiHttpField(L"", L"value"));
			TEST_ERROR(CreateAsciiHttpField(L"bad name", L"value"));
			TEST_ERROR(CreateAsciiHttpField(L"name", L"bad\rvalue"));
			TEST_ERROR(CreateAsciiHttpField(L"name", WString::CopyFrom(L"\x0080", 1)));
			wchar_t negativeAsciiAlias = (wchar_t)-191;
			TEST_ERROR(CreateAsciiHttpField(WString::CopyFrom(&negativeAsciiAlias, 1), L"value"));
			TEST_ERROR(CreateAsciiHttpField(L"name", WString::CopyFrom(&negativeAsciiAlias, 1)));
		});

		TEST_CASE(L"HTTP field ASCII decoding preserves explicit bytes and stable failure outputs")
		{
			auto value = ByteArray("A\0B");
			WString text = L"unchanged";
			TEST_ASSERT(DecodeAsciiHttpFieldValue(value, text));
			TEST_ASSERT(text.Length() == 3 && text[0] == L'A' && text[1] == 0 && text[2] == L'B');
			TEST_ASSERT(HttpFieldValueEqualsAscii(value, WString::CopyFrom(L"A\0B", 3)));
			TEST_ASSERT(!HttpFieldValueEqualsAscii(value, L"A"));

			Array<vuint8_t> nonAscii(1);
			nonAscii[0] = 0x80;
			text = L"unchanged";
			TEST_ASSERT(!DecodeAsciiHttpFieldValue(nonAscii, text));
			TEST_ASSERT(text == L"unchanged");
			TEST_ASSERT(!HttpFieldValueEqualsAscii(nonAscii, WString::CopyFrom(L"\x0080", 1)));
			wchar_t negativeAsciiAlias = (wchar_t)-191;
			TEST_ASSERT(!HttpFieldValueEqualsAscii(ByteArray("A"), WString::CopyFrom(&negativeAsciiAlias, 1)));
		});

		TEST_CASE(L"HTTP body helpers count and flatten chunks while ignoring trailers")
		{
			HttpBody body;
			AddChunk(body, "A\0");
			AddChunk(body, "");
			AddChunk(body, "BC");
			AddField(body.trailers, L"digest", "ignored");
			vint size = -1;
			TEST_ASSERT(TryGetHttpBodySize(body, size) && size == 4);
			Array<vuint8_t> flattened = ByteArray("old");
			TEST_ASSERT(FlattenHttpBody(body, flattened));
			TEST_ASSERT(SameBytes(flattened, "A\0BC"));

			HttpBody oversized;
			HttpBodyChunk chunk;
			chunk.data.Resize(HttpBodySizeLimit + 1);
			oversized.chunks.Add(std::move(chunk));
			size = 123;
			flattened = ByteArray("unchanged");
			TEST_ASSERT(!TryGetHttpBodySize(oversized, size) && size == 123);
			TEST_ASSERT(!FlattenHttpBody(oversized, flattened));
			TEST_ASSERT(SameBytes(flattened, "unchanged"));
		});

		TEST_CASE(L"HTTP body replacement clears metadata and supports aliased rvalues")
		{
			HttpBody body;
			AddChunk(body, "alias");
			AddField(body.trailers, L"digest", "old");
			SetHttpBodyBytes(body, std::move(body.chunks[0].data));
			TEST_ASSERT(body.chunks.Count() == 1 && SameBytes(body.chunks[0].data, "alias"));
			TEST_ASSERT(body.trailers.Count() == 0);

			Array<vuint8_t> empty;
			SetHttpBodyBytes(body, std::move(empty));
			TEST_ASSERT(body.chunks.Count() == 0 && body.trailers.Count() == 0);

			AddChunk(body, "preserved");
			AddField(body.trailers, L"digest", "preserved");
			Array<vuint8_t> oversized(HttpBodySizeLimit + 1);
			TEST_ERROR(SetHttpBodyBytes(body, std::move(oversized)));
			TEST_ASSERT(body.chunks.Count() == 1 && SameBytes(body.chunks[0].data, "preserved"));
			TEST_ASSERT(body.trailers.Count() == 1 && oversized.Count() == HttpBodySizeLimit + 1);
		});

		TEST_CASE(L"Strict UTF-8 helpers round-trip empty, NUL, BMP, and supplementary characters")
		{
			List<wchar_t> characters;
			characters.Add(L'A');
			characters.Add(0);
			characters.Add((wchar_t)0xE9);
			if constexpr (sizeof(wchar_t) == 2)
			{
				characters.Add((wchar_t)0xD83D);
				characters.Add((wchar_t)0xDE00);
			}
			else
			{
				characters.Add((wchar_t)0x1F600);
			}
			auto original = WString::CopyFrom(&characters[0], characters.Count());
			Array<vuint8_t> encoded;
			TEST_ASSERT(EncodeStrictUtf8(original, encoded));
			TEST_ASSERT(SameBytes(encoded, "A\0\xC3\xA9\xF0\x9F\x98\x80"));
			WString decoded = L"old";
			TEST_ASSERT(DecodeStrictUtf8(&encoded[0], encoded.Count(), decoded));
			TEST_ASSERT(decoded == original);

			Array<vuint8_t> emptyBytes = ByteArray("old");
			TEST_ASSERT(EncodeStrictUtf8(WString::Empty, emptyBytes) && emptyBytes.Count() == 0);
			decoded = L"old";
			TEST_ASSERT(DecodeStrictUtf8(nullptr, 0, decoded) && decoded == WString::Empty);

			auto boundaries = ByteArray("\0\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF");
			TEST_ASSERT(DecodeStrictUtf8(&boundaries[0], boundaries.Count(), decoded));
			Array<vuint8_t> reencoded;
			TEST_ASSERT(EncodeStrictUtf8(decoded, reencoded));
			TEST_ASSERT(SameBytes(reencoded, boundaries));
		});

		TEST_CASE(L"Strict UTF-8 helpers reject malformed input without changing outputs")
		{
			Array<vuint8_t> encoded = ByteArray("unchanged");
			wchar_t invalidCharacter = (wchar_t)0xD800;
			TEST_ASSERT(!EncodeStrictUtf8(WString::CopyFrom(&invalidCharacter, 1), encoded));
			TEST_ASSERT(SameBytes(encoded, "unchanged"));
			invalidCharacter = (wchar_t)0xDC00;
			TEST_ASSERT(!EncodeStrictUtf8(WString::CopyFrom(&invalidCharacter, 1), encoded));
			TEST_ASSERT(SameBytes(encoded, "unchanged"));
			if constexpr (sizeof(wchar_t) > 2)
			{
				invalidCharacter = (wchar_t)0x110000;
				TEST_ASSERT(!EncodeStrictUtf8(WString::CopyFrom(&invalidCharacter, 1), encoded));
				TEST_ASSERT(SameBytes(encoded, "unchanged"));
			}

			auto assertInvalid = [](auto&& input)
			{
				WString output = L"unchanged";
				TEST_ASSERT(!DecodeStrictUtf8(&input[0], input.Count(), output));
				TEST_ASSERT(output == L"unchanged");
			};
			assertInvalid(ByteArray("\x80"));
			assertInvalid(ByteArray("\xC0\x80"));
			assertInvalid(ByteArray("\xE0\x80\x80"));
			assertInvalid(ByteArray("\xF0\x80\x80\x80"));
			assertInvalid(ByteArray("\xC2"));
			assertInvalid(ByteArray("\xE2\x82"));
			assertInvalid(ByteArray("\xF0\x90\x80"));
			assertInvalid(ByteArray("\xE2\x28\xA1"));
			assertInvalid(ByteArray("\xED\xA0\x80"));
			assertInvalid(ByteArray("\xF4\x90\x80\x80"));
			assertInvalid(ByteArray("\xF5\x80\x80\x80"));
			WString output = L"unchanged";
			TEST_ASSERT(!DecodeStrictUtf8(nullptr, 1, output) && output == L"unchanged");
			TEST_ASSERT(!DecodeStrictUtf8(nullptr, -1, output) && output == L"unchanged");
		});

		TEST_CASE(L"HTTP request-line validation enforces syntax and the exact configured limit")
		{
			TEST_ASSERT(ValidateHttpRequestLine(L"GET", L"/") == HttpRequestLineValidationResult::Succeeded);
			TEST_ASSERT(ValidateHttpRequestLine(L"", L"/") == HttpRequestLineValidationResult::InvalidMethod);
			TEST_ASSERT(ValidateHttpRequestLine(L"G ET", L"/") == HttpRequestLineValidationResult::InvalidMethod);
			wchar_t negativeAsciiAlias = (wchar_t)-191;
			TEST_ASSERT(ValidateHttpRequestLine(WString::CopyFrom(&negativeAsciiAlias, 1), L"/") == HttpRequestLineValidationResult::InvalidMethod);
			TEST_ASSERT(ValidateHttpRequestLine(L"GET", L"") == HttpRequestLineValidationResult::InvalidRequestTarget);
			TEST_ASSERT(ValidateHttpRequestLine(L"GET", L"/has space") == HttpRequestLineValidationResult::InvalidRequestTarget);

			Array<wchar_t> exactTargetCharacters(HttpRequestLineSizeLimit - 13);
			for (vint i = 0; i < exactTargetCharacters.Count(); i++) exactTargetCharacters[i] = L'/';
			auto exactTarget = WString::CopyFrom(&exactTargetCharacters[0], exactTargetCharacters.Count());
			TEST_ASSERT(ValidateHttpRequestLine(L"GET", exactTarget) == HttpRequestLineValidationResult::Succeeded);
			Array<wchar_t> oversizedTargetCharacters(HttpRequestLineSizeLimit - 12);
			for (vint i = 0; i < oversizedTargetCharacters.Count(); i++) oversizedTargetCharacters[i] = L'/';
			auto oversizedTarget = WString::CopyFrom(&oversizedTargetCharacters[0], oversizedTargetCharacters.Count());
			TEST_ASSERT(ValidateHttpRequestLine(L"GET", oversizedTarget) == HttpRequestLineValidationResult::TooLong);
			TEST_ASSERT(ValidateHttpRequestLine(L"G ET", oversizedTarget) == HttpRequestLineValidationResult::InvalidMethod);
			oversizedTargetCharacters[0] = L' ';
			auto invalidOversizedTarget = WString::CopyFrom(&oversizedTargetCharacters[0], oversizedTargetCharacters.Count());
			TEST_ASSERT(ValidateHttpRequestLine(L"GET", invalidOversizedTarget) == HttpRequestLineValidationResult::InvalidRequestTarget);
		});
	}

	void RunChunkHelperTestCases()
	{
		TEST_CASE(L"HTTP chunk helper parses chunks, extensions, trailers, and suffix")
		{
			auto input = ByteArray("7\r\nHello, \r\n6;ext=ok\r\nworld!\r\n0\r\nDigest: value\r\n\r\nNEXT");
			HttpBody output;
			vint consumedBytes = -1;
			auto result = ParseHttpRequestBodyToChunks(&input[0], input.Count(), output, consumedBytes);

			TEST_ASSERT(result == HttpRequestBodyParsingResult::Succeeded);
			TEST_ASSERT(consumedBytes == input.Count() - 4);
			TEST_ASSERT(output.chunks.Count() == 2);
			TEST_ASSERT(SameBytes(output.chunks[0].data, "Hello, "));
			TEST_ASSERT(SameBytes(output.chunks[1].data, "world!"));
			TEST_ASSERT(output.trailers.Count() == 1);
			TEST_ASSERT(output.trailers[0].name == L"digest");
			TEST_ASSERT(SameBytes(output.trailers[0].value, "value"));
		});

		TEST_CASE(L"HTTP chunk helper distinguishes incomplete prefixes")
		{
			auto assertIncomplete = [](auto&& input)
			{
				HttpBody output;
				vint consumedBytes = -1;
				auto result = ParseHttpRequestBodyToChunks(&input[0], input.Count(), output, consumedBytes);
				TEST_ASSERT(result == HttpRequestBodyParsingResult::Incomplete);
			};

			assertIncomplete(ByteArray("7\r"));
			assertIncomplete(ByteArray("7\r\nHello"));
			assertIncomplete(ByteArray("1\r\nA\r"));
			assertIncomplete(ByteArray("0\r\nDigest: value\r\n"));
		});

		TEST_CASE(L"HTTP chunk helper counts arbitrary binary octets")
		{
			auto input = ByteArray("4\r\nA\r\0\xFF\r\n0\r\n\r\n");
			HttpBody output;
			vint consumedBytes = -1;
			auto result = ParseHttpRequestBodyToChunks(&input[0], input.Count(), output, consumedBytes);

			TEST_ASSERT(result == HttpRequestBodyParsingResult::Succeeded);
			TEST_ASSERT(consumedBytes == input.Count());
			TEST_ASSERT(output.chunks.Count() == 1);
			TEST_ASSERT(SameBytes(output.chunks[0].data, "A\r\0\xFF"));
			TEST_ASSERT(output.trailers.Count() == 0);
		});

		TEST_CASE(L"HTTP chunk helper rejects malformed and overflowing framing")
		{
			auto assertInvalid = [](auto&& input)
			{
				HttpBody output;
				vint consumedBytes = -1;
				auto result = ParseHttpRequestBodyToChunks(&input[0], input.Count(), output, consumedBytes);
				TEST_ASSERT(result == HttpRequestBodyParsingResult::Invalid);
			};

			assertInvalid(ByteArray("Z"));
			assertInvalid(ByteArray("Z\r\n"));
			assertInvalid(ByteArray("FFFFFFFFFFFFFFFFF"));
			assertInvalid(ByteArray("FFFFFFFFFFFFFFFFF\r\n"));
			assertInvalid(ByteArray("\r"));
			assertInvalid(ByteArray("1;\r"));
			assertInvalid(ByteArray("1;foo=\r"));
			assertInvalid(ByteArray("1;@"));
			assertInvalid(ByteArray("1;bad=\"unterminated\r\nA\r\n0\r\n\r\n"));
			assertInvalid(ByteArray("1\r\nAX"));
			assertInvalid(ByteArray("1\r\nAXY"));
			assertInvalid(ByteArray("0\r\nMalformed-Trailer\r\n\r\n"));
		});

		TEST_CASE(L"HTTP chunk helper enforces body, line, and trailer limits")
		{
			{
				auto input = ByteArray("1000001\r\n");
				HttpBody output;
				vint consumedBytes = -1;
				TEST_ASSERT(ParseHttpRequestBodyToChunks(&input[0], input.Count(), output, consumedBytes) == HttpRequestBodyParsingResult::Invalid);
			}
			{
				List<vuint8_t> bytes;
				AppendBytes(bytes, "1;a=");
				while (bytes.Count() <= HttpChunkSizeLineLimit)
				{
					bytes.Add('x');
				}
				auto input = ToByteArray(bytes);
				HttpBody output;
				vint consumedBytes = -1;
				TEST_ASSERT(ParseHttpRequestBodyToChunks(&input[0], input.Count(), output, consumedBytes) == HttpRequestBodyParsingResult::Invalid);
			}
			{
				List<vuint8_t> bytes;
				AppendBytes(bytes, "0\r\n");
				for (vint i = 0; i <= HttpTrailerBlockSizeLimit; i++)
				{
					bytes.Add('x');
				}
				auto input = ToByteArray(bytes);
				HttpBody output;
				vint consumedBytes = -1;
				TEST_ASSERT(ParseHttpRequestBodyToChunks(&input[0], input.Count(), output, consumedBytes) == HttpRequestBodyParsingResult::Invalid);
			}
		});

		TEST_CASE(L"HTTP chunk helper accepts exact limits split after trailing CR")
		{
			{
				List<vuint8_t> bytes;
				AppendBytes(bytes, "1;a=");
				while (bytes.Count() < HttpChunkSizeLineLimit)
				{
					bytes.Add('x');
				}
				AppendBytes(bytes, "\r\nA\r\n0\r\n\r\n");
				auto input = ToByteArray(bytes);
				HttpBody output;
				vint consumedBytes = -1;
				TEST_ASSERT(ParseHttpRequestBodyToChunks(&input[0], HttpChunkSizeLineLimit + 1, output, consumedBytes) == HttpRequestBodyParsingResult::Incomplete);
				TEST_ASSERT(ParseHttpRequestBodyToChunks(&input[0], input.Count(), output, consumedBytes) == HttpRequestBodyParsingResult::Succeeded);
				TEST_ASSERT(output.chunks.Count() == 1 && SameBytes(output.chunks[0].data, "A"));
			}
			{
				List<vuint8_t> bytes;
				AppendBytes(bytes, "0\r\nx: ");
				for (vint i = 0; i < HttpTrailerBlockSizeLimit - 7; i++)
				{
					bytes.Add('x');
				}
				AppendBytes(bytes, "\r\n\r\n");
				auto input = ToByteArray(bytes);
				HttpBody output;
				vint consumedBytes = -1;
				TEST_ASSERT(ParseHttpRequestBodyToChunks(&input[0], input.Count() - 1, output, consumedBytes) == HttpRequestBodyParsingResult::Incomplete);
				TEST_ASSERT(ParseHttpRequestBodyToChunks(&input[0], input.Count(), output, consumedBytes) == HttpRequestBodyParsingResult::Succeeded);
				TEST_ASSERT(consumedBytes == input.Count());
				TEST_ASSERT(output.trailers.Count() == 1 && output.trailers[0].value.Count() == HttpTrailerBlockSizeLimit - 7);
			}
		});
	}

	void RunFakeConnectionTestCases()
	{
		TEST_CASE(L"HTTP fake socket preserves fragments, chunks, and coalesced message suffix")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
			RecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			socket->Feed(ByteArray("POST /split HTTP/1.1\r"));
			socket->Feed(ByteArray("\nX-Repeat: one\r\nX-Repeat: two\r\nTransfer-Encoding: chunked\r\n\r"));
			socket->Feed(ByteArray("\n3\r\nA\r"));
			TEST_ASSERT(callback.requests.Count() == 0);

			socket->Feed(ByteArray("\n\r\n0\r\nDigest: done\r\n\r\nGET /next HTTP/1.1\r\nContent-Length: 0\r\n\r\n"));
			TEST_ASSERT(callback.requests.Count() == 1);
			auto first = callback.requests[0];
			TEST_ASSERT(first->version.major == 1 && first->version.minor == 1);
			TEST_ASSERT(first->method == L"POST");
			TEST_ASSERT(first->requestTarget == L"/split");
			TEST_ASSERT(CountField(first->headers, L"x-repeat") == 2);
			TEST_ASSERT(HasField(first->headers, L"x-repeat", "one"));
			TEST_ASSERT(HasField(first->headers, L"x-repeat", "two"));
			for (auto&& field : first->headers)
			{
				TEST_ASSERT(IsLowercaseFieldName(field.name));
			}
			TEST_ASSERT(first->body.chunks.Count() == 1);
			TEST_ASSERT(SameBytes(first->body.chunks[0].data, "A\r\n"));
			TEST_ASSERT(first->body.trailers.Count() == 1);
			TEST_ASSERT(first->body.trailers[0].name == L"digest");
			TEST_ASSERT(SameBytes(first->body.trailers[0].value, "done"));

			auto response = Ptr(new HttpResponse);
			response->statusCode = 201;
			response->reason = L"Created";
			AddField(response->headers, L"x-order", "first");
			AddChunk(response->body, "A\0");
			AddChunk(response->body, "\xFF");
			AddField(response->body.trailers, L"digest", "ok");
			connection->SendResponse(response);

			TEST_ASSERT(socket->writes.Count() == 1);
			TEST_ASSERT(SameBytes(
				socket->writes[0]->data,
				"HTTP/1.1 201 Created\r\nx-order: first\r\ntransfer-encoding: chunked\r\n\r\n2\r\nA\0\r\n1\r\n\xFF\r\n0\r\ndigest: ok\r\n\r\n"
				));
			TEST_ASSERT(callback.requests.Count() == 1);

			socket->CompleteNextWrite();
			TEST_ASSERT(callback.writeCompletedCount == 1);
			TEST_ASSERT(callback.requests.Count() == 2);
			TEST_ASSERT(callback.requests[1]->method == L"GET");
			TEST_ASSERT(callback.requests[1]->requestTarget == L"/next");
			TEST_ASSERT(callback.requests[1]->body.chunks.Count() == 0);

			connection->SendResponse(CreateEmptyResponse());
			TEST_ASSERT(socket->writes.Count() == 2);
			socket->CompleteNextWrite();
			TEST_ASSERT(callback.writeCompletedCount == 2);
			connection->Stop();
			TEST_ASSERT(callback.disconnectedCount == 1);
		});

		TEST_CASE(L"HTTP fake socket serializes requests and orders write completion before response")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Client));
			RecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			auto request = Ptr(new HttpRequest);
			request->method = L"POST";
			request->requestTarget = L"/wire";
			AddField(request->headers, L"x-first", "one");
			AddField(request->headers, L"x-first", "two");
			AddChunk(request->body, "A\0B\xFF");
			connection->SendRequest(request);

			TEST_ASSERT(socket->writes.Count() == 1);
			TEST_ASSERT(SameBytes(
				socket->writes[0]->data,
				"POST /wire HTTP/1.1\r\nx-first: one\r\nx-first: two\r\ncontent-length: 4\r\n\r\nA\0B\xFF"
				));
			TEST_ASSERT(callback.writeCompletedCount == 0);

			socket->Feed(ByteArray("HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n"));
			TEST_ASSERT(callback.responses.Count() == 0);
			socket->CompleteNextWrite();
			TEST_ASSERT(callback.writeCompletedCount == 1);
			TEST_ASSERT(callback.responses.Count() == 1);
			TEST_ASSERT(callback.responses[0]->statusCode == 202);
			TEST_ASSERT(callback.responses[0]->reason == L"Accepted");
			TEST_ASSERT(callback.events.IndexOf(L"write") < callback.events.IndexOf(L"response"));
			connection->Stop();
		});

		TEST_CASE(L"HTTP client accepts one cross-thread request while a response callback is delivering")
		{
			class BlockingResponseCallback : public RecordingHttpCallback
			{
			public:
				EventObject						eventEntered;
				EventObject						eventRelease;

				BlockingResponseCallback()
				{
					CHECK_ERROR(eventEntered.CreateManualUnsignal(false), L"Failed to create the response-entered event.");
					CHECK_ERROR(eventRelease.CreateManualUnsignal(false), L"Failed to create the response-release event.");
				}

				void OnReadResponse(Ptr<HttpResponse> response) override
				{
					auto first = responses.Count() == 0;
					RecordingHttpCallback::OnReadResponse(response);
					if (first)
					{
						eventEntered.Signal();
						CHECK_ERROR(eventRelease.WaitForTime(ConnectTimeout), L"The first response callback was not released before its deadline.");
					}
				}
			};

			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto timer = Ptr(new ManualTimeoutController);
			auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Client, nullptr, timer));
			BlockingResponseCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			auto first = Ptr(new HttpRequest);
			first->method = L"GET";
			first->requestTarget = L"/before-delivery";
			connection->SendRequest(first);
			socket->CompleteNextWrite();

			EventObject eventFeedReturned;
			TEST_ASSERT(eventFeedReturned.CreateManualUnsignal(false));
			atomic_vint workerFailed = 0;
			TEST_ASSERT(ThreadPoolLite::Queue(Func<void()>([socket, &eventFeedReturned, &workerFailed]()
			{
				try
				{
					socket->Feed(ByteArray("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"));
				}
				catch (...)
				{
					workerFailed = 1;
				}
				eventFeedReturned.Signal();
			})));
			EventReleaseOnExit releaseResponse(callback.eventRelease);
			TEST_ASSERT(callback.eventEntered.WaitForTime(ConnectTimeout));

			auto second = Ptr(new HttpRequest);
			second->method = L"GET";
			second->requestTarget = L"/after-delivery";
			bool sendSucceeded = true;
			try
			{
				connection->SendRequest(second, 1234);
			}
			catch (...)
			{
				sendSucceeded = false;
			}
			auto writesBeforeRelease = socket->writes.Count();

			callback.eventRelease.Signal();
			TEST_ASSERT(eventFeedReturned.WaitForTime(ConnectTimeout));
			TEST_ASSERT(workerFailed == 0);
			TEST_ASSERT(sendSucceeded);
			TEST_ASSERT(writesBeforeRelease == 1);
			TEST_ASSERT(callback.responses.Count() == 1);
			TEST_ASSERT(socket->writes.Count() == 2);
			TEST_ASSERT(SameBytes(socket->writes[1]->data, "GET /after-delivery HTTP/1.1\r\n\r\n"));
			connection->Stop();
		});

		TEST_CASE(L"HTTP fake socket rejects an unsolicited coalesced response suffix")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Client));
			RecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			auto request = Ptr(new HttpRequest);
			request->method = L"GET";
			request->requestTarget = L"/single-response";
			connection->SendRequest(request);
			socket->Feed(ByteArray(
				"HTTP/1.1 200 Expected\r\ncontent-length: 0\r\n\r\n"
				"HTTP/1.1 201 Unsolicited\r\ncontent-length: 0\r\n\r\n"
				));
			TEST_ASSERT(callback.responses.Count() == 0);
			socket->CompleteNextWrite();

			TEST_ASSERT(callback.writeCompletedCount == 1);
			TEST_ASSERT(callback.responses.Count() == 1);
			TEST_ASSERT(callback.responses[0]->statusCode == 200);
			TEST_ASSERT(callback.lastErrorFatal);
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.events.IndexOf(L"write") < callback.events.IndexOf(L"response"));
			TEST_ASSERT(callback.events.IndexOf(L"response") < callback.events.IndexOf(L"fatal"));
			TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
		});

		TEST_CASE(L"HTTP fake socket reports structured request parse failures before disconnection")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
			RecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			socket->Feed(ByteArray("GET / HTTP/1.1\r\nBad Header: value\r\n\r\n"));
			TEST_ASSERT(callback.requests.Count() == 0);
			TEST_ASSERT(callback.requestFailures.Count() == 1);
			TEST_ASSERT(callback.requestFailures[0] == HttpRequestFailure::BadRequest);
			TEST_ASSERT(callback.lastError == L"");
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.events.IndexOf(L"request-failure") < callback.events.IndexOf(L"disconnected"));
			TEST_ASSERT(socket->stopCount >= 1);
		});

		TEST_CASE(L"HTTP fake socket enforces the request-line size limit")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
			RecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			Array<vuint8_t> oversizedLine(HttpRequestLineSizeLimit + 1);
			for (vint i = 0; i < oversizedLine.Count(); i++)
			{
				oversizedLine[i] = 'A';
			}
			socket->Feed(oversizedLine);
			TEST_ASSERT(callback.requestFailures.Count() == 1);
			TEST_ASSERT(callback.requestFailures[0] == HttpRequestFailure::UriTooLong);
			TEST_ASSERT(callback.lastError == L"");
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.events.IndexOf(L"request-failure") < callback.events.IndexOf(L"disconnected"));
		});

		TEST_CASE(L"HTTP fake socket rejects ambiguous and unsupported message framing")
		{
			AssertRequestFailureInput(
				ByteArray("POST / HTTP/1.1\r\ncontent-length: 0\r\ntransfer-encoding: chunked\r\n\r\n0\r\n\r\n")
				, HttpRequestFailure::BadRequest
				);
			AssertRequestFailureInput(
				ByteArray("POST / HTTP/1.1\r\ncontent-length: 1\r\ncontent-length: 2\r\n\r\nAB")
				, HttpRequestFailure::BadRequest
				);
			AssertRequestFailureInput(
				ByteArray("POST / HTTP/1.1\r\ntransfer-encoding: gzip\r\n\r\n")
				, HttpRequestFailure::NotImplemented
				);
			AssertRequestFailureInput(
				ByteArray("POST / HTTP/1.1\r\ntransfer-encoding: gzip;bare, chunked\r\n\r\n0\r\n\r\n")
				, HttpRequestFailure::BadRequest
				);
			AssertRequestFailureInput(
				ByteArray("GET / HTTP/2.0\r\n\r\n")
				, HttpRequestFailure::HttpVersionNotSupported
				);
			AssertFatalInput(
				HttpRequestConnectionDirection::Client,
				ByteArray("HTTP/1.1 200 OK\r\nx-value: present\r\n\r\n")
				);
			AssertFatalInput(
				HttpRequestConnectionDirection::Client,
				ByteArray("HTTP/1.1 200\r\ncontent-length: 0\r\n\r\n")
				);
			AssertFatalInput(
				HttpRequestConnectionDirection::Client,
				ByteArray("HTTP/1.1 100 Continue\r\ncontent-length: 0\r\n\r\n")
				);
		});

		TEST_CASE(L"HTTP request failures can send one final response before closing")
		{
			class RespondingFailureCallback : public RecordingHttpCallback
			{
			public:
				void OnReadRequestFailure(HttpRequestFailure failure) override
				{
					RecordingHttpCallback::OnReadRequestFailure(failure);
					auto response = CreateEmptyResponse(417, L"Expectation Failed");
					connection->SendResponse(response);
				}
			};

			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
			RespondingFailureCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			socket->Feed(ByteArray("POST /expect HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n"));
			TEST_ASSERT(callback.requestFailures.Count() == 1);
			TEST_ASSERT(callback.requestFailures[0] == HttpRequestFailure::ExpectationFailed);
			TEST_ASSERT(callback.disconnectedCount == 0);
			TEST_ASSERT(socket->writes.Count() == 1);
			TEST_ASSERT(SameBytes(socket->writes[0]->data, "HTTP/1.1 417 Expectation Failed\r\ncontent-length: 0\r\n\r\n"));

			socket->CompleteNextWrite();
			TEST_ASSERT(callback.writeCompletedCount == 1);
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.lastError == L"");
			TEST_ASSERT(callback.events.IndexOf(L"request-failure") < callback.events.IndexOf(L"write"));
			TEST_ASSERT(callback.events.IndexOf(L"write") < callback.events.IndexOf(L"disconnected"));
		});

		TEST_CASE(L"HTTP HEAD, 204, and 304 exchanges suppress response body octets")
		{
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				socket->Feed(ByteArray("HEAD /metadata HTTP/1.1\r\n\r\n"));
				auto response = Ptr(new HttpResponse);
				response->statusCode = 200;
				response->reason = L"OK";
				AddChunk(response->body, "hello");
				connection->SendResponse(response);
				TEST_ASSERT(SameBytes(socket->writes[0]->data, "HTTP/1.1 200 OK\r\ncontent-length: 5\r\n\r\n"));
				socket->CompleteNextWrite();

				socket->Feed(ByteArray("HEAD /declared-metadata HTTP/1.1\r\n\r\n"));
				auto declaredMetadata = Ptr(new HttpResponse);
				declaredMetadata->statusCode = 200;
				declaredMetadata->reason = L"OK";
				AddField(declaredMetadata->headers, L"content-length", "123");
				connection->SendResponse(declaredMetadata);
				TEST_ASSERT(SameBytes(socket->writes[1]->data, "HTTP/1.1 200 OK\r\ncontent-length: 123\r\n\r\n"));
				socket->CompleteNextWrite();

				socket->Feed(ByteArray("HEAD /unknown-metadata HTTP/1.1\r\n\r\n"));
				auto unknownMetadata = Ptr(new HttpResponse);
				unknownMetadata->statusCode = 200;
				unknownMetadata->reason = L"OK";
				connection->SendResponse(unknownMetadata);
				TEST_ASSERT(SameBytes(socket->writes[2]->data, "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n"));
				socket->CompleteNextWrite();

				socket->Feed(ByteArray("GET /no-content HTTP/1.1\r\n\r\n"));
				auto noContent = Ptr(new HttpResponse);
				noContent->statusCode = 204;
				noContent->reason = L"No Content";
				connection->SendResponse(noContent);
				TEST_ASSERT(SameBytes(socket->writes[3]->data, "HTTP/1.1 204 No Content\r\n\r\n"));
				socket->CompleteNextWrite();

				socket->Feed(ByteArray("GET /not-modified HTTP/1.1\r\n\r\n"));
				auto notModified = Ptr(new HttpResponse);
				notModified->statusCode = 304;
				notModified->reason = L"Not Modified";
				AddField(notModified->headers, L"content-length", "123");
				connection->SendResponse(notModified);
				TEST_ASSERT(SameBytes(socket->writes[4]->data, "HTTP/1.1 304 Not Modified\r\ncontent-length: 123\r\n\r\n"));
				socket->CompleteNextWrite();
				connection->Stop();
			}
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Client, nullptr, timer));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				auto head = Ptr(new HttpRequest);
				head->method = L"HEAD";
				head->requestTarget = L"/metadata";
				connection->SendRequest(head);
				socket->CompleteNextWrite();
				socket->Feed(ByteArray("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"));
				TEST_ASSERT(callback.responses.Count() == 1);
				TEST_ASSERT(callback.responses[0]->body.chunks.Count() == 0);

				auto unframedHead = Ptr(new HttpRequest);
				unframedHead->method = L"HEAD";
				unframedHead->requestTarget = L"/unknown-metadata";
				connection->SendRequest(unframedHead);
				socket->CompleteNextWrite();
				socket->Feed(ByteArray("HTTP/1.1 200 OK\r\n\r\n"));
				TEST_ASSERT(callback.responses.Count() == 2);

				auto get204 = Ptr(new HttpRequest);
				get204->method = L"GET";
				get204->requestTarget = L"/no-content";
				connection->SendRequest(get204);
				socket->CompleteNextWrite();
				socket->Feed(ByteArray("HTTP/1.1 204 No Content\r\n\r\n"));
				TEST_ASSERT(callback.responses.Count() == 3);

				auto get304 = Ptr(new HttpRequest);
				get304->method = L"GET";
				get304->requestTarget = L"/not-modified";
				connection->SendRequest(get304);
				socket->CompleteNextWrite();
				socket->Feed(ByteArray("HTTP/1.1 304 Not Modified\r\nContent-Length: 123\r\n\r\n"));
				TEST_ASSERT(callback.responses.Count() == 4);
				TEST_ASSERT(callback.responses[3]->body.chunks.Count() == 0);
				connection->Stop();
			}
		});

		TEST_CASE(L"HTTP fake socket enforces header and fixed-body limits")
		{
			{
				List<vuint8_t> bytes;
				AppendBytes(bytes, "GET / HTTP/1.1\r\n");
				for (vint i = 0; i <= HttpHeaderBlockSizeLimit; i++)
				{
					bytes.Add('x');
				}
				auto input = ToByteArray(bytes);
				AssertRequestFailureInput(input, HttpRequestFailure::RequestHeaderFieldsTooLarge);
			}
			AssertRequestFailureInput(
				ByteArray("POST /too-large HTTP/1.1\r\ncontent-length: 16777217\r\n\r\n")
				, HttpRequestFailure::PayloadTooLarge
				);
		});

		TEST_CASE(L"HTTP fake socket accepts exact request and header limits split after CR")
		{
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				List<vuint8_t> bytes;
				AppendBytes(bytes, "GET /");
				for (vint i = 0; i < HttpRequestLineSizeLimit - 14; i++)
				{
					bytes.Add('a');
				}
				AppendBytes(bytes, " HTTP/1.1\r");
				TEST_ASSERT(bytes.Count() == HttpRequestLineSizeLimit + 1);
				auto firstBlock = ToByteArray(bytes);
				socket->Feed(firstBlock);
				TEST_ASSERT(callback.lastError == L"");
				TEST_ASSERT(callback.requests.Count() == 0);
				socket->Feed(ByteArray("\ncontent-length: 0\r\n\r\n"));
				TEST_ASSERT(callback.requests.Count() == 1);
				connection->Stop();
			}
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				List<vuint8_t> bytes;
				AppendBytes(bytes, "GET / HTTP/1.1\r\nx: ");
				for (vint i = 0; i < HttpHeaderBlockSizeLimit - 7; i++)
				{
					bytes.Add('x');
				}
				AppendBytes(bytes, "\r\n\r");
				auto firstBlock = ToByteArray(bytes);
				socket->Feed(firstBlock);
				TEST_ASSERT(callback.lastError == L"");
				TEST_ASSERT(callback.requests.Count() == 0);
				socket->Feed(ByteArray("\n"));
				TEST_ASSERT(callback.requests.Count() == 1);
				TEST_ASSERT(callback.requests[0]->headers.Count() == 1);
				TEST_ASSERT(callback.requests[0]->headers[0].value.Count() == HttpHeaderBlockSizeLimit - 7);
				connection->Stop();
			}
		});

		TEST_CASE(L"HTTP fake socket rejects invalid outbound structures and operation order")
		{
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Client));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);

				auto uppercase = Ptr(new HttpRequest);
				uppercase->method = L"GET";
				uppercase->requestTarget = L"/invalid-field";
				AddField(uppercase->headers, L"X-Invalid", "value");
				TEST_ERROR(connection->SendRequest(uppercase));

				auto unsupportedVersion = Ptr(new HttpRequest);
				unsupportedVersion->version.major = 2;
				unsupportedVersion->version.minor = 0;
				unsupportedVersion->method = L"GET";
				unsupportedVersion->requestTarget = L"/unsupported-version";
				TEST_ERROR(connection->SendRequest(unsupportedVersion));

				auto mismatched = Ptr(new HttpRequest);
				mismatched->method = L"POST";
				mismatched->requestTarget = L"/mismatch";
				AddField(mismatched->headers, L"content-length", "2");
				AddChunk(mismatched->body, "A");
				TEST_ERROR(connection->SendRequest(mismatched));

				auto ambiguous = Ptr(new HttpRequest);
				ambiguous->method = L"POST";
				ambiguous->requestTarget = L"/ambiguous";
				AddField(ambiguous->headers, L"content-length", "0");
				AddField(ambiguous->headers, L"transfer-encoding", "chunked");
				TEST_ERROR(connection->SendRequest(ambiguous));

				auto unsupportedCoding = Ptr(new HttpRequest);
				unsupportedCoding->method = L"POST";
				unsupportedCoding->requestTarget = L"/unsupported-coding";
				AddField(unsupportedCoding->headers, L"transfer-encoding", "gzip");
				TEST_ERROR(connection->SendRequest(unsupportedCoding));

				auto tooManyChunks = Ptr(new HttpRequest);
				tooManyChunks->method = L"POST";
				tooManyChunks->requestTarget = L"/too-many-chunks";
				for (vint i = 0; i <= 64 * 1024; i++)
				{
					AddChunk(tooManyChunks->body, "x");
				}
				TEST_ERROR(connection->SendRequest(tooManyChunks));
				TEST_ERROR(connection->SendResponse(CreateEmptyResponse()));
				TEST_ASSERT(socket->writes.Count() == 0);

				auto valid = Ptr(new HttpRequest);
				valid->method = L"GET";
				valid->requestTarget = L"/first";
				connection->SendRequest(valid);
				auto overlapping = Ptr(new HttpRequest);
				overlapping->method = L"GET";
				overlapping->requestTarget = L"/second";
				TEST_ERROR(connection->SendRequest(overlapping));
				TEST_ASSERT(socket->writes.Count() == 1);
				socket->CompleteNextWrite();
				connection->Stop();
			}
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();
				TEST_ERROR(connection->SendResponse(CreateEmptyResponse()));
				auto request = Ptr(new HttpRequest);
				request->method = L"GET";
				request->requestTarget = L"/wrong-direction";
				TEST_ERROR(connection->SendRequest(request));
				socket->Feed(ByteArray("GET /ready HTTP/1.1\r\n\r\n"));
				TEST_ASSERT(callback.requests.Count() == 1);
				auto informational = CreateEmptyResponse(100, L"Continue");
				TEST_ERROR(connection->SendResponse(informational));
				auto outOfRange = CreateEmptyResponse(600, L"Out of Range");
				TEST_ERROR(connection->SendResponse(outOfRange));
				auto framedNoContent = CreateEmptyResponse(204, L"No Content");
				TEST_ERROR(connection->SendResponse(framedNoContent));
				connection->SendResponse(CreateEmptyResponse());
				TEST_ERROR(connection->SendResponse(CreateEmptyResponse()));
				TEST_ASSERT(socket->writes.Count() == 1);
				socket->CompleteNextWrite();
				connection->Stop();
			}
		});

		TEST_CASE(L"HTTP fake socket exposes a deterministic incomplete-message timeout")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto timer = Ptr(new ManualTimeoutController);
			auto connection = Ptr(new HttpRequestConnection(
				socket.Obj(),
				HttpRequestConnectionDirection::Server,
				nullptr,
				timer
				));
			RecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			socket->Feed(ByteArray("POST /timeout HTTP/1.1\r\nContent-Length: 5\r\n\r\nab"));
			TEST_ASSERT(timer->armCount >= 1);
			TEST_ASSERT(timer->lastArmDuration == HttpIncompleteMessageTimeout);
			TEST_ASSERT(callback.requests.Count() == 0);
			timer->Fire();
			TEST_ASSERT(callback.requestFailures.Count() == 1);
			TEST_ASSERT(callback.requestFailures[0] == HttpRequestFailure::RequestTimeout);
			TEST_ASSERT(callback.lastError == L"");
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.events.IndexOf(L"request-failure") < callback.events.IndexOf(L"disconnected"));
			TEST_ASSERT(timer->cancelCount >= 1);
		});
	}

	void RunHttpLifecycleTestCases()
	{
		TEST_CASE(L"HTTP Connection: close stops at the response boundary in both directions")
		{
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				socket->Feed(ByteArray("GET /close HTTP/1.1\r\nConnection: close\r\n\r\n"));
				TEST_ASSERT(callback.requests.Count() == 1);
				TEST_ASSERT(callback.disconnectedCount == 0);
				connection->SendResponse(CreateEmptyResponse());
				TEST_ASSERT(callback.disconnectedCount == 0);

				socket->CompleteNextWrite();
				TEST_ASSERT(callback.writeCompletedCount == 1);
				TEST_ASSERT(callback.disconnectedCount == 1);
				TEST_ASSERT(callback.lastError == L"");
				TEST_ASSERT(callback.events.IndexOf(L"write") < callback.events.IndexOf(L"disconnected"));
				TEST_ASSERT(socket->stopCount >= 1);
				TEST_ASSERT(!socket->HasCallback());
				connection->Stop();
				TEST_ASSERT(callback.disconnectedCount == 1);
			}
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				auto connection = Ptr(new HttpRequestConnection(
					socket.Obj(),
					HttpRequestConnectionDirection::Client,
					nullptr,
					timer
					));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				auto request = Ptr(new HttpRequest);
				request->method = L"GET";
				request->requestTarget = L"/close";
				connection->SendRequest(request);
				socket->CompleteNextWrite();
				TEST_ASSERT(callback.disconnectedCount == 0);

				socket->Feed(ByteArray("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"));
				TEST_ASSERT(callback.responses.Count() == 1);
				TEST_ASSERT(callback.disconnectedCount == 1);
				TEST_ASSERT(callback.lastError == L"");
				TEST_ASSERT(callback.events.IndexOf(L"response") < callback.events.IndexOf(L"disconnected"));
				TEST_ASSERT(!timer->TryFire());
				TEST_ASSERT(!socket->HasCallback());
			}
		});

		TEST_CASE(L"HTTP Stop is callback-reentrant and returns after the terminal callback")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Server));
			StopInsideRequestCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			socket->Feed(ByteArray("GET /stop-in-callback HTTP/1.1\r\nContent-Length: 0\r\n\r\n"));
			TEST_ASSERT(callback.requestCount == 1);
			TEST_ASSERT(callback.stopReturned);
			TEST_ASSERT(callback.disconnectedBeforeStopReturned);
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.errorCount == 0);
			TEST_ASSERT(callback.callbacksAfterStop == 0);
			TEST_ASSERT(socket->stopCount >= 1);
			TEST_ASSERT(!socket->HasCallback());

			connection->Stop();
			connection->Stop();
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.errorCount == 0);
			TEST_ASSERT(callback.callbacksAfterStop == 0);
		});

		TEST_CASE(L"HttpRequestServer retains accepted adapters and hard-stops accepted and rejected clients")
		{
			{
				auto nativeServer = Ptr(new FakeAsyncSocketServer);
				FakeHttpRequestServer server(nativeServer, true);
				auto socket = Ptr(new FakeAsyncSocketConnection);
				server.Start();

				TEST_ASSERT(nativeServer->Accept(socket.Obj()) == WaitForClientResult::Accept);
				TEST_ASSERT(server.acceptCallbackCount == 1);
				TEST_ASSERT(server.acceptedConnection != nullptr);
				TEST_ASSERT(socket->HasCallback());
				socket->Feed(ByteArray("GET /retained HTTP/1.1\r\nContent-Length: 0\r\n\r\n"));
				TEST_ASSERT(server.acceptedCallback.requests.Count() == 1);

				server.Stop();
				TEST_ASSERT(server.IsStopped());
				TEST_ASSERT(nativeServer->callbackPresentDuringFirstStop);
				TEST_ASSERT(!nativeServer->HasCallback());
				TEST_ASSERT(socket->stopCount >= 1);
				TEST_ASSERT(!socket->HasCallback());
				TEST_ASSERT(server.acceptedCallback.disconnectedCount == 1);
				TEST_ASSERT(server.acceptedCallback.lastError == L"");

				server.Stop();
				TEST_ASSERT(server.IsStopped());
				TEST_ASSERT(nativeServer->stopCount >= 2);
				TEST_ASSERT(server.acceptedCallback.disconnectedCount == 1);
			}
			{
				auto nativeServer = Ptr(new FakeAsyncSocketServer);
				FakeHttpRequestServer server(nativeServer, false);
				auto socket = Ptr(new FakeAsyncSocketConnection);
				server.Start();

				TEST_ASSERT(nativeServer->Accept(socket.Obj()) == WaitForClientResult::Reject);
				TEST_ASSERT(server.acceptCallbackCount == 1);
				TEST_ASSERT(socket->stopCount >= 1);
				TEST_ASSERT(socket->uninstalledCallbackCount >= 1);
				TEST_ASSERT(!socket->HasCallback());

				server.Stop();
				server.Stop();
				TEST_ASSERT(server.IsStopped());
				TEST_ASSERT(nativeServer->stopCount >= 2);
			}
		});

		TEST_CASE(L"HttpRequestServer relays one unexpected listener stop and suppresses explicit stops")
		{
			{
				auto nativeServer = Ptr(new FakeAsyncSocketServer);
				FakeHttpRequestServer server(nativeServer, true);
				server.Start();
				nativeServer->FailUnexpected();
				TEST_ASSERT(server.unexpectedStopCount == 1);
				TEST_ASSERT(server.IsStopped());
				TEST_ASSERT(nativeServer->stopCount >= 1);
				server.Stop();
				TEST_ASSERT(server.unexpectedStopCount == 1);
			}
			{
				auto nativeServer = Ptr(new FakeAsyncSocketServer);
				FakeHttpRequestServer server(nativeServer, true);
				server.Start();
				server.Stop();
				TEST_ASSERT(server.unexpectedStopCount == 0);
			}
		});

		TEST_CASE(L"HTTP retained adapters drain when a native call finishes after peer disconnection")
		{
			class DrainCounter : public Object
			{
			public:
				atomic_vint count = 0;
			};

			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Client));
			RecordingHttpCallback callback;
			auto drained = Ptr(new DrainCounter);
			connection->RetainUntilStopped(connection, Func<void()>([drained]()
			{
				drained->count++;
			}));
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();
			socket->beforeWrite = Func<void()>([socket]()
			{
				socket->DisconnectFromPeer();
			});

			auto request = Ptr(new HttpRequest);
			request->method = L"GET";
			request->requestTarget = L"/disconnect-during-write";
			connection->SendRequest(request);
			socket->beforeWrite = Func<void()>();

			TEST_ASSERT(drained->count == 1);
			TEST_ASSERT(callback.lastErrorFatal);
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
			TEST_ASSERT(!socket->HasCallback());
			connection->Stop();
			TEST_ASSERT(drained->count == 1);
		});

		TEST_CASE(L"HTTP message completion cancels a timeout that cannot fire afterward")
		{
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				auto connection = Ptr(new HttpRequestConnection(
					socket.Obj(),
					HttpRequestConnectionDirection::Server,
					nullptr,
					timer
					));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				socket->Feed(ByteArray("POST /complete HTTP/1.1\r\nContent-Length: 3\r\n\r\nA"));
				TEST_ASSERT(callback.requests.Count() == 0);
				TEST_ASSERT(timer->HasCallback());
				socket->Feed(ByteArray("BC"));
				TEST_ASSERT(callback.requests.Count() == 1);
				TEST_ASSERT(timer->cancelCount >= 1);
				TEST_ASSERT(!timer->HasCallback());
				TEST_ASSERT(!timer->TryFire());
				TEST_ASSERT(callback.lastError == L"");

				connection->Stop();
				TEST_ASSERT(callback.disconnectedCount == 1);
				TEST_ASSERT(callback.lastError == L"");
				TEST_ASSERT(!timer->TryFire());
			}
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				auto connection = Ptr(new HttpRequestConnection(
					socket.Obj(),
					HttpRequestConnectionDirection::Client,
					nullptr,
					timer
					));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				auto request = Ptr(new HttpRequest);
				request->method = L"GET";
				request->requestTarget = L"/complete";
				connection->SendRequest(request);
				TEST_ASSERT(timer->armCount == 0);
				TEST_ASSERT(!timer->HasCallback());
				socket->CompleteNextWrite();
				TEST_ASSERT(timer->HasCallback());

				socket->Feed(ByteArray("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"));
				TEST_ASSERT(callback.responses.Count() == 1);
				TEST_ASSERT(timer->cancelCount >= 1);
				TEST_ASSERT(!timer->HasCallback());
				TEST_ASSERT(!timer->TryFire());
				TEST_ASSERT(callback.lastError == L"");
				connection->Stop();
			}
		});

		TEST_CASE(L"HTTP local Stop cancels incomplete client and server messages without a fatal error")
		{
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				auto connection = Ptr(new HttpRequestConnection(
					socket.Obj(),
					HttpRequestConnectionDirection::Server,
					nullptr,
					timer
					));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				socket->Feed(ByteArray("POST /stop HTTP/1.1\r\nContent-Length: 4\r\n\r\nA"));
				TEST_ASSERT(timer->HasCallback());
				connection->Stop();
				TEST_ASSERT(callback.requests.Count() == 0);
				TEST_ASSERT(callback.lastError == L"");
				TEST_ASSERT(callback.disconnectedCount == 1);
				TEST_ASSERT(timer->cancelCount >= 1);
				TEST_ASSERT(!timer->TryFire());
			}
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				auto connection = Ptr(new HttpRequestConnection(
					socket.Obj(),
					HttpRequestConnectionDirection::Client,
					nullptr,
					timer
					));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				auto request = Ptr(new HttpRequest);
				request->method = L"GET";
				request->requestTarget = L"/stop";
				connection->SendRequest(request);
				socket->CompleteNextWrite();
				TEST_ASSERT(timer->HasCallback());
				socket->Feed(ByteArray("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nA"));
				TEST_ASSERT(callback.responses.Count() == 0);
				TEST_ASSERT(timer->refreshCount == 0);
				connection->Stop();

				TEST_ASSERT(callback.lastError == L"");
				TEST_ASSERT(callback.disconnectedCount == 1);
				TEST_ASSERT(timer->cancelCount >= 1);
				TEST_ASSERT(!timer->TryFire());
			}
		});

		TEST_CASE(L"HTTP client arms its response timeout only after request write completion")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto timer = Ptr(new ManualTimeoutController);
			auto connection = Ptr(new HttpRequestConnection(
				socket.Obj(),
				HttpRequestConnectionDirection::Client,
				nullptr,
				timer
				));
			RecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			auto request = Ptr(new HttpRequest);
			request->method = L"GET";
			request->requestTarget = L"/no-response";
			connection->SendRequest(request);
			TEST_ASSERT(timer->armCount == 0);
			TEST_ASSERT(!timer->HasCallback());

			socket->CompleteNextWrite();
			TEST_ASSERT(callback.writeCompletedCount == 1);
			TEST_ASSERT(timer->armCount == 1);
			TEST_ASSERT(timer->lastArmDuration == HttpIncompleteMessageTimeout);
			TEST_ASSERT(timer->HasCallback());
			timer->Fire();

			TEST_ASSERT(callback.responses.Count() == 0);
			TEST_ASSERT(callback.lastError != L"");
			TEST_ASSERT(callback.lastErrorFatal);
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.events.IndexOf(L"write") < callback.events.IndexOf(L"fatal"));
			TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
			TEST_ASSERT(!timer->TryFire());
			TEST_ASSERT(!socket->HasCallback());
		});

		TEST_CASE(L"HTTP response timeout does not cyclically wait for its native disconnection callback")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto timer = Ptr(new RacingTimeoutController);
			auto connection = Ptr(new HttpRequestConnection(
				socket.Obj(),
				HttpRequestConnectionDirection::Client,
				nullptr,
				timer
				));
			RecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			auto request = Ptr(new HttpRequest);
			request->method = L"GET";
			request->requestTarget = L"/timeout-disconnect-race";
			connection->SendRequest(request);
			socket->CompleteNextWrite();

			EventObject eventNativeDisconnectFinished;
			TEST_ASSERT(eventNativeDisconnectFinished.CreateManualUnsignal(false));
			atomic_vint workerFailed = 0;
			socket->stopAction = Func<void()>([socket, &eventNativeDisconnectFinished, &workerFailed]()
			{
				auto queued = ThreadPoolLite::Queue(Func<void()>([socket, &eventNativeDisconnectFinished, &workerFailed]()
				{
					try
					{
						socket->DisconnectFromPeer();
					}
					catch (...)
					{
						workerFailed = 1;
					}
					eventNativeDisconnectFinished.Signal();
				}));
				if (!queued || !eventNativeDisconnectFinished.WaitForTime(ConnectTimeout))
				{
					workerFailed = 1;
				}
			});

			timer->Fire();
			socket->stopAction = Func<void()>();

			TEST_ASSERT(workerFailed == 0);
			TEST_ASSERT(timer->externalCancelWaits == 0);
			TEST_ASSERT(callback.lastErrorFatal);
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
			TEST_ASSERT(!socket->HasCallback());
			connection->Stop();
		});

		TEST_CASE(L"HTTP external Stop does not cyclically wait for a firing response timeout")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto timer = Ptr(new RacingTimeoutController);
			auto connection = Ptr(new HttpRequestConnection(
				socket.Obj(),
				HttpRequestConnectionDirection::Client,
				nullptr,
				timer
				));
			BlockingErrorHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			auto request = Ptr(new HttpRequest);
			request->method = L"GET";
			request->requestTarget = L"/timeout-external-stop-race";
			connection->SendRequest(request);
			socket->CompleteNextWrite();

			EventObject eventFireDone;
			EventObject eventStopDone;
			TEST_ASSERT(eventFireDone.CreateManualUnsignal(false));
			TEST_ASSERT(eventStopDone.CreateManualUnsignal(false));
			atomic_vint fireFailed = 0;
			atomic_vint stopFailed = 0;
			auto fireQueued = ThreadPoolLite::Queue(Func<void()>([timer, &eventFireDone, &fireFailed]()
			{
				EventReleaseOnExit signal(eventFireDone);
				try
				{
					timer->Fire();
				}
				catch (...)
				{
					fireFailed = 1;
				}
			}));

			auto errorEntered = fireQueued && callback.eventErrorEntered.WaitForTime(ConnectTimeout);
			bool stopQueued = false;
			if (errorEntered)
			{
				stopQueued = ThreadPoolLite::Queue(Func<void()>([connection, &eventStopDone, &stopFailed]()
				{
					EventReleaseOnExit signal(eventStopDone);
					try
					{
						connection->Stop();
					}
					catch (...)
					{
						stopFailed = 1;
					}
				}));
			}
			auto externalCancelEntered = stopQueued && timer->eventExternalCancelEntered.WaitForTime(ConnectTimeout);
			callback.eventReleaseError.Signal();
			auto fireDone = !fireQueued || eventFireDone.WaitForTime(ConnectTimeout);
			auto stopDone = !stopQueued || eventStopDone.WaitForTime(ConnectTimeout);
			connection->Stop();

			TEST_ASSERT(fireQueued);
			TEST_ASSERT(errorEntered);
			TEST_ASSERT(stopQueued);
			TEST_ASSERT(externalCancelEntered);
			TEST_ASSERT(fireDone);
			TEST_ASSERT(stopDone);
			TEST_ASSERT(fireFailed == 0);
			TEST_ASSERT(stopFailed == 0);
			TEST_ASSERT(timer->externalCancelWaits == 1);
			TEST_ASSERT(callback.lastErrorFatal);
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
			TEST_ASSERT(!socket->HasCallback());
		});

		TEST_CASE(L"HTTP client uses a per-exchange deadline without refreshing partial responses")
		{
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Client, nullptr, timer));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				auto request = Ptr(new HttpRequest);
				request->method = L"GET";
				request->requestTarget = L"/custom-deadline";
				connection->SendRequest(request, 1234);
				TEST_ASSERT(timer->armCount == 0);
				socket->CompleteNextWrite();
				TEST_ASSERT(timer->armCount == 1);
				TEST_ASSERT(timer->lastArmDuration == 1234);

				socket->Feed(ByteArray("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nA"));
				TEST_ASSERT(timer->refreshCount == 0);
				socket->Feed(ByteArray("B"));
				TEST_ASSERT(callback.responses.Count() == 1);
				TEST_ASSERT(timer->refreshCount == 0);
				TEST_ASSERT(!timer->TryFire());
				connection->Stop();
			}
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Client, nullptr, timer));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				auto request = Ptr(new HttpRequest);
				request->method = L"GET";
				request->requestTarget = L"/infinite-deadline";
				connection->SendRequest(request, 0);
				socket->CompleteNextWrite();
				TEST_ASSERT(timer->armCount == 0);
				socket->Feed(ByteArray("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nA"));
				TEST_ASSERT(timer->armCount == 0);
				TEST_ASSERT(timer->refreshCount == 0);
				connection->Stop();
				TEST_ASSERT(callback.lastError == L"");
			}
		});

		TEST_CASE(L"HTTP client rejects unsolicited bytes while idle and after a held response")
		{
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Client));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				socket->Feed(ByteArray("HTTP/1.1 200 Unsolicited\r\nContent-Length: 0\r\n\r\n"));
				TEST_ASSERT(callback.responses.Count() == 0);
				TEST_ASSERT(callback.lastErrorFatal);
				TEST_ASSERT(callback.disconnectedCount == 1);
				TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
				TEST_ASSERT(!socket->HasCallback());
			}
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				auto connection = Ptr(new HttpRequestConnection(
					socket.Obj(),
					HttpRequestConnectionDirection::Client,
					nullptr,
					timer
					));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				auto request = Ptr(new HttpRequest);
				request->method = L"GET";
				request->requestTarget = L"/held";
				connection->SendRequest(request);
				socket->Feed(ByteArray("HTTP/1.1 200 Expected\r\nContent-Length: 0\r\n\r\n"));
				TEST_ASSERT(callback.responses.Count() == 0);
				socket->Feed(ByteArray("HTTP/1.1 201 Extra\r\nContent-Length: 0\r\n\r\n"));
				TEST_ASSERT(callback.responses.Count() == 0);

				socket->CompleteNextWrite();
				TEST_ASSERT(callback.writeCompletedCount == 1);
				TEST_ASSERT(callback.responses.Count() == 1);
				TEST_ASSERT(callback.responses[0]->statusCode == 200);
				TEST_ASSERT(callback.lastErrorFatal);
				TEST_ASSERT(callback.disconnectedCount == 1);
				TEST_ASSERT(callback.events.IndexOf(L"write") < callback.events.IndexOf(L"response"));
				TEST_ASSERT(callback.events.IndexOf(L"response") < callback.events.IndexOf(L"fatal"));
				TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
				TEST_ASSERT(!timer->TryFire());
				TEST_ASSERT(!socket->HasCallback());
			}
		});

		TEST_CASE(L"HTTP timeout controller arm and refresh failures become fatal connection errors")
		{
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				timer->failArm = true;
				auto connection = Ptr(new HttpRequestConnection(
					socket.Obj(),
					HttpRequestConnectionDirection::Server,
					nullptr,
					timer
					));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				socket->Feed(ByteArray("POST /arm-failure HTTP/1.1\r\nContent-Length: 2\r\n\r\nA"));
				TEST_ASSERT(timer->armCount == 1);
				TEST_ASSERT(callback.lastErrorFatal);
				TEST_ASSERT(callback.disconnectedCount == 1);
				TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
				TEST_ASSERT(!socket->HasCallback());
			}
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				auto connection = Ptr(new HttpRequestConnection(
					socket.Obj(),
					HttpRequestConnectionDirection::Server,
					nullptr,
					timer
					));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				socket->Feed(ByteArray("POST /refresh-failure HTTP/1.1\r\nContent-Length: 3\r\n\r\nA"));
				TEST_ASSERT(timer->armCount == 1);
				timer->failRefresh = true;
				socket->Feed(ByteArray("B"));
				TEST_ASSERT(timer->refreshCount == 1);
				TEST_ASSERT(callback.lastErrorFatal);
				TEST_ASSERT(callback.disconnectedCount == 1);
				TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
				TEST_ASSERT(!socket->HasCallback());
			}
			{
				auto socket = Ptr(new FakeAsyncSocketConnection);
				auto timer = Ptr(new ManualTimeoutController);
				timer->failArm = true;
				auto connection = Ptr(new HttpRequestConnection(
					socket.Obj(),
					HttpRequestConnectionDirection::Client,
					nullptr,
					timer
					));
				RecordingHttpCallback callback;
				connection->InstallCallback(&callback);
				connection->BeginReadingLoopUnsafe();

				auto request = Ptr(new HttpRequest);
				request->method = L"GET";
				request->requestTarget = L"/response-arm-failure";
				connection->SendRequest(request);
				socket->CompleteNextWrite();
				TEST_ASSERT(callback.writeCompletedCount == 1);
				TEST_ASSERT(timer->armCount == 1);
				TEST_ASSERT(callback.lastErrorFatal);
				TEST_ASSERT(callback.disconnectedCount == 1);
				TEST_ASSERT(callback.events.IndexOf(L"write") < callback.events.IndexOf(L"fatal"));
				TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
				TEST_ASSERT(!socket->HasCallback());
			}
		});

		TEST_CASE(L"HTTP client preserves write ordering when a held response is followed by surplus bytes and peer disconnection")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto connection = Ptr(new HttpRequestConnection(socket.Obj(), HttpRequestConnectionDirection::Client));
			RecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			auto request = Ptr(new HttpRequest);
			request->method = L"GET";
			request->requestTarget = L"/held-disconnect";
			connection->SendRequest(request);
			socket->Feed(ByteArray("HTTP/1.1 200 Expected\r\nContent-Length: 0\r\n\r\n"));
			socket->Feed(ByteArray("HTTP/1.1 201 Extra\r\nContent-Length: 0\r\n\r\n"));
			TEST_ASSERT(callback.responses.Count() == 0);

			socket->DisconnectFromPeer();
			TEST_ASSERT(callback.writeCompletedCount == 0);
			TEST_ASSERT(callback.responses.Count() == 0);
			TEST_ASSERT(callback.lastErrorFatal);
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
			TEST_ASSERT(!socket->HasCallback());
		});

		TEST_CASE(L"HTTP client reserves a completed response against a concurrent unsolicited read")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto timer = Ptr(new BlockingFirstCancelTimeoutController);
			auto connection = Ptr(new HttpRequestConnection(
				socket.Obj(),
				HttpRequestConnectionDirection::Client,
				nullptr,
				timer
				));
			CoordinatedRecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			auto request = Ptr(new HttpRequest);
			request->method = L"GET";
			request->requestTarget = L"/delivery-race";
			connection->SendRequest(request);
			socket->CompleteNextWrite();

			EventObject eventResponseFeedReturned;
			EventObject eventExtraFeedReturned;
			atomic_vint workerFailure = 0;
			TEST_ASSERT(eventResponseFeedReturned.CreateManualUnsignal(false));
			TEST_ASSERT(eventExtraFeedReturned.CreateManualUnsignal(false));
			TEST_ASSERT(ThreadPoolLite::Queue(Func<void()>([socket, &eventResponseFeedReturned, &workerFailure]()
			{
				try
				{
					socket->Feed(ByteArray("HTTP/1.1 200 Expected\r\nContent-Length: 0\r\n\r\n"));
				}
				catch (...)
				{
					workerFailure = 1;
					eventResponseFeedReturned.Signal();
					return;
				}
				eventResponseFeedReturned.Signal();
			})));
			EventReleaseOnExit releaseFirstCancel(timer->eventReleaseFirstCancel);
			TEST_ASSERT(timer->eventFirstCancelEntered.WaitForTime(ConnectTimeout));
			auto overlapping = Ptr(new HttpRequest);
			overlapping->method = L"GET";
			overlapping->requestTarget = L"/overlapping-delivery";
			connection->SendRequest(overlapping);
			TEST_ASSERT(socket->writes.Count() == 1);

			TEST_ASSERT(ThreadPoolLite::Queue(Func<void()>([socket, &callback, &eventExtraFeedReturned, &workerFailure]()
			{
				try
				{
					socket->Feed(ByteArray("HTTP/1.1 201 Extra\r\nContent-Length: 0\r\n\r\n"));
				}
				catch (...)
				{
					workerFailure = 1;
					callback.eventRaceCanRelease.Signal();
					eventExtraFeedReturned.Signal();
					return;
				}
				callback.eventRaceCanRelease.Signal();
				eventExtraFeedReturned.Signal();
			})));
			TEST_ASSERT(callback.eventRaceCanRelease.WaitForTime(ConnectTimeout));
			timer->eventReleaseFirstCancel.Signal();
			TEST_ASSERT(eventResponseFeedReturned.WaitForTime(ConnectTimeout));
			TEST_ASSERT(eventExtraFeedReturned.WaitForTime(ConnectTimeout));
			TEST_ASSERT(workerFailure == 0);

			TEST_ASSERT(callback.responses.Count() == 1);
			TEST_ASSERT(callback.responses[0]->statusCode == 200);
			TEST_ASSERT(callback.lastErrorFatal);
			TEST_ASSERT(callback.disconnectedCount == 1);
			TEST_ASSERT(callback.events.IndexOf(L"response") < callback.events.IndexOf(L"fatal"));
			TEST_ASSERT(callback.events.IndexOf(L"fatal") < callback.events.IndexOf(L"disconnected"));
			TEST_ASSERT(!socket->HasCallback());
		});

		TEST_CASE(L"HTTP invokes the native WriteAsync outside callback and connection state locks")
		{
			auto socket = Ptr(new FakeAsyncSocketConnection);
			auto timer = Ptr(new ManualTimeoutController);
			auto connection = Ptr(new HttpRequestConnection(
				socket.Obj(),
				HttpRequestConnectionDirection::Client,
				nullptr,
				timer
				));
			RecordingHttpCallback callback;
			connection->InstallCallback(&callback);
			connection->BeginReadingLoopUnsafe();

			EventObject eventProbeDone;
			TEST_ASSERT(eventProbeDone.CreateManualUnsignal(false));
			atomic_vint queued = 0;
			atomic_vint insideCallback = 0;
			atomic_vint completedInsideWrite = 0;
			atomic_vint probeResult = 0;
			socket->beforeWrite = Func<void()>([connection, &eventProbeDone, &queued, &insideCallback, &completedInsideWrite, &probeResult]()
			{
				insideCallback = connection->IsInsideCallback() ? 1 : 0;
				queued = ThreadPoolLite::Queue(Func<void()>([connection, &eventProbeDone, &probeResult]()
				{
					try
					{
						auto overlapping = Ptr(new HttpRequest);
						overlapping->method = L"GET";
						overlapping->requestTarget = L"/overlapping";
						connection->SendRequest(overlapping);
						probeResult = 2;
					}
					catch (const Error&)
					{
						probeResult = 1;
					}
					catch (...)
					{
						probeResult = 3;
					}
					eventProbeDone.Signal();
				})) ? 1 : 0;
				if (queued)
				{
					completedInsideWrite = eventProbeDone.WaitForTime(ConnectTimeout) ? 1 : 0;
				}
			});

			auto request = Ptr(new HttpRequest);
			request->method = L"GET";
			request->requestTarget = L"/lock-probe";
			connection->SendRequest(request);
			socket->beforeWrite = Func<void()>();
			if (queued && !completedInsideWrite)
			{
				TEST_ASSERT(eventProbeDone.WaitForTime(ConnectTimeout));
			}

			TEST_ASSERT(queued == 1);
			TEST_ASSERT(insideCallback == 0);
			TEST_ASSERT(completedInsideWrite == 1);
			TEST_ASSERT(probeResult == 1);
			TEST_ASSERT(socket->writes.Count() == 1);
			socket->CompleteNextWrite();
			connection->Stop();
		});
	}

	class SignalEventOnExit
	{
	private:
		EventObject*					eventObject = nullptr;

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

	struct NativeScenarioState
	{
		EventObject					eventWaitReturned;
		EventObject					eventAccepted;
		EventObject					eventDone;
		SpinLock					lockFailure;
		WString						failure;
		atomic_vint				acceptedCount = 0;
		atomic_vint				serverRequestCount = 0;
		atomic_vint				serverWriteCount = 0;
		atomic_vint				clientResponseCount = 0;
		atomic_vint				clientWriteCount = 0;
		atomic_vint				clientConnectedCount = 0;
		atomic_vint				clientEventSequence = 0;
		atomic_vint				clientLastWriteSequence = -1;
		atomic_vint				clientLastResponseSequence = -1;

		NativeScenarioState()
		{
			CHECK_ERROR(eventWaitReturned.CreateManualUnsignal(false), L"Failed to create the native HTTP wait event.");
			CHECK_ERROR(eventAccepted.CreateManualUnsignal(false), L"Failed to create the native HTTP accept event.");
			CHECK_ERROR(eventDone.CreateManualUnsignal(false), L"Failed to create the native HTTP completion event.");
		}

		void Fail(const WString& message)
		{
			SPIN_LOCK(lockFailure)
			{
				if (failure == L"")
				{
					failure = message;
				}
			}
		}

		void Expect(bool condition, const wchar_t* message)
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
	};

	void RecordCurrentException(NativeScenarioState& state, const wchar_t* operation)
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

	class NativeServerHttpCallback : public Object, public virtual IHttpRequestCallback
	{
	private:
		NativeScenarioState*			state = nullptr;
		IHttpRequestConnection*			connection = nullptr;

		void ValidateFirstRequest(Ptr<HttpRequest> request)
		{
			state->Expect(request->version.major == 1 && request->version.minor == 1, L"The first native HTTP request did not preserve HTTP/1.1.");
			state->Expect(request->method == L"POST", L"The first native HTTP request did not preserve its method.");
			state->Expect(request->requestTarget == L"/echo/%2F?q=a%20b", L"The first native HTTP request did not preserve its encoded target.");
			state->Expect(request->headers.Count() == 4, L"The first native HTTP request returned an unexpected header count.");
			if (request->headers.Count() == 4)
			{
				state->Expect(request->headers[0].name == L"x-repeat" && SameBytes(request->headers[0].value, "first"), L"The first repeated native HTTP field changed name, value, or position.");
				state->Expect(request->headers[1].name == L"x-repeat" && SameBytes(request->headers[1].value, "second"), L"The second repeated native HTTP field changed name, value, or position.");
				state->Expect(request->headers[2].name == L"content-type" && SameBytes(request->headers[2].value, "application/octet-stream"), L"The native HTTP content-type field changed.");
				state->Expect(request->headers[3].name == L"content-length" && SameBytes(request->headers[3].value, "5"), L"The native HTTP fixed-body framing changed.");
			}
			for (auto&& field : request->headers)
			{
				state->Expect(IsLowercaseFieldName(field.name), L"A parsed native HTTP request field was not lowercase.");
			}
			state->Expect(request->body.chunks.Count() == 1, L"The native fixed request body was not represented as one chunk.");
			if (request->body.chunks.Count() == 1)
			{
				state->Expect(SameBytes(request->body.chunks[0].data, "\0\xFF\r\nA"), L"The native fixed request body bytes changed.");
			}
			state->Expect(request->body.trailers.Count() == 0, L"The fixed native request unexpectedly contained trailers.");
		}

		void ValidateSecondRequest(Ptr<HttpRequest> request)
		{
			state->Expect(request->version.major == 1 && request->version.minor == 1, L"The second native HTTP request did not preserve HTTP/1.1.");
			state->Expect(request->method == L"GET", L"The second native HTTP request did not preserve its method.");
			state->Expect(request->requestTarget == L"/empty", L"The second native HTTP request did not preserve its target.");
			state->Expect(HasField(request->headers, L"content-length", "0"), L"The second native HTTP request lost Content-Length: 0.");
			state->Expect(request->body.chunks.Count() == 0, L"The second native HTTP request was not empty.");
		}

	public:
		NativeServerHttpCallback(NativeScenarioState& value)
			: state(&value)
		{
		}

		void OnInstalled(IHttpRequestConnection* value) override
		{
			connection = value;
		}

		void OnReadRequest(Ptr<HttpRequest> request) override
		{
			try
			{
				auto requestCount = ++state->serverRequestCount;
				if (requestCount == 1)
				{
					ValidateFirstRequest(request);
					auto response = Ptr(new HttpResponse);
					response->statusCode = 201;
					response->reason = L"Created";
					AddField(response->headers, L"content-type", "application/octet-stream");
					AddChunk(response->body, "\0\xFF\r");
					AddChunk(response->body, "\nZ");
					AddField(response->body.trailers, L"digest", "native");
					connection->SendResponse(response);
				}
				else if (requestCount == 2)
				{
					ValidateSecondRequest(request);
					connection->SendResponse(CreateEmptyResponse(200, L"Done"));
				}
				else
				{
					state->Fail(L"The native HTTP server received more than two requests.");
					state->eventDone.Signal();
				}
			}
			catch (...)
			{
				RecordCurrentException(*state, L"Native HTTP server request callback");
				state->eventDone.Signal();
			}
		}

		void OnWriteCompleted() override
		{
			state->serverWriteCount++;
		}

		void OnError(const WString& error, bool fatal) override
		{
			state->Fail(WString(fatal ? L"Fatal" : L"Nonfatal") + L" native HTTP server error: " + error);
			state->eventDone.Signal();
		}
	};

	class NativeClientHttpCallback : public Object, public virtual IHttpRequestCallback
	{
	private:
		NativeScenarioState*			state = nullptr;
		IHttpRequestConnection*			connection = nullptr;

		void ValidateFirstResponse(Ptr<HttpResponse> response)
		{
			state->Expect(response->version.major == 1 && response->version.minor == 1, L"The first native HTTP response did not preserve HTTP/1.1.");
			state->Expect(response->statusCode == 201, L"The first native HTTP response changed its status.");
			state->Expect(response->reason == L"Created", L"The first native HTTP response changed its reason.");
			for (auto&& field : response->headers)
			{
				state->Expect(IsLowercaseFieldName(field.name), L"A parsed native HTTP response field was not lowercase.");
			}
			state->Expect(HasField(response->headers, L"content-type", "application/octet-stream"), L"The first native HTTP response lost content-type.");
			state->Expect(HasField(response->headers, L"transfer-encoding", "chunked"), L"The first native HTTP response was not chunked.");
			state->Expect(response->body.chunks.Count() == 2, L"The first native HTTP response did not retain its two chunk boundaries.");
			if (response->body.chunks.Count() == 2)
			{
				state->Expect(SameBytes(response->body.chunks[0].data, "\0\xFF\r"), L"The first native HTTP response chunk changed.");
				state->Expect(SameBytes(response->body.chunks[1].data, "\nZ"), L"The second native HTTP response chunk changed.");
			}
			state->Expect(response->body.trailers.Count() == 1, L"The first native HTTP response lost its trailer.");
			if (response->body.trailers.Count() == 1)
			{
				state->Expect(response->body.trailers[0].name == L"digest", L"The native HTTP response trailer name was not normalized.");
				state->Expect(SameBytes(response->body.trailers[0].value, "native"), L"The native HTTP response trailer value changed.");
			}
		}

		void ValidateSecondResponse(Ptr<HttpResponse> response)
		{
			state->Expect(response->statusCode == 200, L"The second native HTTP response changed its status.");
			state->Expect(response->reason == L"Done", L"The second native HTTP response changed its reason.");
			state->Expect(HasField(response->headers, L"content-length", "0"), L"The second native HTTP response lost Content-Length: 0.");
			state->Expect(response->body.chunks.Count() == 0, L"The second native HTTP response was not empty.");
		}

	public:
		NativeClientHttpCallback(NativeScenarioState& value)
			: state(&value)
		{
		}

		void OnInstalled(IHttpRequestConnection* value) override
		{
			connection = value;
		}

		void OnConnected() override
		{
			state->clientConnectedCount++;
		}

		void OnWriteCompleted() override
		{
			state->clientWriteCount++;
			state->clientLastWriteSequence = ++state->clientEventSequence;
		}

		void OnReadResponse(Ptr<HttpResponse> response) override
		{
			try
			{
				auto responseSequence = ++state->clientEventSequence;
				state->clientLastResponseSequence = responseSequence;
				state->Expect(state->clientLastWriteSequence >= 0 && state->clientLastWriteSequence < responseSequence, L"A native HTTP response callback preceded request write completion.");
				state->Expect(state->clientWriteCount >= state->clientResponseCount + 1, L"A native HTTP response arrived before the matching whole-message write completion.");
				auto responseCount = ++state->clientResponseCount;
				if (responseCount == 1)
				{
					ValidateFirstResponse(response);
					auto request = Ptr(new HttpRequest);
					request->method = L"GET";
					request->requestTarget = L"/empty";
					AddField(request->headers, L"content-length", "0");
					connection->SendRequest(request);
				}
				else if (responseCount == 2)
				{
					ValidateSecondResponse(response);
					state->eventDone.Signal();
				}
				else
				{
					state->Fail(L"The native HTTP client received more than two responses.");
					state->eventDone.Signal();
				}
			}
			catch (...)
			{
				RecordCurrentException(*state, L"Native HTTP client response callback");
				state->eventDone.Signal();
			}
		}

		void OnError(const WString& error, bool fatal) override
		{
			state->Fail(WString(fatal ? L"Fatal" : L"Nonfatal") + L" native HTTP client error: " + error);
			state->eventDone.Signal();
		}
	};

	class NativeScenarioServer : public HttpRequestServer
	{
	private:
		NativeScenarioState*			state = nullptr;
		NativeServerHttpCallback*		callback = nullptr;

	public:
		NativeScenarioServer(
			Ptr<IAsyncSocketServer> server,
			NativeScenarioState& _state,
			NativeServerHttpCallback& _callback
			)
			: HttpRequestServer(server)
			, state(&_state)
			, callback(&_callback)
		{
		}

		~NativeScenarioServer()
		{
			HttpRequestServer::Stop();
		}

		WaitForClientResult OnClientConnected(IHttpRequestConnection* connection) override
		{
			try
			{
				if (++state->acceptedCount != 1)
				{
					state->Fail(L"The native HTTP server accepted more than one connection.");
					return WaitForClientResult::Reject;
				}
				connection->InstallCallback(callback);
				connection->BeginReadingLoopUnsafe();
				state->eventAccepted.Signal();
				return WaitForClientResult::Accept;
			}
			catch (...)
			{
				RecordCurrentException(*state, L"Native HTTP server accept callback");
				state->eventAccepted.Signal();
				state->eventDone.Signal();
				return WaitForClientResult::Reject;
			}
		}
	};

	Ptr<HttpRequest> CreateFirstNativeRequest()
	{
		auto request = Ptr(new HttpRequest);
		request->method = L"POST";
		request->requestTarget = L"/echo/%2F?q=a%20b";
		AddField(request->headers, L"x-repeat", "first");
		AddField(request->headers, L"x-repeat", "second");
		AddField(request->headers, L"content-type", "application/octet-stream");
		AddChunk(request->body, "\0\xFF\r\nA");
		return request;
	}

	void AssertNativeScenarioState(NativeScenarioState& state)
	{
		auto failure = state.GetFailure();
		if (failure != L"")
		{
			TEST_PRINT(failure);
		}
		TEST_ASSERT(failure == L"");
	}

	template<typename TNativeServer, typename TNativeClient>
	void RunNativeScenario()
	{
		auto state = Ptr(new NativeScenarioState);
		NativeServerHttpCallback serverCallback(*state.Obj());
		NativeClientHttpCallback clientCallback(*state.Obj());
		auto nativeServer = Ptr<IAsyncSocketServer>(new TNativeServer(38800));
		auto server = Ptr(new NativeScenarioServer(nativeServer, *state.Obj(), serverCallback));
		Ptr<HttpRequestClient> client;
		bool waitQueued = false;
		bool waitReturned = false;

		try
		{
			server->Start();
			auto nativeClient = Ptr<IAsyncSocketClient>(new TNativeClient(38800));
			client = Ptr(new HttpRequestClient(nativeClient));
			client->GetConnection()->InstallCallback(&clientCallback);
			waitQueued = ThreadPoolLite::Queue(Func<void()>([client, state]()
			{
				SignalEventOnExit signal(state->eventWaitReturned);
				try
				{
					client->WaitForServer();
				}
				catch (...)
				{
					RecordCurrentException(*state.Obj(), L"Native HTTP client WaitForServer");
					state->eventDone.Signal();
				}
			}));
			if (!waitQueued)
			{
				state->Fail(L"Failed to queue native HTTP client WaitForServer.");
				state->eventWaitReturned.Signal();
			}

			waitReturned = state->eventWaitReturned.WaitForTime(ConnectTimeout);
			state->Expect(waitReturned, L"Native HTTP client WaitForServer timed out.");
			state->Expect(state->eventAccepted.WaitForTime(ConnectTimeout), L"The native HTTP server did not accept the client.");
			if (waitReturned && client->GetStatus() == ClientStatus::Connected)
			{
				client->GetConnection()->BeginReadingLoopUnsafe();
				client->GetConnection()->SendRequest(CreateFirstNativeRequest());
				state->Expect(state->eventDone.WaitForTime(TransferTimeout), L"The two-exchange native HTTP scenario timed out.");
			}
			else
			{
				state->Fail(L"The native HTTP client did not connect.");
			}
		}
		catch (...)
		{
			RecordCurrentException(*state.Obj(), L"Running the native HTTP scenario");
		}

		if (client)
		{
			try
			{
				client->GetConnection()->Stop();
				client->GetConnection()->InstallCallback(nullptr);
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"Stopping the native HTTP client");
			}
		}
		if (waitQueued && !waitReturned)
		{
			if (!state->eventWaitReturned.WaitForTime(ConnectTimeout))
			{
				state->Fail(L"Native HTTP client WaitForServer did not drain after Stop.");
			}
		}
		try
		{
			server->Stop();
		}
		catch (...)
		{
			RecordCurrentException(*state.Obj(), L"Stopping the native HTTP server");
		}

		AssertNativeScenarioState(*state.Obj());
		TEST_ASSERT(server->IsStopped());
		TEST_ASSERT(state->acceptedCount == 1);
		TEST_ASSERT(state->serverRequestCount == 2);
		TEST_ASSERT(state->serverWriteCount == 2);
		TEST_ASSERT(state->clientResponseCount == 2);
		TEST_ASSERT(state->clientWriteCount == 2);
		TEST_ASSERT(state->clientConnectedCount == 1);
	}

#if defined VCZH_MSVC

	template<size_t N>
	bool SameChars(const Array<char>& actual, const char(&expected)[N])
	{
		if (actual.Count() != (vint)N - 1)
		{
			return false;
		}
		for (vint i = 0; i < actual.Count(); i++)
		{
			if (actual[i] != expected[i])
			{
				return false;
			}
		}
		return true;
	}

	template<size_t N>
	bool SameAscii(const char* actual, vint actualLength, const char(&expected)[N])
	{
		if (!actual || actualLength != (vint)N - 1)
		{
			return false;
		}
		for (vint i = 0; i < actualLength; i++)
		{
			if (actual[i] != expected[i])
			{
				return false;
			}
		}
		return true;
	}

	bool SameAscii(const char* actual, vint actualLength, const char* expected, vint expectedLength);

	template<size_t N>
	bool HasUnknownHeader(PHTTP_REQUEST request, const char(&name)[N], const char* value)
	{
		for (USHORT i = 0; i < request->Headers.UnknownHeaderCount; i++)
		{
			auto&& header = request->Headers.pUnknownHeaders[i];
			if (
				SameAscii(header.pName, header.NameLength, name) &&
				SameAscii(header.pRawValue, header.RawValueLength, value, (vint)strlen(value))
				)
			{
				return true;
			}
		}
		return false;
	}

	bool SameAscii(const char* actual, vint actualLength, const char* expected, vint expectedLength)
	{
		if (!actual || actualLength != expectedLength)
		{
			return false;
		}
		for (vint i = 0; i < actualLength; i++)
		{
			if (actual[i] != expected[i])
			{
				return false;
			}
		}
		return true;
	}

	class WindowsClientApiServerCallback : public Object, public virtual IHttpRequestCallback
	{
	private:
		NativeScenarioState*			state = nullptr;
		IHttpRequestConnection*			connection = nullptr;

	public:
		WindowsClientApiServerCallback(NativeScenarioState& value)
			: state(&value)
		{
		}

		void OnInstalled(IHttpRequestConnection* value) override
		{
			connection = value;
		}

		void OnReadRequest(Ptr<vl::inter_process::async_tcp_socket::HttpRequest> request) override
		{
			try
			{
				state->serverRequestCount++;
				state->Expect(request->version.major == 1 && request->version.minor == 1, L"WinHTTP did not send an HTTP/1.1 request.");
				state->Expect(request->method == L"POST", L"WinHTTP changed the explicit request method.");
				state->Expect(request->requestTarget == L"/interop/client?encoded=%2F", L"WinHTTP changed the explicit request target.");
				state->Expect(HasField(request->headers, L"x-mixed-field", "mixed-value"), L"The HTTP request server did not normalize WinHTTP's mixed-case custom field.");
				state->Expect(HasField(request->headers, L"content-type", "application/octet-stream"), L"The HTTP request server lost WinHTTP's content type.");
				state->Expect(HasField(request->headers, L"content-length", "11"), L"The HTTP request server did not parse WinHTTP's generated body framing.");
				auto body = FlattenBody(request->body);
				state->Expect(SameBytes(body, "client-body"), L"The HTTP request server changed WinHTTP's request body.");

				auto response = Ptr(new vl::inter_process::async_tcp_socket::HttpResponse);
				response->statusCode = 207;
				response->reason = L"Multi-Status";
				AddField(response->headers, L"content-type", "text/plain; charset=utf-8");
				AddField(response->headers, L"set-cookie", "session=vlppos");
				AddChunk(response->body, "native ");
				AddChunk(response->body, "response");
				connection->SendResponse(response);
			}
			catch (...)
			{
				RecordCurrentException(*state, L"WinHTTP-to-HTTP-server request callback");
				state->eventDone.Signal();
			}
		}

		void OnWriteCompleted() override
		{
			state->serverWriteCount++;
		}

		void OnError(const WString& error, bool fatal) override
		{
			state->Fail(WString(fatal ? L"Fatal" : L"Nonfatal") + L" WinHTTP interop server error: " + error);
			state->eventDone.Signal();
		}
	};

	class WindowsClientApiInteropServer : public HttpRequestServer
	{
	private:
		NativeScenarioState*			state = nullptr;
		WindowsClientApiServerCallback*	callback = nullptr;

	public:
		WindowsClientApiInteropServer(
			Ptr<IAsyncSocketServer> server,
			NativeScenarioState& _state,
			WindowsClientApiServerCallback& _callback
			)
			: HttpRequestServer(server)
			, state(&_state)
			, callback(&_callback)
		{
		}

		~WindowsClientApiInteropServer()
		{
			HttpRequestServer::Stop();
		}

		WaitForClientResult OnClientConnected(IHttpRequestConnection* connection) override
		{
			try
			{
				state->acceptedCount++;
				connection->InstallCallback(callback);
				connection->BeginReadingLoopUnsafe();
				state->eventAccepted.Signal();
				return WaitForClientResult::Accept;
			}
			catch (...)
			{
				RecordCurrentException(*state, L"WinHTTP interop accept callback");
				state->eventAccepted.Signal();
				state->eventDone.Signal();
				return WaitForClientResult::Reject;
			}
		}
	};

	void RunWindowsClientApiToHttpRequestServer()
	{
		using namespace vl::inter_process::async_tcp_socket::windows_socket;
		NativeScenarioState state;
		WindowsClientApiServerCallback serverCallback(state);
		auto nativeServer = Ptr<IAsyncSocketServer>(new AsyncSocketServer(38801));
		auto server = Ptr(new WindowsClientApiInteropServer(nativeServer, state, serverCallback));
		server->Start();

		windows_http::HttpClientApi client(L"127.0.0.1", 38801);
		windows_http::HttpRequest request;
		request.method = L"POST";
		request.query = L"/interop/client?encoded=%2F";
		request.contentType = L"application/octet-stream";
		request.extraHeaders.Add(L"X-MiXeD-Field", L"mixed-value");
		request.SetBodyUtf8(L"client-body");
		client.HttpQuery(request, Func<void(Variant<windows_http::HttpResponse, windows_http::HttpError>)>([&state](Variant<windows_http::HttpResponse, windows_http::HttpError> result)
		{
			SignalEventOnExit signal(state.eventDone);
			try
			{
				if (auto error = result.TryGet<windows_http::HttpError>())
				{
					state.Fail(L"WinHTTP interop query failed: " + error->operation + L": " + error->message);
					return;
				}
				auto&& response = result.Get<windows_http::HttpResponse>();
				state.Expect(response.statusCode == 207, L"WinHTTP did not receive the non-default HTTP server status.");
				state.Expect(SameChars(response.body, "native response"), L"WinHTTP did not flatten the two response chunks correctly.");
				state.Expect(response.contentType == L"text/plain; charset=utf-8", L"WinHTTP did not receive the response content type.");
				state.Expect(response.cookie == L"session=vlppos", L"WinHTTP did not receive the response cookie.");
			}
			catch (...)
			{
				RecordCurrentException(state, L"Validating WinHTTP interop response");
			}
		}));

		state.Expect(state.eventAccepted.WaitForTime(ConnectTimeout), L"The HTTP request server did not accept WinHTTP.");
		state.Expect(state.eventDone.WaitForTime(TransferTimeout), L"The WinHTTP-to-HTTP-server interop test timed out.");
		client.Stop();
		server->Stop();

		AssertNativeScenarioState(state);
		TEST_ASSERT(server->IsStopped());
		TEST_ASSERT(state.acceptedCount == 1);
		TEST_ASSERT(state.serverRequestCount == 1);
		TEST_ASSERT(state.serverWriteCount == 1);
	}

	class HttpSysInteropServer : public windows_http::HttpServerApi
	{
	private:
		NativeScenarioState*			state = nullptr;

	protected:
		void OnHttpRequestReceived(PHTTP_REQUEST request) override
		{
			SignalEventOnExit signal(state->eventAccepted);
			try
			{
				state->serverRequestCount++;
				state->Expect(request->Version.MajorVersion == 1 && request->Version.MinorVersion == 1, L"The async HTTP client did not send HTTP/1.1 to HTTP.sys.");
				state->Expect(request->Verb == HttpVerbPOST, L"The async HTTP client did not preserve the POST verb for HTTP.sys.");
				state->Expect(SameAscii(request->pRawUrl, request->RawUrlLength, "/vlppos-http/submit?encoded=%2F"), L"The async HTTP client did not preserve the raw target for HTTP.sys.");
				auto&& host = request->Headers.KnownHeaders[HttpHeaderHost];
				state->Expect(SameAscii(host.pRawValue, host.RawValueLength, "localhost:38802"), L"The async HTTP client did not send the exact HTTP.sys Host field.");
				state->Expect(HasUnknownHeader(request, "x-interop-field", "async-value"), L"HTTP.sys did not receive the async HTTP client's custom field.");
				auto&& contentLength = request->Headers.KnownHeaders[HttpHeaderContentLength];
				state->Expect(SameAscii(contentLength.pRawValue, contentLength.RawValueLength, "17"), L"HTTP.sys did not receive fixed-length request framing.");
				auto body = GetUtf8Body(request);
				state->Expect(body && body.Value() == L"async-client-body", L"HTTP.sys did not receive the async HTTP client's exact body.");

				auto result = SendResponse(
					GetHttpRequestQueue(),
					request->RequestId,
					{ 299, L"Interop Result", L"http-sys-body", L"text/plain; charset=utf-8" }
					);
				state->Expect(result == NO_ERROR, L"HTTP.sys failed to send the interop response.");
			}
			catch (...)
			{
				RecordCurrentException(*state, L"HTTP.sys interop request callback");
				state->eventDone.Signal();
			}
		}

	public:
		HttpSysInteropServer(NativeScenarioState& value)
			: windows_http::HttpServerApi(L"http://localhost:38802/vlppos-http/", false)
			, state(&value)
		{
		}
	};

	class HttpSysInteropClientCallback : public Object, public virtual IHttpRequestCallback
	{
	private:
		NativeScenarioState*			state = nullptr;

	public:
		HttpSysInteropClientCallback(NativeScenarioState& value)
			: state(&value)
		{
		}

		void OnInstalled(IHttpRequestConnection*) override
		{
		}

		void OnConnected() override
		{
			state->clientConnectedCount++;
		}

		void OnWriteCompleted() override
		{
			state->clientWriteCount++;
		}

		void OnReadResponse(Ptr<vl::inter_process::async_tcp_socket::HttpResponse> response) override
		{
			SignalEventOnExit signal(state->eventDone);
			try
			{
				state->clientResponseCount++;
				state->Expect(state->clientWriteCount == 1, L"The HTTP.sys response arrived before the async request write completed.");
				state->Expect(response->statusCode == 299, L"The async HTTP client did not parse HTTP.sys's non-default status.");
				state->Expect(response->reason == L"Interop Result", L"The async HTTP client did not parse HTTP.sys's reason phrase.");
				for (auto&& field : response->headers)
				{
					state->Expect(IsLowercaseFieldName(field.name), L"The async HTTP client did not normalize an HTTP.sys response field.");
				}
				state->Expect(HasField(response->headers, L"content-type", "text/plain; charset=utf-8"), L"The async HTTP client lost HTTP.sys's content type.");
				auto body = FlattenBody(response->body);
				state->Expect(SameBytes(body, "http-sys-body"), L"The async HTTP client changed HTTP.sys's response body.");
			}
			catch (...)
			{
				RecordCurrentException(*state, L"Validating HTTP.sys interop response");
			}
		}

		void OnError(const WString& error, bool fatal) override
		{
			state->Fail(WString(fatal ? L"Fatal" : L"Nonfatal") + L" HTTP.sys interop client error: " + error);
			state->eventDone.Signal();
		}
	};

	void RunHttpRequestClientToWindowsServerApi()
	{
		using namespace vl::inter_process::async_tcp_socket::windows_socket;
		auto state = Ptr(new NativeScenarioState);
		HttpSysInteropServer server(*state.Obj());
		server.Start();

		HttpSysInteropClientCallback clientCallback(*state.Obj());
		auto nativeClient = Ptr<IAsyncSocketClient>(new AsyncSocketClient(38802));
		auto client = Ptr(new HttpRequestClient(nativeClient));
		client->GetConnection()->InstallCallback(&clientCallback);
		bool waitQueued = ThreadPoolLite::Queue(Func<void()>([client, state]()
		{
			SignalEventOnExit signal(state->eventWaitReturned);
			try
			{
				client->WaitForServer();
			}
			catch (...)
			{
				RecordCurrentException(*state.Obj(), L"HTTP.sys interop WaitForServer");
				state->eventDone.Signal();
			}
		}));
		if (!waitQueued)
		{
			state->Fail(L"Failed to queue HTTP.sys interop WaitForServer.");
			state->eventWaitReturned.Signal();
		}

		bool waitReturned = state->eventWaitReturned.WaitForTime(ConnectTimeout);
		state->Expect(waitReturned, L"HTTP.sys interop WaitForServer timed out.");
		if (waitReturned && client->GetStatus() == ClientStatus::Connected)
		{
			client->GetConnection()->BeginReadingLoopUnsafe();
			auto request = Ptr(new vl::inter_process::async_tcp_socket::HttpRequest);
			request->method = L"POST";
			request->requestTarget = L"/vlppos-http/submit?encoded=%2F";
			AddField(request->headers, L"host", "localhost:38802");
			AddField(request->headers, L"x-interop-field", "async-value");
			AddField(request->headers, L"content-type", "application/json; charset=utf8");
			AddChunk(request->body, "async-client-body");
			client->GetConnection()->SendRequest(request);
			state->Expect(state->eventAccepted.WaitForTime(TransferTimeout), L"HTTP.sys did not receive the async HTTP request.");
			state->Expect(state->eventDone.WaitForTime(TransferTimeout), L"The async HTTP client did not receive the HTTP.sys response.");
		}
		else
		{
			state->Fail(L"The async HTTP client did not connect to HTTP.sys.");
		}

		try
		{
			client->GetConnection()->Stop();
			client->GetConnection()->InstallCallback(nullptr);
		}
		catch (...)
		{
			RecordCurrentException(*state.Obj(), L"Stopping the HTTP.sys interop client");
		}
		if (waitQueued && !waitReturned)
		{
			if (!state->eventWaitReturned.WaitForTime(ConnectTimeout))
			{
				state->Fail(L"HTTP.sys interop WaitForServer did not drain.");
			}
		}
		server.Stop();

		AssertNativeScenarioState(*state.Obj());
		TEST_ASSERT(server.IsStopped());
		TEST_ASSERT(state->serverRequestCount == 1);
		TEST_ASSERT(state->clientResponseCount == 1);
		TEST_ASSERT(state->clientWriteCount == 1);
		TEST_ASSERT(state->clientConnectedCount == 1);
	}

#endif
}

using namespace http_request_test;

TEST_FILE
{
	RunHttpRequestDataHelperTestCases();
	RunChunkHelperTestCases();
	RunFakeConnectionTestCases();
	RunHttpLifecycleTestCases();

	TEST_CASE(L"HTTP request and response complete two native sequential exchanges")
	{
#if defined VCZH_MSVC
		using namespace vl::inter_process::async_tcp_socket::windows_socket;
		RunNativeScenario<AsyncSocketServer, AsyncSocketClient>();
#elif defined VCZH_GCC && defined VCZH_APPLE
		using namespace vl::inter_process::async_tcp_socket::macos_socket;
		RunNativeScenario<AsyncSocketServer, AsyncSocketClient>();
#elif defined VCZH_GCC && !defined VCZH_APPLE
		using namespace vl::inter_process::async_tcp_socket::linux_socket;
		RunNativeScenario<AsyncSocketServer, AsyncSocketClient>();
#endif
	});

#if defined VCZH_MSVC
	TEST_CASE(L"Windows HttpClientApi interoperates with HttpRequestServer")
	{
		RunWindowsClientApiToHttpRequestServer();
	});

	TEST_CASE(L"HttpRequestClient interoperates with Windows HttpServerApi")
	{
		RunHttpRequestClientToWindowsServerApi();
	});
#endif
}
