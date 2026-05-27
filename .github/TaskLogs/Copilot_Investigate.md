# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

## Task 1

This task happens in `VlppOS` repo.

`IChannelClient::OnError` is renamed to `IChannelClient::OnReadError`. This is a callback that only listen to `IChannelServer::BroadcastError`.
`IChannelClient::OnLocalError` is added. Other errors go here.
- For example, currently `HttpClient` calls `RaiseErrorUnsafe` when there is any connection error happens, it should call `OnLocalError` instead.
- When `/Connect` or `/Respond` fails, retry for 3 times. Each failure calls `OnLocalError`, non fatal except for the last retry
- When `/Request` fails, just retry, because it would timeout when the http server does not send any request, no `OnLocalError` is needed.
- When the client already stopped or disconnected, no retry is issued, and in this case, no `OnLocalError` is needed.
- For named pipe, any error causing the pipe to close would issue `OnLocalError` with fatal.
- Only after calling `OnLocalError` with fatal, disconnect the client.
`HttpServerApi::SendResponse` should group the last 4 arguments into a struct `HttpServerResponse`.

## Task 2

This task happens in `VlppParser2` repo.
Release `VlppOS` to `VlppParser2`.

Move `JsonNodeListSerializer` from `GuiRemoteProtocol_Channel_Json.h` in `GacUI` to `GlrJson.h` in `VlppParser2`, put it in namespace `vl::glr::json`.
Add a similar `XmlElementListSerializer` in `GlrXml.h`.
- When serializing, group all elements in an `<Array>` element, build an element (not document) and `XmlPrint`.
- When deserializing, just use all elements in the root element, no need to care about the name or attributes of the root element. Use `XmlParseElement`.

## Task (Final Validation)

This task happens in `GacUI` and `GacJS` repo.
Release `VlppOS` and `VlppParser2 to `GacUI`.

### GacUI

Make sure `RemotingTest_Core` and `RemotingTest_Win32_Renderer` works with `/Http /FCT`.
- Since `IChannelClient::OnError` is renamed, the renderer will build break, just rename the function too, but no need to handle `OnLocalError`.
- Delete `JsonNodeListSerializer` as it is moved to `VlppParser2`.

To verify that, you should launch both processes with the debugger, so that you are able to know the renderer actually communicate commands correctly with core.
You may need to write a piece of temporary powershell script to close the process in a gentle way:
  - You can use whatever way you like, including calling Windows API, to close the renderer gently.
  - After the renderer exits, ensure that core will be notified and exits. If the code has no problem core should already been working that way.
  - Delete that script before commiting.

Release `GacUI`.

### GacJS

Follows `Tools\DebugGacUIWithBrowser.md` to run `RemotingTest_Core` with `/Http /RPT` and make sure `GacJS` could launch and operate the UI.
Make sure test cases work. Half of tests fail in `Gaclib\website\entry`, figure out why and fix all of them.
- Hint: it works on an en-US machine which is faster.
- You are now running on an zh-CN machine which is slower.
- Some cases fail because of localization, make sure it works on both zh-CN and en-US.
- Machine performance might not be a factor, make your own judgement.

# UPDATES

# TEST [CONFIRMED]

Use the existing inter-process unit tests and targeted build coverage:

- Build `Test\UnitTest\UnitTest.sln` because the channel interfaces and Windows HTTP/named-pipe implementations are changed.
- Run the `UnitTest` executable. The existing inter-process tests cover named-pipe and HTTP channel connect/request/response paths and local client/server broadcast behavior.
- Success criteria: all unit tests pass, the code compiles with the renamed `IChannelClient::OnReadError` and new `IChannelClient::OnLocalError`, and `Execute.log` does not contain a memory leak report.

# PROPOSALS

- No.1 Split remote read errors from local transport errors [CONFIRMED]

## No.1 Split remote read errors from local transport errors

Complete the channel API split by renaming the remaining `IChannelClient::OnError` uses to `OnReadError`, add an `INetworkProtocolCallback::OnLocalError` hook for lower-level transports, and route local transport failures through it. HTTP `/Connect` and `/Response` retry up to three attempts and report each failed attempt through `OnLocalError`, fatal only on the last attempt. HTTP `/Request` retries silently while the client is still running. Named-pipe failures that close the pipe report a fatal local error before notifying disconnection. Also replace `HttpServerApi::SendResponse`'s final response arguments with an `HttpServerResponse` struct.

### CODE CHANGE

- Updated `IChannelClient` implementations to use `OnReadError` only for `IChannelServer::BroadcastError` messages and added `OnLocalError` as the local transport error path.
- Added `INetworkProtocolCallback::OnLocalError` and routed fatal client-side local errors through it before disconnect notification.
- Updated `HttpClient` so `/Connect` and `/Response` failures call `OnLocalError` and retry up to 3 attempts, with only the last failure fatal. `/Request` failures retry without calling `OnLocalError`.
- Updated named-pipe read/write close errors to report a fatal local error before `OnDisconnected` when the connection was not already stopped.
- Added `HttpServerResponse` and changed `HttpServerApi::SendResponse` to take the response struct instead of four separate response fields.
- Regenerated `Release\VlppOS.*` from `Release\CodegenConfig.xml`.

### CONFIRMED

Built `Test\UnitTest\UnitTest.sln` with `copilotBuild.ps1`: build succeeded with 0 warnings and 0 errors. Ran `UnitTest` with `copilotExecute.ps1 -Mode UnitTest -Executable UnitTest`: all 12/12 test files and 115/115 test cases passed. `Execute.log` ended at the passing summary and did not contain a memory leak report.
