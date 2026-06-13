# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

## Task 2

This task is on `VlppOS` repo.
`NetworkProtocolChannelServer` has a serious design problem. It should use a `INetworkProtocolServer` insteads of implementing one.
But there are many other `INetworkProtocolServer` implementations, like `HttpServer` or `NamedPipeServer`, these should be `NetworkProtocolChannelServer`'s base class.
So you could declare the class like this:

```C++
template<typename TPackage, typename TSerialization, typename TServerBase>
class NetworkProtocolChannelServer : public TServerBase, public virtual IChannelServer<TPackage>
{
...
public:
  typename<typename ...TArgs>
  NetworkProtocolChannelServer(TArgs&& ...args)
    : TServerBase(std::forward<TArgs&&>(args)...)
    , ...
  {
    ...
  }

  ...
}
```

Add a `IChannelClient* localClient` to `IChannelServer::OnClientConnected` as the last argument.
It will not non-null when the client is a local client.
It will be null otherwise.

# UPDATES

# TEST [CONFIRMED]

- Build `Test/UnitTest/UnitTest.sln` Debug Win32 and Debug x64.
- Run `UnitTest` Debug Win32 and Debug x64.
- Confirm `NetworkProtocolChannelServer` is instantiated with `NamedPipeServer` and `HttpServer` as its base server implementations.
- Confirm `IChannelServer::OnClientConnected` receives null for network clients and non-null for server-side local clients.

# PROPOSALS

- No.1 Template `NetworkProtocolChannelServer` over the protocol server base [CONFIRMED]

## No.1 Template `NetworkProtocolChannelServer` over the protocol server base

Move transport ownership out of `NetworkProtocolChannelServer` by making the protocol server implementation a template base. Keep channel-side connection maps, local-client routing, and message dispatch in `NetworkProtocolChannelServer`, but let the base `INetworkProtocolServer` implementation own listening and network connection lifetimes.

### CODE CHANGE

[CONFIRMED]

- Added `localClient` to `IChannelServer::OnClientConnected`.
- Changed `NetworkProtocolChannelServer` to `NetworkProtocolChannelServer<TPackage, TSerialization, TServerBase>`, inheriting `TServerBase` and forwarding constructor arguments to it.
- Added a small local-server bridge interface so `NetworkProtocolLocalChannelClient` can send through any `NetworkProtocolChannelServer` specialization without knowing the transport base type.
- Updated network accepts to pass `nullptr` and local accepts to pass the `IChannelClient<TPackage>*` instance.
- Updated stop handling so channel bookkeeping and local clients are cleared by the channel layer, while network and pending connection shutdown is delegated to `TServerBase::Stop()`.
- Updated inter-process tests to derive channel servers directly from `NetworkProtocolChannelServer<..., NamedPipeServer>` and `NetworkProtocolChannelServer<..., HttpServer>`, removing manual adapter overrides.
- Added test assertions that the two network channel clients pass null `localClient` and the server-side local channel client passes non-null `localClient`.

### CONFIRMED

- `copilotBuild.ps1 -Configuration Debug -Platform Win32`: passed with 0 errors.
- `copilotBuild.ps1 -Configuration Debug -Platform x64`: passed with 0 errors.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform Win32`: passed 12/12 files and 117/117 cases.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`: passed 12/12 files and 117/117 cases.

## Task 3 Release Support

### CODE CHANGE

[CONFIRMED]

- Regenerated `Release/VlppOS.h` from `Release/CodegenConfig.xml` so downstream repos can import the Task 2 channel-server API changes.
- Tightened the `NetworkProtocolChannelServer` variadic constructor overload so a serializer context plus transport constructor arguments selects the context-aware overload instead of being forwarded entirely to the transport base.

### CONFIRMED

- `copilotBuild.ps1 -Configuration Debug -Platform Win32`: passed with 0 errors after the constructor overload fix.
- `copilotBuild.ps1 -Configuration Debug -Platform x64`: passed with 0 errors after the constructor overload fix.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform Win32`: passed 12/12 files and 117/117 cases after the constructor overload fix.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`: passed 12/12 files and 117/117 cases after the constructor overload fix.

## Task 4 Release Support

### CODE CHANGE

[CONFIRMED]

- Guarded `NetworkProtocolChannelClient` destruction so it does not stop an already-disconnected transport connection.
- Changed `NamedPipeConnection::Stop` to cancel pending pipe I/O before waiting for overlapped read callbacks to drain, avoiding shutdown hangs when the remote side closes the pipe.
- Regenerated `Release/VlppOS.h` from `Release/CodegenConfig.xml` for downstream import into GacUI.
- Regenerated `Release/VlppOS.Windows.cpp` from `Release/CodegenConfig.xml` for downstream import into GacUI.

### CONFIRMED

- `copilotBuild.ps1 -Configuration Debug -Platform x64`: passed with 0 errors.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`: passed 12/12 files and 117/117 cases.
