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

Refactor `HttpClient`.
- In `HttpClient::BeginReadingLoopUnsafe` and `HttpClient::SendString` there is two similar `WinHttpSetStatusCallback` calls.
  - In `HttpClient::WaitForServer` there is one more `WinHttpSetStatusCallback` calls with a much simpler handling.
  - I think there is no reason to have 3 different `WinHttpSetStatusCallback` calls as their handling should be almost the same.
  - I would like you to make a protected `HttpClient::SendHttpRequest` function to send all different requests to the server. In this function you can pass an enum to distinguish Connect/Request/Response so that when a full body is received different actions could be taken. And `/Request` doesn't need to call `callback->OnReadString` because `HttpServer` always send messages through `/Response`.
  - Currenty calling to `WinHttpSetStatusCallback` is incorrect. It uses `self->QueueCallback` to block `Stop` but it is not correct. The correct way to handle it is that:
    - In `HttpClient::SendHttpRequest` checks state and ignore if already stopping.
    - `BeginPendingCallback`.
    - If anything error happens before `WinHttpSendRequest` in that function, `EndPendingCallback`.
    - If no error happens before `WinHttpSendRequest`, `EndPendingCallback` will be called when `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` happens, as in the document this status will always be invoked no matter what happens.
    - No `self->QueueCallback` is needed as the piece of code should just be executed in the `WinHttpSetStatusCallback` directly. Calling `ThreadPoolLite::Queue` for any callback is just a waste of resource.
- `hEventWaitForServer` should use `EventObject` instead of `HANDLE`.

Refactor `HttpServer`.
- All interlocked operations should be replaced by `atomic<T>`.

Refactor `NamedPipe.Windows.cpp`.
- All interlocked operations should be replaced by `atomic<T>`.

Remove all 6 `Thread::Sleep(1000);` in TestInterProcess.cpp.

The goal is to make sure all `Stop` actually waits until no more callback could happen.
