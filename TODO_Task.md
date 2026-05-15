investigate repro

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

## Task 2

**IMPORTANT** This work changes many repos.
**IMPORTANT** If my proposal is not working in Task 2 because the assumption is wrong, skip Task 3.

Add `new int;` at the last line of `TEST_CASE(L"HttpServer (Channel)")` so that the test case will leak memory.
I found when I was doing this, no memory leak are detected at the end of `Execute.log`. Figure this out. You are granted permission to change files in the `Import` and `.github` folder. Probably `Vlpp.cpp` and `copilotExecute.ps1` will be affected.

Any changes should be also port to `Vlpp` repo (if `Vlpp.h` and `Vlpp.cpp` in `Import` folder is changed) and `Tools` repo (if files in `.github` folder is changed, those files are from `Tools/Copilot`).

After fixing the issue, remove `new int` and check again to see the leak in `Execute.log` is gone, and push all 3 repos if they have local changes.
