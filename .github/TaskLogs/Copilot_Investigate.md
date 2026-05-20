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
I removed INetworkProtocolServer::WaitForClient, it is replaced by OnClientConnected, and the server will just begin to listen to client until Stop is called.

I removed IChannelServer::WaitForClient, it is no longer needed, and it already has a OnClientConnedted.

INetworkProtocolServer:
For named pipe, follow the above plan
For http, it is called after /Connect is called. You can now remove semaphoreQueuedConnections and lockQueuedConnections and queuedConnections, HTTP connections is born async but the previous implelementation force it to sync.

IChannelServer:
It should implement INetworkProtocolServer::OnClientConnected, and always return Accept.
THe reason for that is that the client needs to send channel names for exchanging for a client id, when the client id is generated, IChannelServer::OnClientConnected is called. If user chooses to return Reject, disconnect the client.
It should not affect how ConnectLocalClient is called but let's see.

# UPDATES

# TEST [CONFIRMED]

Use the existing `Test\Source\TestInterProcess.cpp` unit tests because the `UnitTest` project is configured to run this file under debug x64 for the inter-process work.

The criteria for success are:
- `NamedPipe (NetworkProtocol)` and `HttpServer (NetworkProtocol)` compile and pass while using `INetworkProtocolServer::OnClientConnected` instead of `WaitForClient`.
- `NamedPipe (Channel)` and `HttpServer (Channel)` compile and pass while using `IChannelServer::OnClientConnected` to accept or reject channel clients after channel names are received.
- The unit test process exits without timeouts, crashes, or memory leak reports.

The solution builds successfully with 0 warnings and 0 errors.

Running `UnitTest` reproduces the problem: `NamedPipe (NetworkProtocol)` times out at `TEST_ASSERT(!timeoutThread->timeout)`, proving that at least one server/client thread remains blocked and the async accept path is not reliable.

# PROPOSALS

- No.1 COMPLETE SYNCHRONOUS NAMED-PIPE CONNECTIONS DIRECTLY

## No.1 COMPLETE SYNCHRONOUS NAMED-PIPE CONNECTIONS DIRECTLY

`NamedPipeServer::BeginListening` creates a `PendingConnection` and calls `ConnectNamedPipe`. For asynchronous completion it adds the object to `pendingConnections` and the wait callback later calls `CompletePendingConnection(PendingConnection*, bool)`, which finds the stored object before completing it.

For synchronous completion (`ERROR_SUCCESS` or `ERROR_PIPE_CONNECTED`), the object has not been stored in `pendingConnections`, but the current code still calls the pointer overload. That overload cannot find the object, so it does nothing: no next listener is started, no `OnClientConnected` callback is raised, and the test times out.

The proposal is to call the `Ptr<PendingConnection>` overload directly in the synchronous cases so the connection is finalized immediately.

### CODE CHANGE
