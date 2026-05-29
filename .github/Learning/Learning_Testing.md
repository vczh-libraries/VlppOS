# !!!LEARNING!!!

# Orders

- Debug UnitTest logs append memory leaks after the pass summary [9]
- Use focused `TestInterProcess.cpp` runs for inter-process work [9]
- Split channel clients by role when validating sender ids [3]
- Repeat inter-process transport scenarios instead of sleeping after `Stop()` [1]

# Refinements

## Debug UnitTest logs append memory leaks after the pass summary

For Windows Debug UnitTest runs, `copilotExecute.ps1` passes `/C` and `/DebugOutput:<absolute path>`, appends any CRT leak dump to `Execute.log`, and removes the temporary leak file. A run that reports all test cases passed still needs the log tail checked for `Detected memory leaks!`, and any leak dump must be fixed.

## Use focused `TestInterProcess.cpp` runs for inter-process work

When the UnitTest project is configured to focus on `TestInterProcess.cpp` under Debug x64 (for example through a `/F:TestInterProcess.cpp` user filter), use that focused run to validate named-pipe, HTTP, and channel transport changes first. The expected matrix includes the basic network protocol cases and the channel cases for both transports.

For broad refactors to `vl::inter_process`, run the whole `UnitTest` executable afterwards as well, because callback-ordering changes can affect non-focused tests and shutdown memory leak reporting.

## Split channel clients by role when validating sender ids

When testing channel delivery, use separate client classes for each role instead of mixing behaviors in one handler. Let Tom/Jerry-style peer clients remember the peer id announced by the server-side local client, and assert that every peer message arrives with that exact `senderClientId`. Model server speech with its own local client role so all asserted senders are real positive client ids.

## Repeat inter-process transport scenarios instead of sleeping after `Stop()`

When validating named-pipe and HTTP callback draining, remove fixed `Thread::Sleep(1000)` delays and repeat each scenario many times. Repetition exposes races where `Stop()` returns before read/connect/request callbacks have drained, while sleeps only hide the broken shutdown boundary.
