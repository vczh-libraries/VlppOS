#include "AsyncSocket_HttpServer.h"
#include "../NetworkProtocolHttp.h"

#include <random>

namespace vl::inter_process::async_tcp_socket
{
	using namespace collections;

	namespace
	{
		constexpr vint						GeneratedTokenLength = 36;

		WString ValidateServerUrlPrefix(const WString& urlPrefix)
		{
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::SocketHttpServer::SocketHttpServer(Ptr<IAsyncSocketServer>, const WString&)#"
			auto normalizedUrlPrefix = urlPrefix;
			while (normalizedUrlPrefix.Length() > 0 && normalizedUrlPrefix[normalizedUrlPrefix.Length() - 1] == L'/')
			{
				normalizedUrlPrefix = normalizedUrlPrefix.Left(normalizedUrlPrefix.Length() - 1);
			}
			CHECK_ERROR(ValidateHttpNetworkProtocolBaseUrl(normalizedUrlPrefix), ERROR_MESSAGE_PREFIX L"urlPrefix must be empty or a legal ASCII origin-form path prefix.");
			CHECK_ERROR(ValidateHttpRequestLine(L"GET", normalizedUrlPrefix + HttpServerUrl_Connect) == HttpRequestLineValidationResult::Succeeded, ERROR_MESSAGE_PREFIX L"The /Connect target exceeds the HTTP request-line limit.");
			const WString tokenPlaceholder = L"000000000000000000000000000000000000";
			CHECK_ERROR(ValidateHttpRequestLine(L"POST", normalizedUrlPrefix + HttpServerUrl_Request + L"/" + tokenPlaceholder) == HttpRequestLineValidationResult::Succeeded, ERROR_MESSAGE_PREFIX L"The /Request target plus a generated token exceeds the HTTP request-line limit.");
			CHECK_ERROR(ValidateHttpRequestLine(L"POST", normalizedUrlPrefix + HttpServerUrl_Response + L"/" + tokenPlaceholder) == HttpRequestLineValidationResult::Succeeded, ERROR_MESSAGE_PREFIX L"The /Response target plus a generated token exceeds the HTTP request-line limit.");
			return normalizedUrlPrefix;
#undef ERROR_MESSAGE_PREFIX
		}

		bool HasEmptyBody(Ptr<HttpRequest> request)
		{
			if (!request || request->body.chunks.Count() != 0 || request->body.trailers.Count() != 0) return false;
			HttpFraming framing;
			if (AnalyzeHttpFraming(request->headers, framing) != HttpFramingAnalysisResult::Succeeded) return false;
			if (framing.kind == HttpFramingKind::None) return true;
			return
				framing.kind == HttpFramingKind::ContentLength &&
				framing.contentLength == 0 &&
				framing.contentLengthFieldCount == 1 &&
				framing.contentLengthValueCount == 1 &&
				framing.contentLengthValuesPlainDecimal;
		}

		bool DecodeSubmittedMessage(Ptr<SocketHttpRequestContext> context, Ptr<HttpRequest> request, WString& message)
		{
			if (!request || request->body.trailers.Count() != 0) return false;
			HttpFraming framing;
			if (AnalyzeHttpFraming(request->headers, framing) != HttpFramingAnalysisResult::Succeeded) return false;
			if (
				framing.kind != HttpFramingKind::ContentLength ||
				framing.contentLength == 0 ||
				framing.contentLengthFieldCount != 1 ||
				framing.contentLengthValueCount != 1 ||
				!framing.contentLengthValuesPlainDecimal ||
				CountHttpFields(request->headers, L"content-type") != 1
				)
			{
				return false;
			}
			auto contentType = FindHttpField(request->headers, L"content-type");
			return
				HttpFieldValueEqualsAscii(contentType->value, HttpNetworkProtocolContentType) &&
				context->TryGetBodyUtf8(message) &&
				IsValidHttpNetworkProtocolMessage(message);
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
		class SocketHttpServerOutboundMessage : public Object
		{
		public:
			Array<vuint8_t>					body;
		};

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
			List<Ptr<SocketHttpServerOutboundMessage>>
										queuedOutbound;
			List<Ptr<SocketHttpRequestContext>>
										queuedPollRegistrations;
			Ptr<SocketHttpRequestContext>		pendingPoll;
			Ptr<SocketHttpRequestContext>		inFlightPoll;
			Ptr<SocketHttpServerOutboundMessage>
										inFlightMessage;
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
				List<Ptr<SocketHttpServerOutboundMessage>>
										generated;

				InboundFrame(Ptr<SocketHttpServerConnectionLifecycle> _state);
				~InboundFrame();
			};

			struct PollWork
			{
				Ptr<SocketHttpRequestContext>		context;
				Ptr<SocketHttpServerOutboundMessage>
										message;

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
			bool DispatchInbound(const WString& message, Ptr<SocketHttpServerOutboundMessage>& response);
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
				submitted = work.context->RespondBytes(
					200,
					L"OK",
					HttpNetworkProtocolContentType,
					work.message->body,
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
					state->inFlightMessage = nullptr;
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

		bool SocketHttpServerConnection::DispatchInbound(const WString& message, Ptr<SocketHttpServerOutboundMessage>& response)
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

			List<Ptr<SocketHttpServerOutboundMessage>> generated;
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
#define ERROR_MESSAGE_PREFIX L"vl::inter_process::async_tcp_socket::SocketHttpServerConnection::SendString(const WString&)#"
			Array<vuint8_t> validated;
			CHECK_ERROR(str.Length() > 0, ERROR_MESSAGE_PREFIX L"A logical HTTP message cannot be empty.");
			CHECK_ERROR(IsValidHttpNetworkProtocolMessage(str), ERROR_MESSAGE_PREFIX L"A logical HTTP message must contain valid Unicode without NUL.");
			CHECK_ERROR(EncodeStrictUtf8(str, validated), ERROR_MESSAGE_PREFIX L"A logical HTTP message must contain valid Unicode without NUL.");
			CHECK_ERROR(validated.Count() <= HttpBodySizeLimit, ERROR_MESSAGE_PREFIX L"The UTF-8 message exceeds HttpBodySizeLimit.");
#undef ERROR_MESSAGE_PREFIX
			auto message = Ptr(new SocketHttpServerOutboundMessage);
			message->body = std::move(validated);
			auto state = lifecycle;
			PollWork work;
			CS_LOCK(state->lockState)
			{
				CHECK_ERROR(!state->stopStarted, L"SocketHttpServerConnection::SendString cannot send on a stopped connection.");
				if (currentInboundFrame && currentInboundFrame->state == state)
				{
					currentInboundFrame->generated.Add(message);
					return;
				}
				state->queuedOutbound.Add(message);
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
				context->RespondStatus(404, L"Route not found");
				return;
			}

			if (request->method == L"GET" && path == HttpServerUrl_Connect && HasEmptyBody(request))
			{
				auto connection = lifecycle->CreateConnection(lifecycle);
				if (!connection)
				{
					context->RespondStatus(404, L"Connection rejected");
					return;
				}

				WaitForClientResult result = WaitForClientResult::Reject;
				try { result = connection->InvokeClientConnected(owner); }
				catch (...) { result = WaitForClientResult::Reject; }
				if (result != WaitForClientResult::Accept)
				{
					connection->Stop();
					context->RespondStatus(404, L"Connection rejected");
					return;
				}

				auto token = connection->GetToken();
				auto body = CreateHttpNetworkProtocolConnectBody(
					WString::Unmanaged(HttpServerUrl_Request) + L"/" + token,
					WString::Unmanaged(HttpServerUrl_Response) + L"/" + token
					);
				context->RespondUtf8(200, L"OK", HttpNetworkProtocolContentType, body);
				return;
			}

			WString token;
			if (request->method == L"POST" && ExtractToken(path, HttpServerUrl_Request, token) && HasEmptyBody(request))
			{
				auto connection = lifecycle->FindConnection(token);
				if (connection && connection->RegisterPoll(context)) return;
				context->RespondStatus(404, L"Connection not found");
				return;
			}

			WString message;
			if (request->method == L"POST" && ExtractToken(path, HttpServerUrl_Response, token) && DecodeSubmittedMessage(context, request, message))
			{
				auto connection = lifecycle->FindConnection(token);
				Ptr<SocketHttpServerOutboundMessage> response;
				if (connection && connection->DispatchInbound(message, response))
				{
					Array<vuint8_t> empty;
					context->RespondBytes(200, L"OK", HttpNetworkProtocolContentType, response ? response->body : empty);
					return;
				}
			}

			context->RespondStatus(404, L"Route not found");
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

	SocketHttpServer::SocketHttpServer(Ptr<IAsyncSocketServer> server, const WString& urlPrefix)
		: SocketHttpServerApi(server, ValidateServerUrlPrefix(urlPrefix))
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
