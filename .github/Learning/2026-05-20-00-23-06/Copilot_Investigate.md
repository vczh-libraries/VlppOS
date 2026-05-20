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
- `NetworkProtocolChannelClient` has a little extra code for handling local client, I would like you to make a sub class called `NetworkProtocolLocalChannelClient` and move things there.
  - One important pattern is that some functions handle local client branch and then do common things. In the sub class you can override the function, do local client branch, and call the base method.
  - Remember to move local client specific members to the sub class.
  - In server's ConnectLocalClient it can just `dynamic_cast` to `NetworkProtocolLocalChannelClient` and make sure it succeeds, everything will be cleaner.
  - In this way, no lock needs to protect the `localServer` field.
  - If `NetworkProtocolLocalChannelClient` needs to use any `NetworkProtocolChannelClient` variables, make them protected only.

Verify `SPIN_LOCK`:
- Just like `NetworkProtocolChannel`, you should reorder and group all protected fields under each `SpinLock`, and comment the coverage.

# UPDATES

# TEST [CONFIRMED]

The configured Debug x64 UnitTest filter runs `TestInterProcess.cpp`, including both `RunTextNetworkProtocol` and `RunNetworkProtocolChannel` over named pipe and HTTP transports. Success criteria:

- `NetworkProtocolChannelClient` no longer owns local-client state or local-server branches.
- `NetworkProtocolLocalChannelClient` owns the local server pointer and overrides local behavior while reusing common client behavior where appropriate.
- `NetworkProtocolChannelServer::ConnectLocalClient` accepts only `NetworkProtocolLocalChannelClient` instances and keeps local clients in the server delivery map.
- Every `SpinLock` changed in `TextNetworkProtocol.h` and `TestInterProcess.cpp` has its covered fields grouped with a coverage comment.
- Debug x64 `UnitTest` builds successfully and the configured UnitTest run passes without a memory leak dump.

# PROPOSALS

- No.1 Move local channel behavior into NetworkProtocolLocalChannelClient [CONFIRMED]

## No.1 Move local channel behavior into NetworkProtocolLocalChannelClient

### CODE CHANGE

- Added `NetworkProtocolLocalChannelClient` as a subclass of `NetworkProtocolChannelClient`.
- Moved `localServer`, local connection setup, local wait behavior, local disconnection cleanup, local send routing, and local error broadcasting into `NetworkProtocolLocalChannelClient`.
- Removed local-server branches from `NetworkProtocolChannelClient`; the base client now handles network clients only, with the small set of state needed by the local subclass exposed as protected.
- Changed `NetworkProtocolChannelServer` to store local clients as `NetworkProtocolLocalChannelClient` instances and require that cast in `ConnectLocalClient`.
- Refactored `TestInterProcess.cpp` so the server-side channel client inherits from `NetworkProtocolLocalChannelClient`, while Tom and Jerry inherit from `NetworkProtocolChannelClient`.
- Added `SpinLock` coverage comments and grouped covered fields in `TextNetworkProtocol.h` and `TestInterProcess.cpp`.

### CONFIRMED

- Initial Debug x64 build found a dependent-base lookup issue in the templated test helper; this was fixed by calling `this->GetChannels()`.
- Final Debug x64 build succeeded with `0 Warning(s)` and `0 Error(s)`.
- The configured UnitTest run used `/F:TestInterProcess.cpp` and passed with `Passed test files: 1/1` and `Passed test cases: 4/4`.
- Final `Execute.log` contains no `Detected memory leaks!`, `Dumping objects ->`, or `Object dump complete`.
