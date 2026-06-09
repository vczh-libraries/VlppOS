# !!!INVESTIGATE!!!
# PROBLEM DESCRIPTION

`IChannel::(SendTo|BroadcastFrom)Client` should remove the `senderClientId` argument because they should know:
- Channels are owned by a client, the client should tell all channels what are their `senderClientId`.

`BroadcastFromClient` add a blocked client id list argument:
- It is implemented by an overloading method with a `const List<vint>& blockedReceivers` argument.
- All implementation of the original function is going to call this one with an empty list.

`NetworkPackage` will need to have a new `Nullable<List<vint>> extraClientIds;` field:
- Empty or null `extraClientIds` are semantically equivalent.
- During serialization, the first section is going to be `clientId,extraClientId1,extraClientId2,...`.
- When `clientId` is null, it becomes an empty string, so that:
  - When `extraClientIds` is null or empty, the first section is empty.
  - Otherwise, the first section starts with a comma.
  - Therefore you will have enough information to tell if it is an empty `clientId` or empty `extraClientIds`.
- During deserialization, an empty `extraClientIds` is going to be null.
- Currently `extraClientIds` is going to represent `blockedReceivers` when `clientId` is empty.
- When `clientId` is not empty, `extraClientIds` is still deserialized but ignored by `IChannel(Client|Server)` implementations.

In the unit test, `ServerChannelClient::OnConnected`, right after the first `BroadcastFromClient`, the local client is going to call two `BroadcastFromClient`:
- Blocked Tom and say "Hello Jerry from Server".
- Blocked Jerry and say "Hello Tom from Server".
`(Tom|Jerry)ChannelClient` will going to write down if they received this message to `ChannelChatData::clientId(1|2)ReceivedHello`.
- Both will be `TEST_ASSERT` after `TEST_ASSERT(!timeoutThread->timeout);` in `RunNetworkProtocolChannel`.
- In this way you will know if `blockedReceivers` is correctly implemented.
# TEST [CONFIRMED]

Extend the existing channel scenario in `TestInterProcess.cpp`:
- Add a `NetworkPackage ExtraClientIds` round-trip test for null client ids with extras, non-null client ids with extras, and empty extras.
- The server-side local channel client broadcasts the client id announcement, then broadcasts one message blocked from Tom and one message blocked from Jerry.
- Tom and Jerry record whether their own client id received the expected unblocked server message.
- `RunNetworkProtocolChannel` asserts no timeout and asserts both recorded fields are true.

Success criteria:
- The focused `TestInterProcess.cpp` unit test builds and passes for the configured Debug x64 run.
- The channel test proves blocked broadcast delivery by requiring both clients to finish only after they receive their unblocked server message.
- The Windows Debug execution log has no appended memory leak report.

# PROPOSALS

- No.1 DERIVE CHANNEL SENDERS AND SERIALIZE BLOCKED BROADCAST IDS [CONFIRMED]

## No.1 DERIVE CHANNEL SENDERS AND SERIALIZE BLOCKED BROADCAST IDS

Change `IChannel<TPackage>` so `SendToClient` accepts only the receiver id and package, and `BroadcastFromClient` accepts either just the package or the package with `const collections::List<vint>& blockedReceivers`. `NetworkProtocolChannelClientBase::Channel` will derive the sender id from the owning client during batch writing.

Add `NetworkPackage::extraClientIds` and encode the first package section as `clientId,extraClientId1,...`, preserving the existing empty-client-id broadcast marker. Server-side channel handling will interpret `extraClientIds` as blocked receivers only when the package has no direct receiver client id. Local clients will pass the same blocked list into the server delivery path without going through text serialization.

### CODE CHANGE

- Updated `IChannel<TPackage>` and `ChannelSerializer<TSerialization>` so `SendToClient` no longer accepts `senderClientId`, and `BroadcastFromClient` has a blocked-receiver overload.
- Updated `NetworkPackage` with `Nullable<List<vint>> extraClientIds` and first-section serialization/deserialization as `clientId,extraClientId1,...`.
- Updated `NetworkProtocolChannel` queueing and batching to group by receiver plus blocked receiver list, deriving sender ids from the owning channel client.
- Updated remote and local channel client/server paths to pass blocked receiver ids to broadcast delivery and skip those clients.
- Added a `NetworkPackage ExtraClientIds` unit test for the new first-section wire format and empty-extra normalization.
- Extended `TestInterProcess.cpp` channel tests so the server-side local client sends blocked broadcasts and Tom/Jerry assert they each receive the unblocked server message. Tom and Jerry now publish their assigned role ids before the local server client sends role-specific blocked broadcasts, because `clientId1` and `clientId2` are connection-order ids and do not reliably identify roles.

### CONFIRMED

The proposal is confirmed.

`copilotBuild.ps1` completed for `Test/UnitTest/UnitTest.sln` Debug x64 with:
- `Build succeeded.`
- `0 Warning(s)`
- `0 Error(s)`

The focused unit test run used the existing Debug x64 `/F:TestInterProcess.cpp` filter and completed with:
- `Passed test files: 1/1`
- `Passed test cases: 5/5`

`Execute.log` has no `Detected memory leaks` or `Dumping objects` marker after the test summary.

The first focused run failed in `NamedPipe (Channel)` because the new test initially used connection-order ids as Tom/Jerry role ids. After Tom and Jerry report their assigned ids through `ChannelChatData::tomClientId` and `ChannelChatData::jerryClientId`, the server-side local client blocks the actual role ids and both NamedPipe and HTTP channel cases pass.
