# !!!LEARNING!!!

# Orders

- Debug UnitTest logs append memory leaks after the pass summary [2]
- Split channel clients by role when validating sender ids [1]
- Use focused `TestInterProcess.cpp` runs for inter-process work [1]

# Refinements

## Debug UnitTest logs append memory leaks after the pass summary

For Windows Debug UnitTest runs, `copilotExecute.ps1` passes `/C` and `/DebugOutput:<absolute path>`, appends any CRT leak dump to `Execute.log`, and removes the temporary leak file. A run that reports all test cases passed still needs the log tail checked for `Detected memory leaks!`, and any leak dump must be fixed.

## Split channel clients by role when validating sender ids

When testing two-client channel delivery, use separate client classes for the two roles instead of mixing both behaviors in one handler. Let each client remember the peer id sent by the server and assert that every peer message arrives with that exact `senderClientId`.

## Use focused `TestInterProcess.cpp` runs for inter-process work

When the UnitTest project is configured to focus on `TestInterProcess.cpp` under Debug x64, use that focused run to validate named-pipe, HTTP, and channel transport changes first. The expected matrix includes the basic network protocol cases and the channel cases for both transports.
