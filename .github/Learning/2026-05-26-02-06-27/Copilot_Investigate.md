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
- `HttpServer::Stop` waits for active HTTP request callbacks before tearing down request buffers and connection state.
- Channel packages that arrive before a network channel client has processed its connection response are replayed after the client id is known and `OnConnected` has run.
- The unit test process exits without timeouts, crashes, or memory leak reports.

The current unit test project builds successfully before the refactor:
- `copilotBuild.ps1` completed with `Build succeeded.`, `0 Warning(s)`, and `0 Error(s)`.

# PROPOSALS

- No.1 EXTRACT CHANNEL CLIENT BASE AND SIMPLIFY WAIT CONTEXTS [CONFIRMED]

## No.1 EXTRACT CHANNEL CLIENT BASE AND SIMPLIFY WAIT CONTEXTS

Extract a `NetworkProtocolChannelClientBase` that owns shared channel-client behavior: serialization context, channel creation, channel maps, status/client id state, connection notifications, and common batch receiving helpers. Keep network-only state and operations in `NetworkProtocolChannelClient`, including the network protocol client, network callback, wait event, string package parsing, and actual `INetworkProtocolClient::WaitForServer` call.

Make `NetworkProtocolLocalChannelClient` inherit from the common base instead of the network client. Its `WaitForServer` will do nothing; `NetworkProtocolChannelServer::ConnectLocalClient` will continue assigning the id, registering channels, and notifying the local client as the connection boundary.

Remove the `SpinLock` fields that only guarded `readWaitContext` and `connectWaitContext`, because those fields are already atomic. Use atomic exchange/compare-exchange directly around registration, stop, and callback ownership.

Change `HttpServerConnection::InstallCallback` so it moves `queuedStrings` to a local list while holding `lockQueuedStrings`, then invokes `OnInstalled` and queued `OnReadString` callbacks after the lock is released.

### CODE CHANGE

Extracted `NetworkProtocolChannelClientBase` from `NetworkProtocolChannelClient` in `TextNetworkProtocol.h`. The base class now owns shared channel-client state and behavior: serialization context, channel creation, channel maps, status/client id tracking, connection notifications, and channel batch receiving. `NetworkProtocolChannelClient` now keeps only network-specific fields and behavior: `INetworkProtocolClient`, the network callback, the wait event, network package parsing, and the actual call to `INetworkProtocolClient::WaitForServer`.

Changed `NetworkProtocolChannelClient::WaitForServer` so the underlying network client `WaitForServer` is only reached after the channel client is confirmed to be in `ClientStatus::Ready`. Waiting, connected, and disconnected states no longer call through to the network client.

Changed `NetworkProtocolLocalChannelClient` to inherit from `NetworkProtocolChannelClientBase` instead of `NetworkProtocolChannelClient`. Its `WaitForServer` is now a no-op, and local connection completion remains in `NetworkProtocolChannelServer::ConnectLocalClient` through `ConnectLocalServer` and `NotifyLocalConnected`.

Removed `lockReadWait` from `NamedPipeConnection` and `lockConnectWait` from `NamedPipeServer::PendingConnection`. The wait-context pointers are now owned through atomic compare-exchange/exchange. Because a stop can race with wait registration, each wait context records registration completion so the stopping thread or callback owner does not unregister or delete a context before `RegisterWaitForSingleObject` has returned.

Changed `HttpServerConnection::InstallCallback` so it validates the callback, calls `OnInstalled` before publishing the callback pointer, moves `queuedStrings` to a local list inside `SPIN_LOCK(lockQueuedStrings)`, releases the lock, then replays queued strings. This avoids a race where a `/Response` could invoke `OnReadString` before the callback had received `OnInstalled`.

Added pending-callback tracking to `HttpServer`. The request wait callback now marks itself active while it uses `bufferRequest` and connection state, and `HttpServer::Stop` waits for active callbacks after unregistering the wait handle. This closes a race where the callback could clear `hWaitHandleRequest`, making `Stop` miss it and destroy buffers while the callback was still running.

Changed `NetworkProtocolChannelClient` to queue channel packages received before the connection response. When the connection response arrives, the client now sets the assigned id, runs `NotifyConnected`, signals `WaitForServer`, and then replays queued channel packages. This handles the legal ordering where a server-side local channel broadcast is sent before a transport client has processed its connection response.

### CONFIRMED

This proposal is confirmed. The local channel client no longer inherits network-only state, `NetworkProtocolLocalChannelClient::WaitForServer` does nothing, and the only remaining `INetworkProtocolClient::WaitForServer` call is in the `Ready` branch of `NetworkProtocolChannelClient::WaitForServer`. The named-pipe wait locks are gone while callback ownership remains guarded by atomics, HTTP callback installation no longer invokes user callbacks while holding the queued-string lock, active HTTP request callbacks are waited by `Stop`, and pre-connect channel packages are replayed after the client id is installed.

Verification succeeded:
- `copilotBuild.ps1` completed with `Build succeeded.`, `0 Warning(s)`, and `0 Error(s)`.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest` completed with `Passed test files: 12/12` and `Passed test cases: 115/115`.
- The tail of `Execute.log` contains no memory leak dump after the pass summary.
