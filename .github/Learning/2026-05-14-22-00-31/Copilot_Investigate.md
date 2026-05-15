# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

The test project has already been configured to only run `TestInterProcess.cpp` when debug x64.
Your work is to find out what happened and fix the issue.
The test case runs one server and two clients in different threads, using NamedPipe and HttpServer, both fail.
I have debugged it for you and find out the server never receives `Tom` and `Jerry` so the process cannot proceed.
The test case is using a lot of spin lock and events but I think the process is pretty simple therefore I have confidence. But you need to carefully figure out what is going wrong, is NamedPipe and HttpServer all implemented incorrectly?

After fixing the issue, commit and push all changes to the current branch. DO NOT ASK ME ANY QUESTION and I will not be watching at you.

# UPDATES

## UPDATE

I just find I forgot to call `BeginReadingLoopUnsafe`, which is by designed to call, but it seems nothing has changed. Please continue to follow [investigate.prompt.md](.github/prompts/investigate.prompt.md) to finish the work.

## UPDATE

You can't assume HttpClient will be new-ed unless in the test case. Just a reminder.

## UPDATE

I am going to worry about why the Thread::Sleep(1000) doesn't help, maybe you could just call server and client's Stop function before the sleep.

## UPDATE

I would expect that the Stop call would handles everything and after that no action will be running, so if Stop doesn't work, we could fix the issue instead of trying other ways in the test case.

# TEST [CONFIRMED]

Use the existing `Test/Source/TestInterProcess.cpp` unit test file with Debug x64 and `/F:TestInterProcess.cpp`.

The test runs the same text chat workflow through `NamedPipe` and `HttpServer`: the server accepts two clients, receives `Tom` and `Jerry`, sends `OK` to both, relays `Tom:Hello` and `Jerry:Good`, and finishes after both clients send `Stop`.

Success criteria:
- The focused unit test file completes without the timeout assertion.
- Both `NamedPipe` and `HttpServer` test cases pass.
- Build succeeds before running the test.

Confirmed with:
- `copilotBuild.ps1 -Configuration Debug -Platform x64`
- `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest -Configuration Debug -Platform x64`
- The focused test was run twice after the final fix; both runs reported `Passed test files: 1/1` and `Passed test cases: 2/2`.

# PROPOSALS

- No.1 [CONFIRMED] Fix Windows inter-process transport framing, HTTP routing, and async shutdown

## No.1 Fix Windows inter-process transport framing, HTTP routing, and async shutdown

### ANALYSIS

There were several independent transport bugs.

`NamedPipeConnection::EndReadingUnsafe` consumed an extra `vint32_t count` before reading the message string. The writer sends a payload byte count and then one string length/body, so the extra read skipped the real string length and corrupted message parsing. `NamedPipeConnection` also did not allocate the read buffer before `ReadFile`, and `NamedPipeClient` attempted a single `CreateFile` even when the second client raced the server's second `CreateNamedPipe`.

`HttpServer` accepted `/Response/GUID` POSTs but routed them as if they were long-poll `/Request/GUID` receives. The server therefore never delivered client messages such as `Tom` and `Jerry` to the connection callback. The connect response also returned URLs with the base path already included, while `HttpClient::WaitForServer` prepends the base path, producing double-prefixed URLs. Empty 200 responses for `/Response` needed no entity chunk, and stale pending request IDs had to be cleared after a response was sent or lost.

After message routing was fixed, debugger runs showed teardown failures. The server destructor cleared `connection->server` before canceling a pending long-poll request, causing a null dereference. `HttpClient::Stop()` also returned while WinHTTP callbacks and request handles could still reference the client object. Sleeping in the test cannot fix this because the problematic callbacks are caused by `Stop()` and handle closure after the sleep begins.

### CODE CHANGE

- Fixed named pipe message parsing to read the sent string frame correctly.
- Allocated the named pipe read buffer and made client pipe creation retry while the server creates/accepts the second pipe instance.
- Made named pipe `Stop()` cancel pending overlapped IO, unregister the wait callback, and prevent the read loop from restarting after stop.
- Routed HTTP `/Response/GUID` bodies through `HttpServerConnection::SubmitResponse`, then acknowledged the POST with an empty 200 response.
- Returned relative request/response URLs from `/Connect` so the client constructs the intended full paths.
- Cleared or preserved pending HTTP long-poll state correctly after successful sends and canceled sends.
- Moved HTTP server connection cleanup into `HttpServer::Stop()`, canceling pending requests before clearing connection back-pointers.
- Made HTTP client async callbacks drain through `Stop()` without assuming `HttpClient` is heap-allocated. Active request handles are tracked explicitly, closed by `Stop()`, and the client waits for queued work plus final WinHTTP handle-closing callbacks before returning.

### CONFIRMED

`Debug|x64` build succeeds.

The focused `TestInterProcess.cpp` run now passes both test cases repeatedly:
- `NamedPipe`
- `HttpServer`
