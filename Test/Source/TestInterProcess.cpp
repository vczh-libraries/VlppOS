#include "../../Source/InterProcess/TextNetworkProtocol.h"
#ifdef VCZH_MSVC
#include "../../Source/InterProcess/Windows/NamedPipe.Windows.h"
#include "../../Source/InterProcess/Windows/HttpClient.Windows.h"
#include "../../Source/InterProcess/Windows/HttpServer.Windows.h"
#endif

using namespace vl;
using namespace vl::inter_process;

namespace mynamespace
{
	class TimeoutThread : public Thread
	{
	public:
		atomic_vint			threadCounter = 0;
		bool				timeout = false;

	protected:

		void Run() override
		{
			auto startTime = DateTime::LocalTime();
			while (DateTime::LocalTime().osMilliseconds - startTime.osMilliseconds < 5000)
			{
				if (threadCounter == 3) return;
			}
			timeout = true;
		}
	};

	void RunTextNetworkProtocol(
		Func<Ptr<INetworkProtocolServer>()> createServer,
		Func<Ptr<INetworkProtocolClient>()> createClient
		)
	{
		auto timeoutThread = Ptr(new TimeoutThread);

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				auto server = createServer();
				auto connection1 = server->WaitForClient();
				auto connection2 = server->WaitForClient();
				Thread::Sleep(3000);
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				auto client = createClient();
				client->WaitForServer();
				Thread::Sleep(3000);
			}
			timeoutThread->threadCounter++;
		});

		ThreadPoolLite::QueueLambda([&]()
		{
			{
				auto client = createClient();
				client->WaitForServer();
				Thread::Sleep(3000);
			}
			timeoutThread->threadCounter++;
		});

		timeoutThread->Start();
		timeoutThread->Wait();
		TEST_ASSERT(!timeoutThread->timeout);
	}
}
using namespace mynamespace;

TEST_FILE
{
#ifdef VCZH_MSVC
	TEST_CASE(L"NamedPipe")
	{
		RunTextNetworkProtocol(
			[]()->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new NamedPipeServer(L"VlppOSTestPipe")); },
			[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new NamedPipeClient(L"VlppOSTestPipe")); }
		);
	});

	TEST_CASE(L"HttpServer")
	{
		RunTextNetworkProtocol(
			[]()->Ptr<INetworkProtocolServer> { return Ptr<INetworkProtocolServer>(new HttpServer(L"/VlppOSTestHttpServer", 8765)); },
			[]()->Ptr<INetworkProtocolClient> { return Ptr<INetworkProtocolClient>(new HttpClient(L"/VlppOSTestHttpServer", 8765)); }
		);
	});
#endif
}