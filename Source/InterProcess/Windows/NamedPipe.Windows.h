/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)

Interfaces:
	NamedPipeServer
	NamedPipeClient

***********************************************************************/

#ifndef VCZH_INTERPROCESS_WINDOWS_NAMEDPIPE
#define VCZH_INTERPROCESS_WINDOWS_NAMEDPIPE

#include "NetworkProtocol.Windows.h"

namespace vl::inter_process
{

constexpr const wchar_t* NamedPipeId = L"\\\\.\\pipe\\GacUIRemoteProtocol";

class NamedPipeSharedCommon : public INetworkProtocol
{
protected:
	// NamedPipe doesn't support a single message that is larger than 64K
	static constexpr vint32_t						MaxMessageSize = 65536;

	INetworkProtocolCallback*						callback = nullptr;
	HANDLE											hPipe = INVALID_HANDLE_VALUE;
};

class NamedPipeSharedReading : public virtual NamedPipeSharedCommon
{
private:
	bool											firstRead = true;
	collections::Array<BYTE>						bufferReadFile;
	stream::MemoryStream							streamReadFile;
	HANDLE											hWaitHandleReadFile = INVALID_HANDLE_VALUE;
	OVERLAPPED										overlappedReadFile;
	HANDLE											hEventReadFile = INVALID_HANDLE_VALUE;

	void											BeginReadingUnsafe();
	void											SubmitReadBufferUnsafe(vint bytes);
	void											EndReadingUnsafe();

protected:
	void											BeginReadingLoopUnsafe();

	NamedPipeSharedReading();
	~NamedPipeSharedReading();
};

class NamedPipeSharedWriting : public virtual NamedPipeSharedCommon
{
private:
	stream::MemoryStream							streamWriteFile;
	OVERLAPPED										overlappedWriteFile;
	HANDLE											hEventWriteFile = INVALID_HANDLE_VALUE;

	vint32_t										WriteInt32ToStream(vint32_t number);
	vint32_t										WriteStringToStream(const WString& str);
	void											BeginSendStream();
	void											EndSendStream(vint32_t bytes);

protected:
	void											SendString(const WString& channelName, const WString& str) override;

	NamedPipeSharedWriting();
	~NamedPipeSharedWriting();
};

class NamedPipeShared
	: public NamedPipeSharedReading
	, public NamedPipeSharedWriting
{
protected:
	NamedPipeShared(HANDLE _hPipe);
	~NamedPipeShared();

public:

	void											Stop();

	void											InstallCallback(INetworkProtocolCallback* _callback) override;
	void											BeginReadingLoopUnsafe() override;
	void											SendString(const WString& channelName, const WString& str) override;
};

class NamedPipeServer : public NamedPipeShared
{
protected:
	static HANDLE									ServerCreatePipe();

public:
	NamedPipeServer();
	~NamedPipeServer();

	void											WaitForClient();
};

class NamedPipeClient : public NamedPipeShared
{
protected:
	static HANDLE									ClientCreatePipe();

public:
	NamedPipeClient();
	~NamedPipeClient();

	void											WaitForServer();
};

#endif