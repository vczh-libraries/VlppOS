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

**IMPORTANT** This work happens in `VlppOS` repo.

Refactor `TestInterProcess.cpp` to change `RunNetworkProtocolChannel` works like:
- Split `ChannelClient` to `TomChannelClient` and `JerryChannelClient`, no mixing of handling together.
- Instead of server returning `OK` to both client, it returns the "client id of another client" to each client.
  - On connected, no name is sent, instead sends `Hello Server`. Name will be no longer useful in thie test case.
  - This would be the first message receiving from the server and each client can remember the other client id.
- Message deliverying do not based one name anymore.
  - No `Jerry:Hello` anymore, just send `Hello` using the client id, so the message got delivered to another client. Server will never convert this message to `Tom>Hello`. The same to other messages.
  - `vint senderClientId` will be added as the first argument to `IChannelReader::OnRead` so that the callback knows who is sending this message. `CHECK_ERROR` should be used to ensure `senderClientId` is exactly the "client id of another client" sending from the server. And when the message is sending from the server, the server should use `AdminClientId` so that the client knows it is comming from the server.

Refactor of `NetworkProtocolChannel(Server|Client)`
  - `vint senderClientId` is added to `IChannel::(SendTo|BroadcastFrom)Client` but it is not implemented.
    - `clientId` renamed to `receiverClientId` in `SendToClient`. Use this name in implementations too.
    - If a message is generated from a server, it should use `AdminClientId`. Check if `-1` is used anywhere, they will be not allowed.
     - Ensure `senderClientId` exists, and `receiverClientId` exists to unless it is `AdminClientId`.
  - `NetworkProtocolChannelServer::Channel::WriteBatch` might need to fix.
  - `IChannelServer::GetChannel` is added so that the user of `NetworkProtocolChannelServer` is able to do communication. Channel objects will be maintained in the server implementation, but only when a channel from a name is required, the object will be created. When the channel does not exist, `CHECK_ERROR`.
    - Check all other functions, if any argument is wrong and the caller is able to know that by querying with other methods, `CHECK_ERROR`.
  - For all members that are not in `IChannel(Server|Client)`, they should all be private. Therefore users of `NetworkProtocolChannel(Server|Client)` can only use members in the interface. This will affect sub classes in the test case. Here is also a chance to delete useless members.

`RunTextNetworkProtocol` should not change.

# UPDATES

# TEST [CONFIRMED]

The existing Debug x64 UnitTest filter runs `TestInterProcess.cpp`; this task changes the inter-process network protocol channel behavior and the corresponding test. Success criteria:

- Build the Debug x64 `UnitTest` project successfully.
- Run `UnitTest` through `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`.
- `TestInterProcess.cpp` passes with sender client ids validated by the refactored channel clients.
- No memory leak dump appears at the end of `Execute.log`.

Confirmed with:

- `copilotBuild.ps1 -Configuration Debug -Platform x64`: `Build succeeded`, `0 Warning(s)`, `0 Error(s)`.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`: `Passed test files: 12/12`, `Passed test cases: 115/115`.
- `Execute.log` contains no `Detected memory leaks!` or `Dumping objects ->`.

# PROPOSALS

- No.1 Add sender client ids to channel delivery [CONFIRMED]

## No.1 Add sender client ids to channel delivery

### CODE CHANGE

- Updated `IChannelReader::OnRead` to receive `senderClientId`, and propagated this through channel serializers and `NetworkProtocolChannel`.
- Implemented sender/receiver-aware channel queues. Client-to-server packages use `clientId` as receiver, server-to-client packages use it as sender, and server-originated channel messages use `AdminClientId`.
- Added `IChannelServer::GetChannel(const WString&)` and moved server channel creation behind it.
- Added validation for connected sender and receiver ids, including `AdminClientId` for server-only or server-originated messages.
- Refactored `RunNetworkProtocolChannel` so Tom and Jerry are separate client classes. Both send `Hello Server`, receive the other client id from `AdminClientId`, and route `Hello` / `Good` / `Stop` by client id instead of embedding names in message bodies.

### CONFIRMED

The Debug x64 build succeeded with 0 warnings and 0 errors. The UnitTest script ran the configured test binary successfully, including the refactored channel cases, and `Execute.log` ended with `Passed test files: 12/12` and `Passed test cases: 115/115` without a memory leak dump.
