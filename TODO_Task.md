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

Refactor `TestInterProcess.cpp` and `NetworkProtocolChannel(Server|Client)`.
- `NetworkProtocolChannelClient` has a little extra code for handling local client, I would like you to make a sub class called `NetworkProtocolLocalChannelClient` and move things there.
  - One important pattern is that some functions handle local client branch and then do common things. In the sub class you can override the function, do local client branch, and call the base method.
  - Remember to move local client specific members to the sub class.
  - In server's ConnectLocalClient it can just `dynamic_cast` to `NetworkProtocolLocalChannelClient` and make sure it succeeds, everything will be cleaner.
  - In this way, no lock needs to protect the `localServer` field.
  - If `NetworkProtocolLocalChannelClient` needs to use any `NetworkProtocolChannelClient` variables, make them protected only.

Verify `SPIN_LOCK`:
- Just like `NetworkProtocolChannel`, you should reorder and group all protected fields under each `SpinLock`, and comment the coverage.
