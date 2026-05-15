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

Refactor `NetworkProtocolChannelClient` and `NetworkProtocolChannelServer`.
- Check two latest commit titled `Update TextNetworkProtocol.h` you will find I performed some change to `NetworkProtocolChannelClient::Channel`. I would like you to:
  - Pay attention to the change in `NetworkProtocolChannelClient::Channel`, I improved the code, similar issues will also be in the server version.
  - Extract similar part from both channel to NetworkProtocolChannel as much as you can. If there is any differences, respect the client version.
  - Both channel calls `client->BatchWrite` and `server->BatchWrite`, you can add a `BatchWrite` virtual function to the base class (`NetworkProtocolChannel`), and I think no other places call `client->` and `server->`. And two versions of channels inherit from that base class and implement `BatchWrite` for redirection.
  - Check if any shared code that appear in both client and server, will it be extracted to static helper functions in `NetworkProtocolChannel`, or any better form, so that implementation would become simpler? Make your own judgement. But more importantly, I don't want you to just randomly find something to extract so that you can say you extracted more. All extraction must be reasonable.
- Review data structures.
  - I have changed `NetworkProtocolChannelClient::Channel::queuedPackages` from a list to group, so that the list for each group is naturally maintained and can be feed to `BatchWrite` directly, avoiding unnecessary copying. You can check existing code and see if anything could be improved.

# UPDATES

# TEST [CONFIRMED]

Task 1 uses the existing inter-process channel tests in `Test/Source/TestInterProcess.cpp`. The refactor should preserve behavior for both channel transports:

- `NamedPipe (Channel)` must pass.
- `HttpServer (Channel)` must pass.
- The whole Debug x64 UnitTest project should still build with 0 warnings and 0 errors.

Confirmed with:

- `copilotBuild.ps1 -Configuration Debug -Platform x64`: `Build succeeded`, `0 Warning(s)`, `0 Error(s)`.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`: `Passed test files: 12/12`, `Passed test cases: 115/115`.

# PROPOSALS

- No.1 Refactor shared network protocol channel behavior [CONFIRMED]

## No.1 Refactor shared network protocol channel behavior

### CODE CHANGE

- Moved common channel state and behavior into `NetworkProtocolChannel<TPackage, TSerialization>`:
  - channel name and reader access
  - unread package replay
  - grouped queued packages
  - public `BatchWrite(bool&)`
  - `SendToClient` and `BroadcastFromClient`
- Changed the server-side channel queue from `List<QueuedPackage>` to `Group<Nullable<vint>, TPackage>`, matching the improved client-side structure and avoiding the manual regrouping copy.
- Kept client and server differences in small derived `WriteBatch` redirects to their owning protocol adapters.
- Moved channel-name validation and the channel-name join/split wire-format helpers into the shared base.

### CONFIRMED

The refactor keeps the existing named-pipe and HTTP channel workflows passing. The grouped queue now feeds each target batch directly to the transport writer on both client and server paths, while the derived channel classes only encode the client/server-specific destination semantics.
