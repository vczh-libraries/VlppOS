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
	void RunTextNetworkProtocol(
		Func<Ptr<INetworkProtocolServer>()> createServer,
		Func<Ptr<INetworkProtocolClient>()> createClient
		)
	{

	}
}
using namespace mynamespace;

TEST_FILE
{
#ifdef VCZH_MSVC
	TEST_CASE(L"NamedPipe")
	{
		RunTextNetworkProtocol(
			[]() { return Ptr<INetworkProtocolServer>(new NamedPipeServer(L"VlppOSTestPipe")); },
			[]() { return Ptr<INetworkProtocolClient>(new NamedPipeClient(L"VlppOSTestPipe")); }
		);
	});

	TEST_CASE(L"HttpServer")
	{
		RunTextNetworkProtocol(
			[]() { return Ptr<INetworkProtocolServer>(new HttpServer(L"VlppOSTestHttpServer", 8765)); },
			[]() { return Ptr<INetworkProtocolClient>(new HttpClient(L"VlppOSTestHttpServer", 8765)); }
		);
	});
#endif
}