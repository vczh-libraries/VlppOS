# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

- you have to follow `REPO-ROOT/.github/Guidelines/Coding.md` when coding.
- you have to run unit test to make sure your change works.
- you have to commit and push all local changes after finishing any task, before doing the next task.
  - It is important to do task one by one strictly, by me designing tasks in this way, we can achieve:
  - Easy-to-understand commits for file changing that is easy to review.
  - Limit side effects so that you don't have to deal with massive of issues at the same time.
- each task will be treated as a new `# Repro`, that is, to wipe the document before execution.

All tasks below are for completing `vl::inter_process`.
`UnitTest` test project has been configured to only run `TestInterProcess.cpp` under debug x64.
I think this is the only test file you need.

## Task 1

Refactor `TestInterProcess.cpp` and `NetworkProtocolChannel(Server|Client)`.
- I realized that adding `IChannelServer::GetChannel` is a bad idea, you are going to delete this function.
  - Because `NetworkProtocolChannelServer` should just handle message delivering, anything leaking to `IChannelServer::GetChannel` is a bad idea. So I think you could read the commit `Refactor inter-process channel sender ids` and revert just this part, but others should kept.
- In order to let `RunNetworkProtocolChannel`'s server being able to send messages:
  - Add a `ServerChannelClient`, and the server thread will call `ConnectLocalClient` on that instance after two clients are connected.
  - Now the `ChannelServer` should no longer implements `IChannelReader<WString>`, being able to do that might mean the original implementationa has fundemental problems. For example, there should be no such things like `ownedChannels`.
  - `ConnectLocalClient` returns the client id for `ServerChannelClient`, and now it has 3 ids.
  - When `ServerChannelClient` is connected, it broadcast "clientid1;clientId2;" to other two clients.
  - This message will be the first message received by Tom and Jerry, the `senderClientId` reveals the client id of `ServerChannelClient`, and then by comparing their client id to `clientId1;clientId2`, Tom knows Jerry and Jerry knows Tom.
  - Continue to do, until Tom and Client sends `Stop` to `ServerChannelClient`.
  - After `ServerChannelClient` receives both stops, the stopping process begins.
- The change means, `AdminClientId` will not be used in the implementation, although it is declared. So any usage of `AdminClientId` in `NetworkProtocolChannel(Server|Client)?` should be removed.

The goal is to make the responsibility of interfaces clean. `IChannelServer` could only deliver messages, it doesn't send messages unless to broadcast errors or system messages, which are not implemented and not involved in the test.

To act like a talking server, a local client registered in the server side is what the original design meant to do.

# UPDATES

# TEST [CONFIRMED]

The configured Debug x64 UnitTest filter already runs `TestInterProcess.cpp`, and the channel tests exercise both named-pipe and HTTP transports through `RunNetworkProtocolChannel`. Success criteria:

- `IChannelServer::GetChannel` is removed, and `NetworkProtocolChannelServer` no longer owns user channel objects for server-originated chat messages.
- `NetworkProtocolChannelServer::ConnectLocalClient` assigns and returns a real client id for a local `ServerChannelClient`.
- `RunNetworkProtocolChannel` completes with Tom, Jerry, and the server-side local client all using positive client ids, with no `AdminClientId` usage in `NetworkProtocolChannelClient`, `NetworkProtocolChannelServer`, or the channel test.
- Debug x64 `UnitTest` builds successfully and the configured UnitTest run passes without a memory leak dump.

# PROPOSALS

- No.1 Route server-side chat through a local channel client [CONFIRMED]

## No.1 Route server-side chat through a local channel client

### CODE CHANGE

- Removed `IChannelServer::GetChannel` and the server-owned user-channel implementation from `NetworkProtocolChannelServer`.
- Changed `IChannelServer::ConnectLocalClient` to return the assigned local client id, and implemented local-client registration, local id detection, local disconnection, and local batch delivery.
- Removed `AdminClientId` usage from `NetworkProtocolChannelClient`, `NetworkProtocolChannelServer`, and `TestInterProcess.cpp`; channel messages now always use positive sender/receiver client ids.
- Refactored `RunNetworkProtocolChannel` so `ChannelServer` only accepts clients, while a server-side `ServerChannelClient` connects locally and broadcasts `clientId1;clientId2;` as the first chat message.
- Updated the test `WStringListSerializer` to length-prefix packages so a single `WString` package can contain semicolons.
- Changed `HttpServerConnection` to queue pending outbound `/Request` responses instead of keeping one overwriteable pending response, preserving the connection response followed immediately by the first channel message.

### CONFIRMED

- Initial Debug x64 build caught one compile error from using `.key/.value` on `indexed`, which was corrected by switching to an explicit index loop.
- Debug x64 build then succeeded with `0 Warning(s)` and `0 Error(s)`.
- The first UnitTest run without a local filter showed all `TestInterProcess.cpp` cases passing and then continued into unrelated test files. The local `UnitTest.vcxproj.user` file was empty despite the task note, so it was updated locally to pass `/F:TestInterProcess.cpp`.
- Final `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64` used `/F:TestInterProcess.cpp` and passed with `Passed test files: 1/1` and `Passed test cases: 4/4`.
- Final `Execute.log` contains no `Detected memory leaks!`, `Dumping objects ->`, or `Object dump complete`.
