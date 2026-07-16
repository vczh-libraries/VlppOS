/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Async Socket HTTP/1.1 Connection

***********************************************************************/

#ifndef VCZH_INTERPROCESS_ASYNCSOCKET_HTTPREQUESTIMPL
#define VCZH_INTERPROCESS_ASYNCSOCKET_HTTPREQUESTIMPL

#include "HttpRequest.h"

namespace vl::inter_process::async_tcp_socket
{
	constexpr vint HttpRequestLineSizeLimit = 8 * 1024;
	constexpr vint HttpHeaderBlockSizeLimit = 64 * 1024;
	constexpr vint HttpBodySizeLimit = 16 * 1024 * 1024;
	constexpr vint HttpChunkSizeLineLimit = 4 * 1024;
	constexpr vint HttpTrailerBlockSizeLimit = 64 * 1024;

	enum class HttpRequestConnectionDirection
	{
		Server,
		Client,
	};

	class IHttpRequestTimeoutController : public virtual Interface
	{
	public:
		virtual void						Arm(vint milliseconds, const Func<void()>& callback) = 0;
		virtual void						Refresh() = 0;
		virtual void						CancelAndWait() = 0;
	};

	extern Ptr<IHttpRequestTimeoutController>	CreateHttpRequestTimeoutController();

	class HttpRequestCallbackDomain : public Object
	{
	public:
		struct CallbackFrame;

	private:
		static thread_local CallbackFrame*	currentCallbackFrame;
		CriticalSection						lockState;
		ConditionVariable					cvState;
		vint							activeCallbacks = 0;

	public:
		struct CallbackFrame
		{
			Ptr<HttpRequestCallbackDomain>		domain;
			CallbackFrame*					previous = nullptr;

			CallbackFrame(Ptr<HttpRequestCallbackDomain> _domain);
			~CallbackFrame();
		};

		vint							CurrentCallbackDepth();
		void							WaitForCallbacks(vint callbackDepth);
	};

	class HttpRequestConnectionLifecycle;

	class HttpRequestConnection final
		: public Object
		, public virtual IHttpRequestConnection
		, public virtual IAsyncSocketCallback
	{
	private:
		using Lifecycle = HttpRequestConnectionLifecycle;
		struct CallbackFrame;
		struct SocketCallbackFrame;
		struct TimeoutCallbackFrame;
		static thread_local CallbackFrame*	currentCallbackFrame;
		static thread_local SocketCallbackFrame*
										currentSocketCallbackFrame;
		static thread_local TimeoutCallbackFrame*
										currentTimeoutCallbackFrame;

		Ptr<Lifecycle>						lifecycle;

		static vint						CurrentCallbackDepth(Ptr<Lifecycle> state);
		static vint						CurrentSocketCallbackDepth(Ptr<Lifecycle> state);
		static vint						CurrentTimeoutCallbackDepth(Ptr<Lifecycle> state);
		static void						FinishSocketCall(Ptr<Lifecycle> state);

		template<typename TCallback>
		static void						InvokeHttpCallback(Ptr<Lifecycle> state, bool allowTerminal, TCallback&& invoke);

		static void						SubmitWrite(Ptr<Lifecycle> state, IAsyncSocketConnection* connection, Ptr<AsyncSocketBuffer> buffer);
		static void						InstallTimeout(Ptr<Lifecycle> state, vint milliseconds, const WString& error);
		static void						RefreshTimeout(Ptr<Lifecycle> state);
		static void						ReportRequestFailure(Ptr<Lifecycle> state, HttpRequestFailure failure, bool timeoutOnly = false, bool reserved = false);
		static void						DeliverResponse(Ptr<Lifecycle> state, Ptr<HttpResponse> response, bool closeAfterDelivery);
		static void						ProcessBufferedInput(Ptr<Lifecycle> state);
		static void						NotifyDisconnected(Ptr<Lifecycle> state);
		static void						StopConnection(Ptr<Lifecycle> state, Ptr<Object> retainedAdapter = nullptr);
		static void						ReportFatalError(Ptr<Lifecycle> state, const WString& error);

	public:
		HttpRequestConnection(
			IAsyncSocketConnection* connection,
			HttpRequestConnectionDirection direction,
			Ptr<HttpRequestCallbackDomain> callbackDomain = nullptr,
			Ptr<IHttpRequestTimeoutController> timeoutController = nullptr
			);
		~HttpRequestConnection();

		void							RetainUntilStopped(Ptr<HttpRequestConnection> retainedAdapter, const Func<void()>& drainedCallback);
		void							StopWithRetainedAdapter(Ptr<HttpRequestConnection> retainedAdapter);
		bool							IsInsideCallback();

		void							InstallCallback(IHttpRequestCallback* callback) override;
		void							BeginReadingLoopUnsafe() override;
		void							SendRequest(Ptr<HttpRequest> request, vint responseTimeout = HttpIncompleteMessageTimeout) override;
		void							SendResponse(Ptr<HttpResponse> response) override;
		void							Stop() override;

		void							OnRead(const vuint8_t* buffer, vint size) override;
		void							OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer) override;
		void							OnError(const WString& error, bool fatal) override;
		void							OnConnected() override;
		void							OnDisconnected() override;
		void							OnInstalled(IAsyncSocketConnection* connection) override;
	};
}

#endif
