investigate repro

- you have to follow `REPO-ROOT/.github/Guidelines/Coding.md` when coding.
- you have to run unit test to make sure your change works.
- you have to commit and push all local changes after finishing any task, before doing the next task.
  - It is important to do task one by one strictly, by me designing tasks in this way, we can achieve:
  - Easy-to-understand commits for file changing that is easy to review.
  - Limit side effects so that you don't have to deal with massive of issues at the same time.

All tasks below are for completing `vl::inter_process`.
`UnitTest` test project has been configured to only run `TestInterProcess.cpp` under debug x64.
I think this is the only test file you need.

## Task 1

- `INetworkProtocolClient::GetStatus` and `INetworkProtocolServer::IsStopped` is added but `(NamedPipe|Http)(Server|Client)` do not implement them. You need to implement it.
  - To test them, you need to `CHECK_ERROR` before/after `WaitForClient` (for client) and `Thread::Sleep` (for all) in the test case. Therefore `ClientStatus::WaitingForClient` will be unreachable in the test, which is fine.

## Task 2

- Complete `NetworkProtocolChannelClient` and `NetworkProtocolChannelServer`.
- All requirements are in comments, follow them strictly.
- Add `RunNetworkProtocolChannel` in `TestInterProcess.cpp`.
  - Organize in a new `namespace mynamespace` after the existing one, better organizing the code.
  - Two more `(Channel)` versions of `TEST_CASE` should be added at the end inside `#ifdef VCZH_MSVC`.
  - Form the test case in a similar way.
  - In `mynamespace` it will be necessary to create sub classes for:
    - `NetworkProtocolChannelClient<WString, xxx>` for listening to client callbacks.
    - `NetworkProtocolChannelServer<WString, xxx>` for listening to client callbacks.
    - The serialization helper so that serialization from `List<WString>` to `WString` is doable.
      - Since this is only for unit test, we can assume there will be no ";" in text message, therefore this could be a delimiter.
      - Use `wcschr` to traverse through all ";" will be easier.
  - To make the code clean, `(NamedPipe|Http)(Server|Client)` could be created and feed to `NetworkProtocolChannel(Server|Client)` in lambda in test cases.
