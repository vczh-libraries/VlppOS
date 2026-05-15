# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

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

# UPDATES

# TEST [CONFIRMED]

Task 1 uses the existing `Test/Source/TestInterProcess.cpp` transport workflow and adds `CHECK_ERROR` assertions for the new status APIs:

- Server status is checked before accepting clients, after each `WaitForClient`, before and after `Thread::Sleep`, and after `Stop`.
- Client status is checked before `WaitForServer`, after `WaitForServer`, before and after `Thread::Sleep`, and after explicitly stopping the connection.
- `ClientStatus::WaitingForServer` is only observable while `WaitForServer` is running, so the test confirms the reachable `Ready`, `Connected`, and `Disconnected` states.

Success criteria:

- The Debug x64 build has 0 warnings and 0 errors.
- The unit test project passes, including both existing `NamedPipe (NetworkProtocol)` and `HttpServer (NetworkProtocol)` cases.

Confirmed with:

- `copilotBuild.ps1 -Configuration Debug -Platform x64`: `Build succeeded`, `0 Warning(s)`, `0 Error(s)`.
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`: `Passed test files: 12/12`, `Passed test cases: 113/113`.

# PROPOSALS

- No.1 [CONFIRMED] Implement transport status APIs

## No.1 Implement transport status APIs

### CODE CHANGE

- Implemented `NamedPipeServer::IsStopped` and `HttpServer::IsStopped`.
- Implemented `NamedPipeClient::GetStatus` and `HttpClient::GetStatus`.
- Added `NamedPipeClient::Stop` so stopping through `INetworkProtocolConnection` updates client status to `Disconnected`.
- Added status `CHECK_ERROR` assertions to the existing named-pipe and HTTP protocol unit tests.

### CONFIRMED

The new accessors compile for all four Windows protocol classes, and the status assertions pass in the existing inter-process workflow. The Debug x64 build is warning-free, and the unit test wrapper completed successfully.
