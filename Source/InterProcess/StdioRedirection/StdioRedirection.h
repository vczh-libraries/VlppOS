#ifndef VCZH_INTERPROCESS_STDIOREDIRECTION
#define VCZH_INTERPROCESS_STDIOREDIRECTION

#include "../NetworkProtocol.h"

namespace vl::inter_process::stdio_redirection
{
	class IStdioRedirectionProcess : public virtual Interface
	{
	public:
		virtual vint								Read(vuint8_t* buffer, vint size, WString& errorMessage) = 0;
		virtual bool								Write(const vuint8_t* buffer, vint size, WString& errorMessage) = 0;
		virtual void								CloseInput() = 0;
		virtual void								CloseOutput() = 0;
		virtual void								WaitForExit() = 0;
	};

	extern Ptr<IStdioRedirectionProcess>				CreateStdioRedirectionProcess(const WString& command);

	class StdioRedirectionConnection
		: public Object
		, public virtual INetworkProtocolConnection
	{
		class Lifecycle;
		struct CallbackFrame;

		static thread_local CallbackFrame*			currentCallbackFrame;
		Ptr<Lifecycle>								lifecycle;

		static vint								CurrentCallbackDepth(Ptr<Lifecycle> state);
		static void								InvokeCallback(Ptr<Lifecycle> state, bool allowTerminal, const Func<void(INetworkProtocolCallback*)>& callback);
		static void								NotifyDisconnected(Ptr<Lifecycle> state);
		static void								ReportLocalError(Ptr<Lifecycle> state, const WString& errorMessage);
		static void								Disconnect(Ptr<Lifecycle> state, bool sendExit);
		static void								ReadLoop(Ptr<Lifecycle> state);
		static bool								ProcessLine(Ptr<Lifecycle> state, const WString& line);
		static void								FinishReader(Ptr<Lifecycle> state);

	public:
		StdioRedirectionConnection();
		StdioRedirectionConnection(Ptr<IStdioRedirectionProcess> process);
		~StdioRedirectionConnection();

		void									InstallCallback(INetworkProtocolCallback* callback) override;
		void									BeginReadingLoopUnsafe() override;
		void									SendString(const WString& str) override;
		void									Stop() override;

		ClientStatus							GetStatus();
		bool									IsInCurrentCallback();
	};

	class StdioRedirectionClient
		: public Object
		, public virtual INetworkProtocolClient
	{
	private:
		Ptr<StdioRedirectionConnection>				connection;

	public:
		StdioRedirectionClient();

		INetworkProtocolConnection*				GetConnection() override;
		void									WaitForServer() override;
		ClientStatus							GetStatus() override;
	};

	class StdioRedirectionServer
		: public Object
		, public virtual INetworkProtocolServer
	{
		class Lifecycle;
		Ptr<Lifecycle>								lifecycle;

		static void								FinalizeStop(Ptr<Lifecycle> state);
		static void								FinishConnect(Ptr<Lifecycle> state, Ptr<StdioRedirectionConnection> connection, bool keepConnection);

	public:
		StdioRedirectionServer();
		~StdioRedirectionServer();

		WaitForClientResult					OnClientConnected(INetworkProtocolConnection* connection) override;
		void									Start() override;
		void									Stop() override;
		bool									IsStopped() override;

		void									ConnectNewClient(const WString& command);
	};
}

#endif
