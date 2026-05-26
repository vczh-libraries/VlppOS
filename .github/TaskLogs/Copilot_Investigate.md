# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

## Task 1

This task happens in `VlppOS` repo.

General Refactoring:
- `INetworkProtocolClient::WaitForServer` should only be called when status is `Ready`.
  - `NetworkProtocolLocalChannelClient` will skip this check.
- `NetworkProtocolLocalChannelClient`
  - `WaitForServer` should just do nothing, everything should be done in `IChannelServer::ConnectLocalClient`.
  - Common base class with `NetworkProtocolChannelClient` should be extracted as the current base class `NetworkProtocolChannelClient` has members that do not work with `NetworkProtocolLocalChannelClient`.
- `NamedPipeConnection`
  - Remove `lockReadWait` as `readWaitContext` is already atomic.
- `NamedPipeServer::PendingConnection`
  - Remove `lockConnectWait` as `connectWaitContext` is already atomic.
- `HttpServerConnection::InstallCallback`
  - Only move `queuedStrings` in `SPIN_LOCK`.

# UPDATES

# TEST [CONFIRMED]

Use the existing `Test\Source\TestInterProcess.cpp` coverage because the `UnitTest` project is configured for the inter-process work.

The criteria for success are:
- All four inter-process test cases still compile and pass: named pipe network protocol, named pipe channel, HTTP network protocol, and HTTP channel.
- `NetworkProtocolChannelClient` only calls the underlying `INetworkProtocolClient::WaitForServer` from the `Ready` state.
- `NetworkProtocolLocalChannelClient::WaitForServer` is a no-op; local connection completion is done by `IChannelServer::ConnectLocalClient`.
- Shared channel-client state is held in a common base class that does not contain network-only members such as the network protocol client, callback, or wait event.
- `NamedPipeConnection` no longer has `lockReadWait`, and `NamedPipeServer::PendingConnection` no longer has `lockConnectWait`; their wait contexts remain protected by atomics.
- `HttpServerConnection::InstallCallback` only moves `queuedStrings` while holding `SPIN_LOCK`, then invokes callbacks after releasing the lock.
- The unit test process exits without timeouts, crashes, or memory leak reports.

The current unit test project builds successfully before the refactor:
- `copilotBuild.ps1` completed with `Build succeeded.`, `0 Warning(s)`, and `0 Error(s)`.

# PROPOSALS

- No.1 EXTRACT CHANNEL CLIENT BASE AND SIMPLIFY WAIT CONTEXTS

## No.1 EXTRACT CHANNEL CLIENT BASE AND SIMPLIFY WAIT CONTEXTS

Extract a `NetworkProtocolChannelClientBase` that owns shared channel-client behavior: serialization context, channel creation, channel maps, status/client id state, connection notifications, and common batch receiving helpers. Keep network-only state and operations in `NetworkProtocolChannelClient`, including the network protocol client, network callback, wait event, string package parsing, and actual `INetworkProtocolClient::WaitForServer` call.

Make `NetworkProtocolLocalChannelClient` inherit from the common base instead of the network client. Its `WaitForServer` will do nothing; `NetworkProtocolChannelServer::ConnectLocalClient` will continue assigning the id, registering channels, and notifying the local client as the connection boundary.

Remove the `SpinLock` fields that only guarded `readWaitContext` and `connectWaitContext`, because those fields are already atomic. Use atomic exchange/compare-exchange directly around registration, stop, and callback ownership.

Change `HttpServerConnection::InstallCallback` so it moves `queuedStrings` to a local list while holding `lockQueuedStrings`, then invokes `OnInstalled` and queued `OnReadString` callbacks after the lock is released.

### CODE CHANGE
