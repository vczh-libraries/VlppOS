/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	HttpClient

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_HTTPCLIENT
#define VCZH_INTERPROCESS_WINDOWS_HTTPCLIENT

#include "NetworkProtocol.Windows.h"

namespace vl::inter_process
{

class HttpClient : public Object, public virtual INetworkProtocolConnection, public virtual INetworkProtocolClient
{
protected:

	enum class State
	{
		Ready,
		WaitForServerConnection,
		Running,
		Stopping,
	};

	State											state = State::Ready;
	INetworkProtocolCallback*						callback = nullptr;
	WString											baseUrl;

	HINTERNET										httpSession = NULL;
	HINTERNET										httpConnection = NULL;
	WString											urlConnect;
	WString											urlRequest;
	WString											urlResponse;

/***********************************************************************
HttpClient (Reading)
***********************************************************************/

protected:
	static constexpr vint32_t						HttpRespondBodyStep = 65536;
	collections::Array<char8_t>						httpRespondBodyBuffer;
	DWORD											httpRespondBodyBufferWriting = 0;
	DWORD											httpRespondBodyBufferWritingAvailable = 0;

	void											RaiseErrorUnsafe(WString errorMessage);
public:

	void											BeginReadingLoopUnsafe() override;

/***********************************************************************
HttpClient (WaitForServer)
***********************************************************************/

protected:

	HANDLE											hEventWaitForServer = INVALID_HANDLE_VALUE;
	DWORD											dwInternetStatus_WaitForServer = 0;
	DWORD											dwStatusInformationLength_WaitForServer = 0;

public:

	void											WaitForServer() override;

/***********************************************************************
HttpClient (Writing)
***********************************************************************/

protected:
	SpinLock										httpRequestBodiesLock;
	collections::Dictionary<HINTERNET, U8String>	httpRequestBodies;


public:

	void											SendString(const WString& str) override;

/***********************************************************************
HttpClient
***********************************************************************/

public:
	HttpClient(const WString _baseUrl, vint port);
	~HttpClient();

	void											InstallCallback(INetworkProtocolCallback* _callback) override;
	void											Stop() override;
};

}

#endif