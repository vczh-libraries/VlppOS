# !!!LEARNING!!!

# Orders

- Use focused `TestInterProcess.cpp` runs for inter-process work [10]
- Debug UnitTest logs append memory leaks after the pass summary [9]
- Split channel clients by role when validating sender ids [5]
- Repeat inter-process transport scenarios instead of sleeping after `Stop()` [3]
- `TestInterProcess_AsyncSocket.cpp` registers shared scenarios once across platforms [2]
- Search project metadata after source file renames [1]
- Test inherited `Thread` completion with a custom subclass [1]
- Synchronize server startup outside dedicated retry tests [1]
- Test shared Socket HTTP routing through one injected listener and client [1]
- Inject response failure when testing Socket HTTP poll requeue [1]
- Verify Windows TUI geometry with the production backend [1]

# Refinements

## Debug UnitTest logs append memory leaks after the pass summary

For Windows Debug UnitTest runs, `copilotExecute.ps1` passes `/C` and `/DebugOutput:<absolute path>`, appends any CRT leak dump to `Execute.log`, and removes the temporary leak file. A run that reports all test cases passed still needs the log tail checked for `Detected memory leaks!`, and any leak dump must be fixed.

## Use focused `TestInterProcess.cpp` runs for inter-process work

When the UnitTest project is configured to focus on `TestInterProcess.cpp` under Debug x64 (for example through a `/F:TestInterProcess.cpp` user filter), use that focused run to validate named-pipe, HTTP, and channel transport changes first. The expected matrix includes the basic network protocol cases and the channel cases for both transports.

For broad refactors to `vl::inter_process`, run the whole `UnitTest` executable afterwards as well, because callback-ordering changes can affect non-focused tests and shutdown memory leak reporting.

## Split channel clients by role when validating sender ids

When testing channel delivery, use separate client classes for each role instead of mixing behaviors in one handler. Let Tom/Jerry-style peer clients remember the peer id announced by the server-side local client, and assert that every peer message arrives with that exact `senderClientId`. Model server speech with its own local client role so all asserted senders are real positive client ids.

Do not assume connection order identifies role order. Have role clients publish their assigned ids before the server-side local client sends role-specific broadcasts or blocked broadcasts, then assert delivery using the actual Tom/Jerry role ids.

Avoid carrying duplicate test fields for the same role identity. If `clientId1` and `clientId2` are already Tom and Jerry, use them directly in blocked-broadcast assertions instead of maintaining parallel `tomClientId` / `jerryClientId` fields.

## Repeat inter-process transport scenarios instead of sleeping after `Stop()`

When validating named-pipe and HTTP callback draining, remove fixed `Thread::Sleep(1000)` delays and repeat each scenario many times. Repetition exposes races where `Stop()` returns before read/connect/request callbacks have drained, while sleeps only hide the broken shutdown boundary. For a platform-specific intermittent deadlock fix, require a substantial consecutive-pass stress run (for example, 30 clean runs) before declaring the race fixed.

## Search project metadata after source file renames

After splitting, renaming, or deleting inter-process source files, search both source includes and MSBuild project/filter metadata for stale file names. A successful focused unit test is not enough if old headers or deleted `.cpp` files remain referenced by project files.

## `TestInterProcess_AsyncSocket.cpp` registers shared scenarios once across platforms

Keep the async-socket behavioral `TEST_CASE` registrations in one platform-neutral helper. Windows, Linux, and macOS branches should supply only their concrete server/client types, maximum read-block size, and timed-wait binding, then invoke the common registration. Add a new platform by filling its prepared binding and running the existing cases; do not copy the five scenarios.

## Test inherited `Thread` completion with a custom subclass

Keep a focused test whose directly derived `Thread` returns immediately from `Run()`, then require `Wait()` to complete, its work to be visible, and its state to be `Thread::Stopped`. Integration hangs can otherwise be misdiagnosed as transport deadlocks when the worker work has finished and only the generic thread-completion contract is broken.

## Synchronize server startup outside dedicated retry tests

Shared protocol scenarios should deterministically start the listener before clients when their purpose is framing, callback ordering, FIFO sends, channel routing, and shutdown. On Windows, posting `ConnectEx` just before `listen` can leave one pending attempt waiting for the TCP retransmission interval even though the library's retry path never runs; use an explicit startup barrier for that scenario instead of changing socket options or retry timing.

Keep client-before-server behavior in a dedicated native async-socket test that intentionally and deterministically exercises retry. Preserve repeated protocol runs and consecutive sends, but do not make an incidental scheduling race responsible for unrelated coverage.

## Test shared Socket HTTP routing through one injected listener and client

To verify the Socket HTTP composition boundary, construct multiple server APIs with the exact same server pointer and distinct prefixes, then call all of them through one client API. Cover exact and descendant routes, a textual near-match that must return 404, longest-prefix selection, independent registration shutdown, and final listener shutdown.

## Inject response failure when testing Socket HTTP poll requeue

Do not assume that stopping an HTTP client after the server claims a long poll will make the server's response submission fail. A successful TCP write only proves local acceptance, and disconnect processing may be delayed by the same completion worker used by the test hook.

Use a private test hook to cancel the claimed server request context immediately before response submission. Require that the failed completion restores the retained message to the FIFO head and that a replacement poll receives the same token and exact UTF-8 bytes. Keep the production rule unchanged; treating a locally successful send as failed can duplicate messages without a protocol acknowledgement.

## Verify Windows TUI geometry with the production backend

An injected backend is appropriate for deterministic layout, input, timer, resize, and replay tests, but it cannot prove Win32 console takeover or restoration. Also run the production executable in a console whose scrollback buffer is taller than its viewport. While active, inspect that buffer and viewport dimensions match through repeated grow/shrink operations and that no vertical-scrollbar condition returns.

After application-controlled shutdown, inspect the real process before any wrapper adjusts child-exit scrollback and require the original buffer size, window rectangle, modes, and sentinel contents to be restored.
